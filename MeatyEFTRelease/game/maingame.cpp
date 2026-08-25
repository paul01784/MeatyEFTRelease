#include "../app/includes.h"
#include "headers/maingame.h"


#include "headers/utils.h"

#include "../memory/Memory.h"

#include "../app/globals.h"
#include "../app/maps.h"
#include "../app/taskManager.h"



#include <cstdint>
#include <codecvt>
#include <atomic>
#include "headers/unitysdk.h"
#include "unity/gameObjectManager.h"
#include "headers/game_world.h"
#include "headers/players.h"
#include "headers/camera.h"
#include "headers/exfil.h"
#include "headers/unityHelper.h"
#include "headers/loot.h"
#include "headers/questManager.h"
#include "headers/wishlist.h"
#include "headers/explosives.h"
#include "headers/dogtag.h"
#include "headers/fireport.h"
#include "headers/readOnlyAim.h"
#include "headers/watchList.h"
#include "../app/makcu.h"

#include <condition_variable>
#include <mutex>
#include <unordered_set>

namespace
{
    template <typename Rep, typename Period>
    bool WaitForStop(
        std::stop_token stopToken,
        std::chrono::duration<Rep, Period> duration)
    {
        std::condition_variable_any wakeCondition;
        std::mutex wakeMutex;
        std::unique_lock wakeLock(wakeMutex);

        wakeCondition.wait_for(
            wakeLock,
            stopToken,
            duration,
            []
            {
                return false;
            });

        return !stopToken.stop_requested();
    }
}



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
    this->localPlayerPWA = NULL;
    this->localplayerProfile = NULL;
    this->localLocation = {};
    this->localGroupId = "";
    this->localIsScoped = false;
    this->localRotation = {};
    this->localIsSavage = false;

    this->pmcNumber = 1;

    this->btrAllocated = false;

    this->active_objects.fill(0);
    this->tagged_objects.fill(0);

    Sleep(500);

    LOGS.logInfo("[MAIN][CACHE] Data cleared");
}

void MainGame::SetBattleMode(bool enabled)
{
    // Do nothing if the requested state is already active.
    if (battleModeEnabled == enabled)
        return;

    if (enabled)
    {
        // Save the user's current settings before disabling them.
        battleModeSavedState.radarDrawLoot = radarGlobals::drawLoot;
        battleModeSavedState.radarDrawQuestHelper = radarGlobals::drawQuestHelper;
        battleModeSavedState.radarDrawGrenades = radarGlobals::drawGrenades;
        battleModeSavedState.radarDrawTripwires = radarGlobals::drawTripwires;
        battleModeSavedState.radarDrawExfils = radarGlobals::drawExfils;
        battleModeSavedState.espDrawLoot = espGlobals::drawLoot;
        battleModeSavedState.espDrawCorpse = espGlobals::drawCorpse;
        battleModeSavedState.espDrawQuestHelper = espGlobals::drawQuestHelper;
        battleModeSavedState.espDrawTripwires = espGlobals::drawTripwires;
        battleModeSavedState.espDrawExfil = espGlobals::drawExfil;
        battleModeSavedState.valid = true;

        // Disable everything hidden by battle mode.
        radarGlobals::drawLoot = false;
        radarGlobals::drawQuestHelper = false;
        radarGlobals::drawGrenades = false;
        radarGlobals::drawTripwires = false;
        radarGlobals::drawExfils = false;

        espGlobals::drawLoot = false;
        espGlobals::drawCorpse = false;
        espGlobals::drawQuestHelper = false;
        espGlobals::drawTripwires = false;
        espGlobals::drawExfil = false;

        battleModeEnabled = true;

        LOGS.logInfo("[KEYS] Battle mode enabled");
    }
    else
    {
        // Restore the exact settings that were active before battle mode
        if (battleModeSavedState.valid)
        {
            radarGlobals::drawLoot = battleModeSavedState.radarDrawLoot;
            radarGlobals::drawQuestHelper = battleModeSavedState.radarDrawQuestHelper;
            radarGlobals::drawGrenades = battleModeSavedState.radarDrawGrenades;
            radarGlobals::drawTripwires = battleModeSavedState.radarDrawTripwires;
            radarGlobals::drawExfils = battleModeSavedState.radarDrawExfils;
            espGlobals::drawLoot = battleModeSavedState.espDrawLoot;
            espGlobals::drawCorpse = battleModeSavedState.espDrawCorpse;
            espGlobals::drawQuestHelper = battleModeSavedState.espDrawQuestHelper;
            espGlobals::drawTripwires = battleModeSavedState.espDrawTripwires;
            espGlobals::drawExfil = battleModeSavedState.espDrawExfil;
        }

        battleModeSavedState.valid = false;
        battleModeEnabled = false;

        LOGS.logInfo("[KEYS] Battle mode disabled");
    }
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
    const uint64_t ptr = mem.Read<uint64_t>(
        mainGame.localGameWorld + sdk::ClientLocalGameWorld::MainPlayer, DmaCacheMode::Uncached
    );

    if (Utils::valid_pointer(ptr))
        mainGame.localPlayerPtr = ptr;
}

