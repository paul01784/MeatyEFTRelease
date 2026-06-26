#include "../app/includes.h"
#include "headers/maingame.h"


#include "headers/utils.h"

#include "../memory/Memory.h"

#include "../app/globals.h"
#include "../app/maps.h"
#include "../app/taskManager.h"


#include <cstdint>
#include <codecvt>
#include "headers/unitysdk.h"
#include "unity/gameObjectManager.h"
#include "headers/players.h"
#include "headers/camera.h"
#include "headers/exfil.h"
#include "headers/unityHelper.h"
#include "headers/loot.h"
#include "headers/questManager.h"
#include "headers/wishlist.h"
#include "headers/explosives.h"
#include "headers/dogtag.h"



MainGame mainGame;

//defaults
uint64_t MainGame::gameObjectManager = NULL;

uint64_t MainGame::gameWorld = NULL;
uint64_t MainGame::localGameWorld = NULL;

uint64_t MainGame::registeredPlayers = NULL;
uint64_t MainGame::registeredPlayersList = NULL;
int MainGame::registeredPlayersCount = 0;

bool MainGame::btrAllocated = FALSE;

std::string MainGame::selectedLocation = "";
bool MainGame::onlineRaid = false;

int MainGame::pmcNumber = 1;

uint64_t MainGame::player_buffer[327] = { 0 };

uint64_t MainGame::localPlayerPtr = NULL;
uint64_t MainGame::localPlayerHands = NULL;
glm::vec3 MainGame::localLocation = glm::vec3();
glm::vec2 MainGame::localRotation = glm::vec2();
std::string MainGame::localGroupId = "";
bool MainGame::localIsScoped = false;
bool MainGame::localIsSavage = false;
uint64_t MainGame::localPlayerPWA = NULL;
uint64_t MainGame::localplayerProfile = NULL;

std::array<uint64_t, 2> MainGame::active_objects;
std::array<uint64_t, 2> MainGame::tagged_objects;

void MainGame::clearCache() {

    appGlobals::runThreads.store(false, std::memory_order_release);
    appGlobals::runRadar.store(false, std::memory_order_release);

    Sleep(500);

    this->gameWorld = NULL;
    this->localGameWorld = NULL;
    this->registeredPlayers = NULL;
    this->registeredPlayersList = NULL;
    this->registeredPlayersCount = 0;
    std::fill(std::begin(this->player_buffer), std::end(this->player_buffer), 0);
    this->selectedLocation = "";
    this->onlineRaid = FALSE;
    this->localPlayerPtr = NULL;
    this->localPlayerHands = NULL;
    this->localLocation = {};
    this->localGroupId = "";
    this->localIsScoped = false;
    this->localRotation = {};
    this->localIsSavage = false;

    this->pmcNumber = 1;

    this->btrAllocated = false;

    Sleep(500);

    LOGS.logInfo("[MAIN][CACHE] Data cleared");
}

static std::string read_widecharr(const std::uintptr_t address, const std::size_t size) {
    const auto buffer = std::make_unique<char[]>(size);
    mem.Read(address, buffer.get(), size);
    return std::string(buffer.get());
}

static unsigned short utf8_to_utf166(const char* val) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
    std::u16string dest = convert.from_bytes(val);
    return *reinterpret_cast<unsigned short*>(&dest[0]);
}

bool MainGame::checkIfRaidStarted()
{
    if (!Utils::valid_pointer(mainGame.localPlayerPtr))
        return false;

    if (!Utils::valid_pointer(mainGame.localPlayerHands))
        return false;

    std::string className;

    try
    {
        className = ReadName(mainGame.localPlayerHands, 64, false);
    }
    catch (...)
    {
        return false;
    }

    if (IsNullOrWhiteSpace(className))
        return false;

    if (className.find("EmptyHandsController") != std::string::npos)
        return false;

    return true;
}

void MainGame::updateLocalPlayerPtr()
{
    mainGame.localPlayerPtr = mem.Read<uint64_t>(mainGame.localGameWorld + sdk::ClientLocalGameWorld::MainPlayer);
}