bool MainGame::updatePlayerList()
{
    if (!mem.vHandle)
        return false;

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return false;

    const auto commitEmptyPlayerList = [&]()
        {
            mainGame.registeredPlayers = 0;
            mainGame.registeredPlayersList = 0;
            mainGame.registeredPlayersCount = 0;

            std::fill(
                std::begin(mainGame.player_buffer),
                std::end(mainGame.player_buffer),
                0
            );
        };

    auto TryReadValue = [&](uint64_t address, auto& out) -> bool
        {
            using T = std::decay_t<decltype(out)>;

            out = {};

            if (!Utils::valid_pointer(address))
                return false;

            try
            {
                return mem.Read(address, &out, sizeof(T), DmaCacheMode::Uncached);
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
                return mem.Read(address, out, size, DmaCacheMode::Uncached);
            }
            catch (...)
            {
                return false;
            }
        };

    // Both of these fields belong to LocalGameWorld, so fetch them together.
    // Keeping them as separate direct reads turned an otherwise small roster
    // refresh into two independent DMA round trips every player tick.
    uint64_t latestLocalPlayer = mainGame.localPlayerPtr;
    uint64_t registeredPlayers = 0;
    uint64_t registeredPlayersList = 0;
    int registeredPlayersCount = 0;

    const Memory::ScatterReadRequest rootRequests[] =
    {
        {
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::MainPlayer,
            &latestLocalPlayer,
            sizeof(latestLocalPlayer)
        },
        {
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers,
            &registeredPlayers,
            sizeof(registeredPlayers)
        }
    };

    if (!mem.ReadScatter(
        rootRequests,
        std::size(rootRequests),
        DmaCacheMode::Uncached,
        "Player roster roots"))
    {
        return false;
    }

    if (Utils::valid_pointer(latestLocalPlayer))
        mainGame.localPlayerPtr = latestLocalPlayer;

    if (!Utils::valid_pointer(registeredPlayers))
        return false;

    const Memory::ScatterReadRequest headerRequests[] =
    {
        {
            registeredPlayers + 0x10,
            &registeredPlayersList,
            sizeof(registeredPlayersList)
        },
        {
            registeredPlayers + 0x18,
            &registeredPlayersCount,
            sizeof(registeredPlayersCount)
        }
    };

    if (!mem.ReadScatter(headerRequests, std::size(headerRequests), DmaCacheMode::Uncached, "Player roster header"))
    {
        return false;
    }

    const int bufferCapacity =
        static_cast<int>(std::size(mainGame.player_buffer));

    if (registeredPlayersCount < 0 || registeredPlayersCount > bufferCapacity)
    {
        LOGS.logError(
            "[PLAYERS][LIST] registeredPlayersCount invalid: " +
            std::to_string(registeredPlayersCount)
        );

        return false;
    }

    // This is a valid read of a genuinely empty list
    // Do not retain old raid/player data
    if (registeredPlayersCount == 0)
    {
        commitEmptyPlayerList();
        return true;
    }

    if (!Utils::valid_pointer(registeredPlayersList))
        return false;

    std::vector<uint64_t> tempBuffer(
        static_cast<size_t>(registeredPlayersCount),
        0
    );

    const uint64_t playerArrayStart = registeredPlayersList + 0x20;

    if (!TryReadBuffer(playerArrayStart, tempBuffer.data(), sizeof(uint64_t) * tempBuffer.size()))
    {
        return false;
    }

    uint64_t verifiedRegisteredPlayers = 0;
    uint64_t verifiedRegisteredPlayersList = 0;
    int verifiedRegisteredPlayersCount = 0;

    const Memory::ScatterReadRequest verificationRequests[] =
    {
        {
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers,
            &verifiedRegisteredPlayers,
            sizeof(verifiedRegisteredPlayers)
        },
        {
            registeredPlayers + 0x10,
            &verifiedRegisteredPlayersList,
            sizeof(verifiedRegisteredPlayersList)
        },
        {
            registeredPlayers + 0x18,
            &verifiedRegisteredPlayersCount,
            sizeof(verifiedRegisteredPlayersCount)
        }
    };

    if (!mem.ReadScatter(verificationRequests, std::size(verificationRequests), DmaCacheMode::Uncached, "Player roster verify"))
    {
        return false;
    }

    if (verifiedRegisteredPlayers != registeredPlayers ||
        verifiedRegisteredPlayersList != registeredPlayersList ||
        verifiedRegisteredPlayersCount != registeredPlayersCount)
    {
        return false;
    }

    std::unordered_set<uint64_t> uniquePlayers;
    uniquePlayers.reserve(tempBuffer.size());

    for (const uint64_t player : tempBuffer)
    {
        if (!Utils::valid_pointer(player) || !uniquePlayers.insert(player).second)
        {
            return false;
        }
    }

    std::copy(
        tempBuffer.begin(),
        tempBuffer.end(),
        std::begin(mainGame.player_buffer));

    // Clear old entries left after the new shorter list
    for (int i = registeredPlayersCount; i < bufferCapacity; ++i)
        mainGame.player_buffer[i] = 0;

    mainGame.registeredPlayers = registeredPlayers;
    mainGame.registeredPlayersList = registeredPlayersList;
    mainGame.registeredPlayersCount = registeredPlayersCount;

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

            if (validCount >= 1)
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

bool MainGame::getGameWorldDetails(std::stop_token stopToken)
{
    LOGS.logInfo("[GameWorld] Waiting for raid start");

    globals::radarSubText = "Trying to resolve game world details";

    int waitAttempts = 0;
    int pendingAttempts = 0;

    std::uint64_t pending_local_gw = 0;
    std::uint64_t pending_gw_object = 0;

    constexpr int kLogEveryAttempts = 4;
    constexpr int kMaxPendingPromoteAttempts = 6;
    constexpr int kRefreshEveryAttempts = 2;
    constexpr DWORD kRetryDelayMs = 2500;

    while (!stopToken.stop_requested())
    {
        try
        {
            ++waitAttempts;

            const bool refreshBeforeLookup = waitAttempts == 1 || ((waitAttempts - 1) % kRefreshEveryAttempts) == 0;

            if (refreshBeforeLookup)
            {
                const ScopedDmaPriority refreshPriority(DmaPriority::High);

                if (!mem.RefreshProcessInformationNow())
                {
                    LOGS.logWarn(
                        "[GameWorld] DMA process/TLB refresh failed; "
                        "continuing with uncached lookup"
                    );
                }
            }

            RaidState raid{};
            std::string probe;
            bool resolved = false;

            if (Utils::valid_pointer(pending_local_gw))
            {
                resolved = tryPromotePendingRaid(this->gameObjectManager, pending_local_gw, pending_gw_object, raid, probe);

                if (!resolved)
                {
                    ++pendingAttempts;

                    if (pendingAttempts >= kMaxPendingPromoteAttempts)
                    {
                        LOGS.logInfo(
                            "[GameWorld] Pending object did not become ready; "
                            "rescanning the active object list | Last state: ",
                            probe.empty() ? "raid data unavailable" : probe
                        );

                        pending_local_gw = 0;
                        pending_gw_object = 0;
                        pendingAttempts = 0;
                    }
                }
            }
            else
            {
                RaidPendingState pending{};

                resolved = tryResolveRaid(this->gameObjectManager, raid, probe, &pending);

                if (!resolved &&
                    pending.active &&
                    Utils::valid_pointer(pending.local_game_world))
                {
                    pending_local_gw = pending.local_game_world;
                    pending_gw_object = pending.game_world_object;
                    pendingAttempts = 0;

                    LOGS.logInfo(
                        "[GameWorld] Object found; waiting for raid data | ",
                        probe.empty() ? "raid data unavailable" : probe
                    );
                }
            }

            if (resolved)
            {
                applyRaidStateToMainGame(raid);

                this->gameWorld = raid.game_world_object;
                this->localGameWorld = raid.local_game_world;

                LOGS.logInfo("[GameWorld] ", probe);
                return true;
            }

            if ((waitAttempts % kLogEveryAttempts) == 0)
            {
                LOGS.logInfo(
                    "[GameWorld] Still waiting | ",
                    probe.empty() ? "raid not ready" : probe
                );
            }
        }
        catch (const std::exception& e)
        {
            LOGS.logError(
                "[GameWorld] Exception while resolving raid: " +
                std::string(e.what())
            );

            pending_local_gw = 0;
            pending_gw_object = 0;
            pendingAttempts = 0;
        }
        catch (...)
        {
            LOGS.logError(
                "[GameWorld] Unknown exception while resolving raid"
            );

            pending_local_gw = 0;
            pending_gw_object = 0;
            pendingAttempts = 0;
        }

        if (!WaitForStop(
            stopToken,
            std::chrono::milliseconds(kRetryDelayMs)))
        {
            break;
        }
    }

    return false;
}

void MainGame::mainThread(std::stop_token stopToken)
{
    bool doOnce = false;

    while (!stopToken.stop_requested())
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

            if (!WaitForStop(stopToken, std::chrono::milliseconds(250)))
                break;
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
                LOGS.logInfo(
                    "[GOM] Signature scan did not find the "
                    "GameObjectManager; retrying"
                );
                if (!WaitForStop(stopToken, std::chrono::milliseconds(3000)))
                    break;
                continue;
            }

            int32_t rva = mem.Read<int32_t>(gomSig + 3);

            uint64_t gomPtrAddr = gomSig + 7 + rva;

            this->gameObjectManager = mem.Read<uint64_t>(gomPtrAddr);

            //this->gameObjectManager = mem.Read<uint64_t>(mem.base + UnityOffsets::GameObjectManager);

            if (this->gameObjectManager == NULL)
            {
                LOGS.logInfo(
                    "[GOM] GameObjectManager pointer is unavailable; retrying"
                );
                if (!WaitForStop(stopToken, std::chrono::milliseconds(3000)))
                    break;
                continue;
            }
            else
                doOnce = true;

            LOGS.logInfo("[GOM] GameObjectManager resolved");

            
            
        }

        if (!WaitForStop(stopToken, std::chrono::milliseconds(4000)))
            break;

        // wait here to get game world!
        if (!getGameWorldDetails(stopToken))
            break;

        appGlobals::runRadar = true;
        mapGlobals::followLocal = true;

        LOGS.logInfo("[MAIN][THREADS] Starting radar");
        appGlobals::runThreads.store(true, std::memory_order_release);

        TaskManager manager;

        // One scheduler
        manager.addTask(
            "cameraTask",
            std::bind(&Camera::cameraTask, &camera),
            &globals::taskCamera,
            { TaskPriority::Critical, DmaPriority::Critical, false, 0.0 });

        manager.addTask(
            "playerPositionTask",
            std::bind(&Players::positionTask, &players),
            &globals::taskPlayerPositions,
            { TaskPriority::Critical, DmaPriority::High, false, 0.0 });

        manager.addTask(
            "readOnlyAim",
            std::bind(&ReadOnlyAim::aimTask, &readOnlyAim),
            &globals::taskAim,
            { TaskPriority::Critical, DmaPriority::Critical, false, 0.0 });

        manager.addTask(
            "fireportTask",
            []()
            {
                const bool fireportNeeded = makcu.IsConnected() && aimGlobals::aimEnabled && (aimGlobals::drawFireportLine || aimGlobals::aimReference == AimReference::Fireport);

                if (fireportNeeded)
                    g_fireport.update(mainGame.localPlayerPtr);
                else
                    g_fireport.clear();
            },
            &globals::taskFireport,
            { TaskPriority::High, DmaPriority::High, false, 4.0 });

        manager.addTask(
            "playersTask",
            std::bind(&Players::playersTask, &players),
            &globals::taskPlayers,
            { TaskPriority::High, DmaPriority::Normal, false, 0.0 });

        manager.addTask(
            "playersBoneTask",
            std::bind(&Players::boneTask, &players),
            &globals::taskPlayersBones,
            { TaskPriority::High, DmaPriority::Normal, false, 10.0 });

        manager.addTask(
            "ExplosiveManagerTask",
            std::bind(&ExplosiveManager::initManager, &explosiveManager),
            &globals::taskGrenades,
            { TaskPriority::High, DmaPriority::High, false, 100.0 });

        manager.addTask(
            "TripwireManagerTask",
            std::bind(&ExplosiveManager::refreshTripwires, &explosiveManager),
            &globals::taskTripWire,
            { TaskPriority::High, DmaPriority::High, false, 20.0 });

        manager.addTask(
            "raidMonitor",
            std::bind(&MainGame::raidMonitorTask, &mainGame),
            &globals::taskRaidMonitor,
            { TaskPriority::Normal, DmaPriority::Normal, false, 40.0 });

        manager.addTask(
            "keyManager",
            std::bind(&MainGame::keyManagerTask, &mainGame),
            &globals::taskKeyManager,
            { TaskPriority::Normal, DmaPriority::Normal, false, 0.0 });

        manager.addTask(
            "exfilTask",
            std::bind(&Exfil::exfilTask, &exfil),
            &globals::taskExfil,
            { TaskPriority::Normal, DmaPriority::Normal, false, 250.0 });

        manager.addTask(
            "lootTask",
            std::bind(&loot::lootTask, &Loot),
            &globals::taskLoot,
            { TaskPriority::Background, DmaPriority::Background, true, 50.0 });

        manager.addTask(
            "PlayerEquipmentTask",
            std::bind(&Players::playerEquipment, &players),
            &globals::taskPlayersEquipment,
            { TaskPriority::Background, DmaPriority::Background, true, 150.0 });

        manager.addTask(
            "PlayerMetadataTask",
            std::bind(&Players::playerMetadataTask, &players),
            &globals::taskPlayerMetadata,
            { TaskPriority::Background, DmaPriority::Background, true, 225.0 });

        manager.addTask(
            "questTask",
            std::bind(&QuestManager::updateAndPruneActiveQuests, &questManager),
            &globals::taskQuest,
            { TaskPriority::Background, DmaPriority::Background, true, 350.0 });

        manager.addTask(
            "wishManagerTask",
            std::bind(&WishListManager::createWishList, &wishListManager),
            &globals::taskWishManager,
            { TaskPriority::Background, DmaPriority::Background, false, 450.0 });

        LOGS.logInfo("[MAIN][MANAGER] Starting Manager");
        LOGS.logInfo("[MAIN][RaidLog] Sent Raid Log Start");
        watchListManager.logRaidStart(this->selectedLocation.c_str());

        manager.run(stopToken);

        appGlobals::runThreads.store(false, std::memory_order_release);

        exfil.clearCache();
        Loot.clearCache();
        wishListData.clear();
        explosiveManager.reset();

        LOGS.logInfo("[MAIN][MANAGER] Stopping Manager");
        watchListManager.logRaidEnd();
        LOGS.logInfo("[MAIN][RaidLog] Sent Raid Log End");

        LOGS.logInfo("[MAIN][THREADS] Stopping radar");

        if (stopToken.stop_requested())
            break;

        //clear caches
        mainGame.clearCache();
        players.clearCache();
        camera.clearCache();
        exfil.clearCache();
        g_dogTagCache.ClearProcessedCorpses();
        if (!WaitForStop(stopToken, std::chrono::milliseconds(3000)))
            break;

    }

    appGlobals::runThreads.store(false, std::memory_order_release);
    appGlobals::runRadar.store(false, std::memory_order_release);
}