bool MainGame::updatePlayerList()
{
    
    if (!mem.vHandle)
        return false;

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return false;

    try
    {
        updateLocalPlayerPtr();
    }
    catch (...)
    {
        LOGS.logError("[PLAYERS][LIST] updateLocalPlayerPtr failed");
        return false;
    }

    auto TryReadValue = [&](uint64_t address, auto& out) -> bool
        {
            using T = std::decay_t<decltype(out)>;

            out = {};

            if (!Utils::valid_pointer(address))
                return false;

            try
            {
                return mem.Read(address, &out, sizeof(T));
            }
            catch (...)
            {
                return false;
            }
        };

    auto TryReadBuffer = [&](uint64_t address, void* out, size_t size) -> bool
        {
            if (!Utils::valid_pointer(address))
                return false;

            if (!out || size == 0)
                return false;

            try
            {
                return mem.Read(address, out, size);
            }
            catch (...)
            {
                return false;
            }
        };

    uint64_t registeredPlayers = 0;
    uint64_t registeredPlayersList = 0;
    int registeredPlayersCount = 0;

    if (!TryReadValue(
        mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers,
        registeredPlayers))
    {
        return false;
    }

    if (!Utils::valid_pointer(registeredPlayers))
        return false;

    if (!TryReadValue(registeredPlayers + 0x10, registeredPlayersList))
        return false;

    if (!Utils::valid_pointer(registeredPlayersList))
        return false;

    if (!TryReadValue(registeredPlayers + 0x18, registeredPlayersCount))
        return false;

    if (registeredPlayersCount <= 0)
        return false;

    constexpr int MAX_REASONABLE_REGISTERED_PLAYERS = 512;

    if (registeredPlayersCount > MAX_REASONABLE_REGISTERED_PLAYERS)
    {
        LOGS.logError("[PLAYERS][LIST] registeredPlayersCount too large, skipping update");
        return false;
    }

    const int bufferCapacity =
        static_cast<int>(std::size(mainGame.player_buffer));

    const int readCount =
        std::min(registeredPlayersCount, bufferCapacity);

    if (readCount <= 0)
        return false;

    std::vector<uint64_t> tempBuffer;
    tempBuffer.resize(static_cast<size_t>(readCount), 0);

    const uint64_t playerArrayStart = registeredPlayersList + 0x20;

    if (!TryReadBuffer(
        playerArrayStart,
        tempBuffer.data(),
        sizeof(uint64_t) * static_cast<size_t>(readCount)))
    {
        return false;
    }

    //valid player pointers into the real buffer.
    int validCount = 0;

    for (int i = 0; i < readCount; ++i)
    {
        const uint64_t player = tempBuffer[static_cast<size_t>(i)];

        if (!Utils::valid_pointer(player))
            continue;

        mainGame.player_buffer[validCount] = player;
        validCount++;
    }

    if (validCount <= 0)
        return false;

    //commit final values after the whole read succeeded.
    mainGame.registeredPlayers = registeredPlayers;
    mainGame.registeredPlayersList = registeredPlayersList;
    mainGame.registeredPlayersCount = validCount;

    return true;
}

void MainGame::getPlayerListDetails()
{

    try {

        LOGS.logInfo("[MAIN] Waiting for player details");

        globals::radarSubText = "Gathering player details";

        while (true)
        {
            Sleep(10);

            mainGame.registeredPlayers = mem.Read<uint64_t>(
                mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers);

            if (!Utils::valid_pointer(mainGame.registeredPlayers))
                continue;

            mainGame.registeredPlayersList = mem.Read<uint64_t>(mainGame.registeredPlayers + 0x10);
            if (!Utils::valid_pointer(mainGame.registeredPlayersList))
                continue;

            mainGame.registeredPlayersCount = mem.Read<int>(mainGame.registeredPlayers + 0x18);
            if (mainGame.registeredPlayersCount <= 0)
            {
                mainGame.registeredPlayers = 0;
                mainGame.registeredPlayersList = 0;
                mainGame.registeredPlayersCount = 0;
                continue;
            }

            // Keep count within buffer bounds
            if (mainGame.registeredPlayersCount > std::size(mainGame.player_buffer))
            {
                mainGame.registeredPlayersCount = std::size(mainGame.player_buffer);
            }

            // Clear buffer
            std::fill(std::begin(mainGame.player_buffer),
                std::end(mainGame.player_buffer),
                0);

            mem.Read(
                mainGame.registeredPlayersList + 0x20,
                mainGame.player_buffer,
                sizeof(uint64_t) * mainGame.registeredPlayersCount);

            size_t validCount = 0;
            for (size_t i = 0; i < mainGame.registeredPlayersCount; ++i)
            {
                if (Utils::valid_pointer(mainGame.player_buffer[i]))
                    ++validCount;
            }

            if (validCount > 1)
                break;
        }

    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in getPlayerListDetails: " + std::string(e.what()) + ". Retrying...");

    }
    catch (...) {
        LOGS.logError("Unknown exception caught in getPlayerListDetails. Retrying...");

    }
}

void MainGame::getGameWorldDetails() {

    try {

        LOGS.logInfo("[Main] Waiting for raid start!");

        globals::radarSubText = "Trying to resolve game world details";

        while (true)
        {
            // Small sleep while we wait for gameworld to resolve
            Sleep(1000);

            // Make sure our cache is refreshed
            //mem.RefreshLight();

            GameObjectManager gom;

            this->gameWorld = gom.GetGameWorldFromListFAST("GameWorld", false);

            if (!Utils::valid_pointer(this->gameWorld))
                continue;

            // Update registered players pointer
            mainGame.registeredPlayers = mem.Read<uint64_t>(
                mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers);

            if (!Utils::valid_pointer(mainGame.registeredPlayers))
                continue;

            mainGame.registeredPlayersList = mem.Read<uint64_t>(mainGame.registeredPlayers + 0x10);
            if (!Utils::valid_pointer(mainGame.registeredPlayersList))
                continue;

            mainGame.registeredPlayersCount = mem.Read<int>(mainGame.registeredPlayers + 0x18);
            if (mainGame.registeredPlayersCount <= 0)
            {
                mainGame.registeredPlayers = 0;
                mainGame.registeredPlayersList = 0;
                mainGame.registeredPlayersCount = 0;
                continue;
            }

            if (mainGame.registeredPlayersCount > std::size(mainGame.player_buffer))
            {
                mainGame.registeredPlayersCount = std::size(mainGame.player_buffer);
            }

            std::fill(std::begin(mainGame.player_buffer),
                std::end(mainGame.player_buffer),
                0);

            mem.Read(
                mainGame.registeredPlayersList + 0x20,
                mainGame.player_buffer,
                sizeof(uint64_t) * mainGame.registeredPlayersCount);

            size_t validCount = 0;
            for (size_t i = 0; i < mainGame.registeredPlayersCount; ++i)
            {
                if (Utils::valid_pointer(mainGame.player_buffer[i]))
                    ++validCount;
            }

            if (validCount > 1)
                break;

            
        }
    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in getGameWorldDetails: " + std::string(e.what()) + ". Retrying...");
       
    }
    catch (...) {
        LOGS.logError("Unknown exception caught in getGameWorldDetails. Retrying...");
       
    }
}

void MainGame::cameraAndAimWorker()
{
    try {

        TaskManager cameraAndAimTask;

        //Task List
        cameraAndAimTask.addTask("cameraTask", std::bind(&Camera::cameraTask, &camera), &globals::taskCamera);
        
        //run the tasks
        cameraAndAimTask.run();

    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in cameraAndAimWorker: " + std::string(e.what()) + ". Retrying...");
    }
    catch (...) {
        LOGS.logError("Unknown exception caught in cameraAndAimWorker. Retrying...");
    }
}