void MainGame::keyManagerTask()
{
    static bool previousFollowKeyState = false;
    static bool previousBattleModeKeyState = false;

    try
    {
        if (!mem.IsDmaOperational())
        {
            previousFollowKeyState = false;
            previousBattleModeKeyState = false;
            return;
        }

        auto* keyboard = mem.GetKeyboard();

        if (!keyboard)
        {
            previousFollowKeyState = false;
            previousBattleModeKeyState = false;
            return;
        }

        const bool followKeyIsHeld = keyboard->IsKeyDown(static_cast<int>(keyGlobals::toggleFollow));
        const bool battleModeKeyIsHeld = keyboard->IsKeyDown(static_cast<int>(keyGlobals::battleMode));

        //the follow key has just been pressed
        const bool followKeyPressed = followKeyIsHeld && !previousFollowKeyState;

        //the battle-mode key has just been pressed
        const bool battleModeKeyPressed = battleModeKeyIsHeld && !previousBattleModeKeyState;

        if (followKeyPressed)
        {
            mapGlobals::followLocal = !mapGlobals::followLocal;

            LOGS.logInfo(
                std::string("[KEYS] Map follow ") +
                (mapGlobals::followLocal
                    ? "enabled"
                    : "disabled")
            );
        }

        if (battleModeKeyPressed)
        {
            SetBattleMode(!battleModeEnabled);
        }

        previousFollowKeyState = followKeyIsHeld;
        previousBattleModeKeyState = battleModeKeyIsHeld;
    }
    catch (const std::exception& e)
    {
        LOGS.logError(
            "Exception caught in keyManagerTask: " +
            std::string(e.what()) +
            ". Retrying..."
        );

        previousFollowKeyState = false;
        previousBattleModeKeyState = false;
    }
    catch (...)
    {
        LOGS.logError(
            "Unknown exception caught in keyManagerTask. Retrying..."
        );

        previousFollowKeyState = false;
        previousBattleModeKeyState = false;
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
        static auto lastIgnoredZeroLog = std::chrono::steady_clock::time_point{};

        constexpr int kZeroCountShutdownTicks = 12;
        constexpr int kInvalidGwShutdownTicks = 10;
        constexpr int kMaxReasonableRegisteredPlayers = 512;

        auto countActiveCachedPlayers = []() -> int
            {
                const PlayerCacheSnapshot snapshotHandle =
                    players.getCacheSnapshot();
                const PlayerCacheCollection& snapshot = *snapshotHandle;

                int active = 0;

                for (const auto& player : snapshot)
                {
                    if (!Utils::valid_pointer(player.instance))
                        continue;

                    if (player.isDead || player.hasExfiled || player.isBTR)
                        continue;

                    ++active;
                }

                return active;
            };

        auto logIgnoredShutdown = [&](const char* reason, int observed, int cachedActive)
            {
                const auto now = std::chrono::steady_clock::now();

                if (lastIgnoredZeroLog != std::chrono::steady_clock::time_point{} &&
                    (now - lastIgnoredZeroLog) < std::chrono::seconds(10))
                {
                    return;
                }

                lastIgnoredZeroLog = now;

                LOGS.logWarn(
                    std::string("[RAID] Delaying shutdown (") +
                    reason +
                    "=" + std::to_string(observed) +
                    ", cachedActive=" + std::to_string(cachedActive) +
                    ")"
                );
            };

        auto requestShutdown = [&](const std::string& reason)
            {
                LOGS.logInfo(reason);

                appGlobals::runThreads.store(false, std::memory_order_release);
                appGlobals::runRadar.store(false, std::memory_order_release);

                raidWasSeenActive = false;
                zeroCountTicks = 0;
                invalidListTicks = 0;
            };

        auto markInvalidListState = [&](const char* reason)
            {
                
                zeroCountTicks = 0;

                if (!raidWasSeenActive)
                {
                    invalidListTicks = 0;
                    return;
                }

                ++invalidListTicks;

                const int cachedActivePlayers = countActiveCachedPlayers();

                if (cachedActivePlayers >= 2 && invalidListTicks <= 3)
                {
                    logIgnoredShutdown(reason, invalidListTicks, cachedActivePlayers);
                }

                if (invalidListTicks >= kInvalidGwShutdownTicks)
                {
                    requestShutdown(
                        std::string("[RAID] ") +
                        reason +
                        " remained invalid for " +
                        std::to_string(invalidListTicks) +
                        " consecutive checks. Requesting shutdown."
                    );
                }
            };

        auto TryReadValue = [&](uint64_t address, auto& out) -> bool
            {
                using T = std::decay_t<decltype(out)>;

                out = {};

                if (!Utils::valid_pointer(address))
                    return false;

                try
                {
                    return mem.Read(address, &out, sizeof(T), DmaCacheMode::Uncached);
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
            markInvalidListState("localGameWorld");
            return;
        }

        uint64_t registeredPlayers = 0;
        uint64_t registeredPlayersList = 0;
        int registeredPlayersCount = 0;

        if (!TryReadPtr(
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers,
            registeredPlayers))
        {
            markInvalidListState("registeredPlayersPtr");
            return;
        }

        if (!TryReadPtr(registeredPlayers + 0x10, registeredPlayersList))
        {
            markInvalidListState("registeredPlayersList");
            return;
        }

        if (!TryReadValue(registeredPlayers + 0x18, registeredPlayersCount))
        {
            markInvalidListState("registeredPlayersCountRead");
            return;
        }

        if (registeredPlayersCount < 0 ||
            registeredPlayersCount > kMaxReasonableRegisteredPlayers)
        {
            markInvalidListState("registeredPlayersCountInvalid");
            return;
        }

        invalidListTicks = 0;

        if (registeredPlayersCount > 0)
        {
            raidWasSeenActive = true;
            zeroCountTicks = 0;
            return;
        }

        if (!raidWasSeenActive)
        {
            zeroCountTicks = 0;
            return;
        }

        const int cachedActivePlayers = countActiveCachedPlayers();

        if (cachedActivePlayers >= 2 && zeroCountTicks < 3)
        {
            logIgnoredShutdown(
                "registeredPlayersCount",
                registeredPlayersCount,
                cachedActivePlayers
            );
        }

        ++zeroCountTicks;

        if (zeroCountTicks >= kZeroCountShutdownTicks)
        {
            requestShutdown(
                "[RAID] RegisteredPlayers count remained 0 for " +
                std::to_string(zeroCountTicks) +
                " consecutive valid reads. Requesting shutdown."
            );
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError(
            "Exception caught in raidMonitorTask: " +
            std::string(e.what()) +
            ". Retrying..."
        );
    }
    catch (...)
    {
        LOGS.logError(
            "Unknown exception caught in raidMonitorTask. Retrying..."
        );
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