void MainGame::featuresTaskWorker()
{
    try {

        TaskManager featuresTask;

        //Task List
        
        featuresTask.addTask("exfilTask", std::bind(&Exfil::exfilTask, &exfil), &globals::taskExfil);
        featuresTask.addTask("lootTask", std::bind(&loot::lootTask, &Loot), &globals::taskLoot);
        featuresTask.addTask("questTask", std::bind(&QuestManager::updateAndPruneActiveQuests, &questManager), &globals::taskQuest);
        featuresTask.addTask("wishManagerTask", std::bind(&WishListManager::createWishList, &wishListManager), &globals::taskWishManager);
        featuresTask.addTask("ExplosiveManagerTask", std::bind(&ExplosiveManager::initManager, &explosiveManager), &globals::taskGrenades);
        featuresTask.addTask("PlayerEquipmentTask", std::bind(&Players::playerEquipment, &players), &globals::taskPlayersEquipment);

        //run the tasks
        featuresTask.run();

        //clear cache after ending worker
        exfil.clearCache();
        Loot.clearCache();
        wishListData.clear();
        explosiveManager.reset();
        
    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in featuresTaskWorker: " + std::string(e.what()) + ". Retrying...");

    }
    catch (...) {
        LOGS.logError("Unknown exception caught in featuresTaskWorker. Retrying...");

    }
}

void MainGame::mainThread()
{
    bool doOnce = false;

    while (true)
    {
        const bool initRunning = mem.IsInitRunning();

        const bool dmaConnected = memoryGlobals::dmaConnected.load(
            std::memory_order_acquire
        );

        const bool processFound = memoryGlobals::processFound.load(
            std::memory_order_acquire
        );

        if (initRunning || !dmaConnected || !processFound)
        {
            doOnce = false;

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }

        if (!doOnce)
        {
            //try sig gom check

            const std::string signature = "48 89 05 ?? ?? ?? ?? 48 83 C4 ?? C3 33 C9";

            uint64_t end = mem.base + mem.baseSize;
            uint64_t gomSig = mem.FindSignature(signature.c_str(), mem.base, end);

            if (!Utils::valid_pointer(gomSig))
            {
                LOGS.logInfo("[GOM] gomSig Incorrect 1");
                Sleep(3000);
                continue;
            }

            int32_t rva = mem.Read<int32_t>(gomSig + 3);

            uint64_t gomPtrAddr = gomSig + 7 + rva;

            this->gameObjectManager = mem.Read<uint64_t>(gomPtrAddr);

            //this->gameObjectManager = mem.Read<uint64_t>(mem.base + UnityOffsets::GameObjectManager);

            if (this->gameObjectManager == NULL)
            {
                LOGS.logInfo("GOM Not Located @ 0x", std::hex, this->gameObjectManager);
                Sleep(3000);
                continue;
            }
            else
                doOnce = true;

            LOGS.logInfo("GOM Located @ 0x", std::hex, this->gameObjectManager);
            
        }

        Sleep(4000);
        // wait here to get game world!
        getGameWorldDetails();

        //load required map to memory
        loadMaps(this->selectedLocation.c_str());
        appGlobals::runRadar = true;
        mapGlobals::followLocal = true;

        bool once = false;
        TaskManager manager;
        while (appGlobals::runRadar.load(std::memory_order_acquire))
        {
            if (!once)
            {
                LOGS.logInfo("[MAIN][THREADS] Starting radar");
                once = true;
                appGlobals::runThreads = true;
            }

            std::thread featuresTaskManager(&MainGame::featuresTaskWorker, &mainGame);
            featuresTaskManager.detach();

            std::thread cameraAndAimManager(&MainGame::cameraAndAimWorker, &mainGame);
            cameraAndAimManager.detach();


            LOGS.logInfo("[MAIN][MANAGER] Starting Manager");

            manager.addTask("raidMonitor", std::bind(&MainGame::raidMonitorTask, &mainGame), &globals::taskRaidMonitor);
            manager.addTask("playersTask", std::bind(&Players::playersTask, &players), &globals::taskPlayers);
            manager.addTask("playersBoneTask", std::bind(&Players::boneTask, &players), &globals::taskPlayersBones);
            
            
            manager.run(); // wont return from here till end of raid
            LOGS.logInfo("[MAIN][MANAGER] Stopping Manager");

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        LOGS.logInfo("[MAIN][THREADS] Stopping radar");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        //clear caches
        mainGame.clearCache();
        players.clearCache();
        camera.clearCache();
        exfil.clearCache();
        g_dogTagCache.ClearProcessedCorpses();
        Sleep(3000);

    }
}

void MainGame::keyManagerTask()
{
    try
    {
       

    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in keyManagerTask: " + std::string(e.what()) + ". Retrying...");

    }
    catch (...) {
        LOGS.logError("Unknown exception caught in keyManagerTask. Retrying...");

    }

}

void MainGame::raidMonitorTask()
{
    try
    {
        if (!mem.vHandle)
            return;

        static int zeroCountTicks = 0;
        static int invalidListTicks = 0;
        static bool raidWasSeenActive = false;

        auto TryReadValue = [&](uint64_t address, auto& out) -> bool
            {
                using T = std::decay_t<decltype(out)>;

                out = {};

                if (!Utils::valid_pointer(address))
                    return false;

                try
                {
                    return mem.Read(address, &out, sizeof(T));
                }
                catch (...)
                {
                    return false;
                }
            };

        auto TryReadPtr = [&](uint64_t address, uint64_t& out) -> bool
            {
                out = 0;

                if (!TryReadValue(address, out))
                    return false;

                return Utils::valid_pointer(out);
            };

        if (!Utils::valid_pointer(mainGame.localGameWorld))
        {
            if (raidWasSeenActive)
            {
                invalidListTicks++;

                if (invalidListTicks >= 3)
                {
                    LOGS.logInfo("[RAID] localGameWorld invalid after raid was active. Requesting shutdown.");

                    appGlobals::runThreads.store(false, std::memory_order_release);
                    appGlobals::runRadar.store(false, std::memory_order_release);

                    raidWasSeenActive = false;
                    zeroCountTicks = 0;
                    invalidListTicks = 0;
                }
            }

            return;
        }

        uint64_t registeredPlayers = 0;
        uint64_t registeredPlayersList = 0;
        int registeredPlayersCount = 0;

        if (!TryReadPtr(
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers,
            registeredPlayers))
        {
            if (raidWasSeenActive)
                invalidListTicks++;

            return;
        }

        if (!TryReadPtr(registeredPlayers + 0x10, registeredPlayersList))
        {
            if (raidWasSeenActive)
                invalidListTicks++;

            return;
        }

        if (!TryReadValue(registeredPlayers + 0x18, registeredPlayersCount))
        {
            if (raidWasSeenActive)
                invalidListTicks++;

            return;
        }

        constexpr int MAX_REASONABLE_REGISTERED_PLAYERS = 512;

        if (registeredPlayersCount < 0 || registeredPlayersCount > MAX_REASONABLE_REGISTERED_PLAYERS)
        {
            if (raidWasSeenActive)
                invalidListTicks++;

            return;
        }

        // Raid/player list is healthy again.
        invalidListTicks = 0;

        if (registeredPlayersCount > 0)
        {
            raidWasSeenActive = true;
            zeroCountTicks = 0;
            return;
        }

        // Count is genuinely zero.
        if (raidWasSeenActive && registeredPlayersCount == 0)
        {
            zeroCountTicks++;

            // Debounce it so one bad read/tick does not kill the raid.
            if (zeroCountTicks >= 2)
            {
                LOGS.logInfo("[RAID] RegisteredPlayers count is 0. Requesting shutdown.");

                appGlobals::runThreads.store(false, std::memory_order_release);
                appGlobals::runRadar.store(false, std::memory_order_release);

                raidWasSeenActive = false;
                zeroCountTicks = 0;
                invalidListTicks = 0;
            }
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError("Exception caught in raidMonitorTask: " + std::string(e.what()) + ". Retrying...");
    }
    catch (...)
    {
        LOGS.logError("Unknown exception caught in raidMonitorTask. Retrying...");
    }
}

#pragma pack(push, 1)
struct MonoBehaviour {
    static constexpr std::uint32_t InstanceIDOffset = 0x8;
    static constexpr std::uint32_t ObjectClassOffset = 0x28;
    static constexpr std::uint32_t GameObjectOffset = 0x30;
    static constexpr std::uint32_t EnabledOffset = 0x38;
    static constexpr std::uint32_t IsAddedOffset = 0x39;

    std::int32_t InstanceID;   // m_InstanceID at offset 0x8
    std::uint64_t ObjectClass; // m_Object at offset 0x28
    std::uint64_t GameObject;  // m_GameObject at offset 0x30
    bool Enabled;              // m_Enabled at offset 0x38
    bool IsAdded;              // m_IsAdded at offset 0x39
};
#pragma pack(pop)


