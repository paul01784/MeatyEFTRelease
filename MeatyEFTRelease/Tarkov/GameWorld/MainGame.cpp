#include "../../UI/includes.h"
#include "MainGame.h"


#include "../../Core/Utilities.h"

#include "../../memory/Memory.h"

#include "../../UI/globals.h"
#include "../../UI/maps.h"
#include "../../UI/taskManager.h"



#include <cstdint>
#include <codecvt>
#include <atomic>
#include "../Unity/UnityOffsets.h"
#include "../Unity/GameObjectManager.h"
#include "GameWorld.h"
#include "RegisteredPlayers.h"
#include "../Unity/cameraManager.h"
#include "Exits/Exfil.h"
#include "../Unity/UnityContainers.h"
#include "Loot/Loot.h"
#include "QuestManager.h"
#include "Loot/WishList.h"
#include "Explosives/ExplosiveManager.h"
#include "Player/DogTagCache.h"
#include "Player/PlayerMemoryAccess.h"
#include "../Features/Aim/FireportTracker.h"
#include "../Features/Aim/ReadOnlyAim.h"
#include "../Features/Visibility/AtlasVisibility.h"
#include "Player/WatchList.h"

#include <condition_variable>
#include <mutex>
#include <thread>
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
std::atomic<RegisteredPlayerArraySnapshot> MainGame::registeredPlayerArraySnapshot{ std::make_shared<const RegisteredPlayerArray>() };

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

RegisteredPlayerArraySnapshot MainGame::getRegisteredPlayerArraySnapshot() noexcept
{
    RegisteredPlayerArraySnapshot snapshot = registeredPlayerArraySnapshot.load(std::memory_order_acquire);
    if (snapshot)
        return snapshot;

    static const RegisteredPlayerArraySnapshot empty = std::make_shared<const RegisteredPlayerArray>();
    return empty;
}

void MainGame::clearCache() {

    appGlobals::runThreads.store(false, std::memory_order_release);
    appGlobals::runRadar.store(false, std::memory_order_release);

    questManager.clearForRaidEnd();

    Sleep(500);

    this->gameWorld = NULL;
    this->localGameWorld = NULL;
    this->registeredPlayers = NULL;
    this->registeredPlayersList = NULL;
    this->registeredPlayersCount = 0;
    this->emptyPlayerListConfirmations = 0;
    std::fill(std::begin(this->player_buffer), std::end(this->player_buffer), 0);
    registeredPlayerArraySnapshot.store(std::make_shared<const RegisteredPlayerArray>(), std::memory_order_release);
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

    uint64_t classNamePointer = 0;
    if (!PlayerMemoryAccess::tryReadChain(mainGame.localPlayerHands, { 0x0, 0x10 }, classNamePointer, DmaCacheMode::Uncached))
        return false;

    const std::string className = PlayerMemoryAccess::readUtf8(classNamePointer, 64, DmaCacheMode::Uncached);

    if (IsNullOrWhiteSpace(className))
        return false;

    if (className.find("EmptyHandsController") != std::string::npos)
        return false;

    return true;
}

bool MainGame::updatePlayerList(bool waitForDma)
{
    if (!mem.IsDmaOperational() || !Utils::valid_pointer(mainGame.localGameWorld))
        return false;

    const auto tryReadExact = [waitForDma](const Memory::ScatterReadRequest* requests, std::size_t count, std::string_view label)
        {
            std::vector<DWORD> bytesRead(count, 0);
            const auto result = mem.TryReadScatter(
                requests,
                count,
                DmaCacheMode::Uncached,
                label,
                bytesRead);

            if (result != Memory::TryScatterReadResult::Success)
                return false;

            for (std::size_t index = 0; index < count; ++index)
            {
                if (bytesRead[index] != static_cast<DWORD>(requests[index].size))
                    return false;
            }

            return true;
        };

    uint64_t localPlayer = mainGame.localPlayerPtr;
    uint64_t registeredPlayers = 0;
    const Memory::ScatterReadRequest rootReads[] =
    {
        { mainGame.localGameWorld + sdk::ClientLocalGameWorld::MainPlayer, &localPlayer, sizeof(localPlayer) },
        { mainGame.localGameWorld + sdk::ClientLocalGameWorld::RegisteredPlayers, &registeredPlayers, sizeof(registeredPlayers) }
    };

    if (!tryReadExact(rootReads, std::size(rootReads), "Player roster roots") || !Utils::valid_pointer(registeredPlayers))
        return false;

    uint64_t registeredPlayersList = 0;
    int registeredPlayersCount = 0;
    const Memory::ScatterReadRequest headerReads[] =
    {
        { registeredPlayers + 0x10, &registeredPlayersList, sizeof(registeredPlayersList) },
        { registeredPlayers + 0x18, &registeredPlayersCount, sizeof(registeredPlayersCount) }
    };

    if (!tryReadExact(headerReads, std::size(headerReads), "Player roster header"))
        return false;

    const int capacity = static_cast<int>(std::size(mainGame.player_buffer));
    if (registeredPlayersCount < 0 || registeredPlayersCount > capacity)
        return false;

    if (registeredPlayersCount == 0)
    {
        if (++emptyPlayerListConfirmations < 2)
            return false;

        emptyPlayerListConfirmations = 0;
        mainGame.registeredPlayers = registeredPlayers;
        mainGame.registeredPlayersList = registeredPlayersList;
        mainGame.registeredPlayersCount = 0;
        std::fill(std::begin(mainGame.player_buffer), std::end(mainGame.player_buffer), 0);
        registeredPlayerArraySnapshot.store(std::make_shared<const RegisteredPlayerArray>(), std::memory_order_release);
        return true;
    }

    emptyPlayerListConfirmations = 0;
    if (!Utils::valid_pointer(registeredPlayersList))
        return false;

    std::vector<uint64_t> roster(static_cast<std::size_t>(registeredPlayersCount), 0);
    const Memory::ScatterReadRequest rosterRead{ registeredPlayersList + 0x20, roster.data(), roster.size() * sizeof(uint64_t) };
    if (!tryReadExact(&rosterRead, 1, "Player roster entries"))
        return false;

    std::unordered_set<uint64_t> seen;
    RegisteredPlayerArray validPlayers;
    seen.reserve(roster.size());
    validPlayers.reserve(roster.size());

    for (uint64_t player : roster)
    {
        if (Utils::valid_pointer(player) && seen.insert(player).second)
            validPlayers.emplace_back(player);
    }

    if (validPlayers.empty())
        return false;

    if (Utils::valid_pointer(localPlayer))
        mainGame.localPlayerPtr = localPlayer;

    std::fill(std::begin(mainGame.player_buffer), std::end(mainGame.player_buffer), 0);
    std::copy(validPlayers.begin(), validPlayers.end(), std::begin(mainGame.player_buffer));
    mainGame.registeredPlayers = registeredPlayers;
    mainGame.registeredPlayersList = registeredPlayersList;
    mainGame.registeredPlayersCount = static_cast<int>(validPlayers.size());
    registeredPlayerArraySnapshot.store(std::make_shared<const RegisteredPlayerArray>(validPlayers), std::memory_order_release);
    return true;
}

bool MainGame::getGameWorldDetails(std::stop_token stopToken)
{
    LOGS.logInfo("[GameWorld] Waiting for raid start");

    globals::radarSubText = "Looking for a raid...";

    int pendingAttempts = 0;

    std::uint64_t pending_local_gw = 0;
    std::uint64_t pending_gw_object = 0;
    std::uint64_t observedRecoveryEpoch = mem.GetDmaRecoveryEpoch();

    const auto bssOnlyStartedAt = std::chrono::steady_clock::now();
    auto lastGomFallbackAt = std::chrono::steady_clock::time_point{};
    auto lastTimedRecoveryAt = std::chrono::steady_clock::now();
    auto nextRaidCheckLogAt = bssOnlyStartedAt;

    constexpr int kMaxPendingPromoteAttempts = 6;
    constexpr auto kBssOnlyDuration = std::chrono::seconds(6);
    constexpr auto kGomFallbackInterval = std::chrono::seconds(20);
    constexpr auto kTimedRecoveryInterval = std::chrono::seconds(25);
    constexpr auto kRaidCheckLogInterval = std::chrono::seconds(5);
    constexpr DWORD kRetryDelayMs = 2500;

    while (!stopToken.stop_requested())
    {
        try
        {
            const auto checkStartedAt = std::chrono::steady_clock::now();
            const bool logRaidCheck = false; //checkStartedAt >= nextRaidCheckLogAt;
            if (logRaidCheck)
            {
                nextRaidCheckLogAt = checkStartedAt + kRaidCheckLogInterval;
                LOGS.logInfo("[GameWorld][Readiness] Refreshing raid state");
            }

            const std::uint64_t recoveryEpoch = mem.GetDmaRecoveryEpoch();
            if (recoveryEpoch != observedRecoveryEpoch)
            {
                observedRecoveryEpoch = recoveryEpoch;
                pending_local_gw = 0;
                pending_gw_object = 0;
                pendingAttempts = 0;
            }

            
            mem.RefreshTarkovPointerSnapshot();
            this->gameObjectManager = mem.GetTarkovPointerSnapshot().gameObjectManager;

            if (logRaidCheck)
                LOGS.logInfo("[GameWorld][Readiness] Evaluating raid readiness");

            RaidState raid{};
            std::string probe;
            bool resolved = false;

            if (Utils::valid_pointer(pending_local_gw))
            {
                RaidPendingState stillPending{};
                resolved = tryPromotePendingRaid(this->gameObjectManager, pending_local_gw, pending_gw_object, raid, probe, &stillPending);

                if (!resolved)
                {
                    ++pendingAttempts;

                    if (!stillPending.active || pendingAttempts >= kMaxPendingPromoteAttempts)
                    {
                        pending_local_gw = 0;
                        pending_gw_object = 0;
                        pendingAttempts = 0;
                    }
                }
            }
            

            if (!resolved)
            {
                RaidPendingState pending{};

                const auto lookupNow = std::chrono::steady_clock::now();
                const bool bssOnlyPeriodComplete =
                    (lookupNow - bssOnlyStartedAt) >= kBssOnlyDuration;
                const bool gomRetryDue =
                    lastGomFallbackAt ==
                        std::chrono::steady_clock::time_point{} ||
                    (lookupNow - lastGomFallbackAt) >=
                        kGomFallbackInterval;
                const bool allowGomFallback =
                    bssOnlyPeriodComplete && gomRetryDue;

                if (allowGomFallback)
                    lastGomFallbackAt = lookupNow;

                resolved = tryResolveRaid(this->gameObjectManager, raid, probe, &pending, allowGomFallback);

                if (!resolved && pending.active && Utils::valid_pointer(pending.local_game_world))
                {
                    if (pending_local_gw != pending.local_game_world)
                        pendingAttempts = 0;
                    pending_local_gw = pending.local_game_world;
                    pending_gw_object = pending.game_world_object;
                }
            }

            if (logRaidCheck)
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - checkStartedAt);
                const char* outcome = "still looking for a raid";
                if (resolved)
                    outcome = "raid ready";
                else if (Utils::valid_pointer(pending_local_gw))
                    outcome = "waiting for raid start";

                LOGS.logInfo("[GameWorld][Readiness] Check completed in ", elapsed.count(), " ms (", outcome, ")");
            }

            if (resolved)
            {
                applyRaidStateToMainGame(raid);

                this->gameWorld = raid.game_world_object;
                this->localGameWorld = raid.local_game_world;

                LOGS.logInfo("[GameWorld] Raid ready | ", probe);
                return true;
            }

            globals::radarSubText =
                Utils::valid_pointer(pending_local_gw)
                ? "Raid found. Waiting to start..."
                : "Looking for a raid...";

            mem.RunCacheMaintenance();

            const auto recoveryNow = std::chrono::steady_clock::now();
            const std::uint64_t maintenanceEpoch = mem.GetDmaRecoveryEpoch();
            if (maintenanceEpoch != observedRecoveryEpoch)
            {
                observedRecoveryEpoch = maintenanceEpoch;
                pending_local_gw = 0;
                pending_gw_object = 0;
                pendingAttempts = 0;
                lastTimedRecoveryAt = recoveryNow;
            }
            else if ((recoveryNow - lastTimedRecoveryAt) >= kTimedRecoveryInterval)
            {
                lastTimedRecoveryAt = recoveryNow;
                if (mem.RefreshProcessInformationNow())
                {
                    mem.RefreshTarkovPointerSnapshot();
                    pending_local_gw = 0;
                    pending_gw_object = 0;
                    pendingAttempts = 0;
                    LOGS.logInfo("[GameWorld] Timed DMA recovery completed after 25 seconds without a raid");
                }
                else
                {
                    LOGS.logWarn("[GameWorld] Timed DMA recovery failed; discovery will continue");
                }
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

    LOGS.logInfo("[GameWorld][Readiness] Raid readiness wait ended");
    return false;
}

void MainGame::mainThread(std::stop_token stopToken)
{
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
            if (!WaitForStop(stopToken, std::chrono::milliseconds(250)))
                break;
            continue;
        }

        // wait here to get game world!
        if (!getGameWorldDetails(stopToken))
            break;

        if (mem.RefreshProcessInformationNow())
        {
            mem.RefreshTarkovPointerSnapshot();
            LOGS.logInfo("[MAIN][DMA] Full refresh completed before raid workers started");
        }
        else
        {
            LOGS.logWarn("[MAIN][DMA] Full refresh failed before raid workers started; continuing with uncached reads");
        }

        appGlobals::runRadar = true;
        mapGlobals::followLocal = true;

        LOGS.logInfo("[MAIN][THREADS] Starting radar");
        appGlobals::runThreads.store(true, std::memory_order_release);

        TaskManager cameraWorker;
        TaskManager fastWorker;
        TaskManager liveWorker;
        TaskManager backgroundWorker;
        TaskManager slowPlayerWorker;

        cameraWorker.addTask(
            "cameraTask",
            [firstUpdate = true]() mutable
            {
                try
                {
                    const bool logStartup = firstUpdate;
                    firstUpdate = false;
                    if (logStartup)
                        LOGS.logInfo("[CAMERA MANAGER] First camera update started");

                    const PlayerSnapshot players = ::registeredPlayers.getCacheSnapshot();
                    const auto local = std::find_if(players->begin(), players->end(),
                        [](const Player& player) { return player.isLocal; });
                    const bool updated = cameraManagerTest.updateWithAds(
                        local != players->end() ? local->P_PWA : 0,
                        local != players->end() && local->isAiming,
                        0,
                        local != players->end() ? local->instance : 0);

                    if (logStartup)
                        LOGS.logInfo("[CAMERA MANAGER] First camera update finished (", updated ? "updated" : "no update", ")");
                }
                catch (const std::exception& exception)
                {
                    static auto lastLog =
                        std::chrono::steady_clock::time_point{};
                    const auto now = std::chrono::steady_clock::now();

                    if (lastLog == std::chrono::steady_clock::time_point{} ||
                        (now - lastLog) >= std::chrono::seconds(5))
                    {
                        lastLog = now;
                        LOGS.logWarn(
                            "[CAMERA MANAGER] Update failed: ",
                            exception.what());
                    }
                }
                catch (...)
                {
                    static auto lastLog = std::chrono::steady_clock::time_point{};
                    const auto now = std::chrono::steady_clock::now();

                    if (lastLog == std::chrono::steady_clock::time_point{} ||
                        (now - lastLog) >= std::chrono::seconds(5))
                    {
                        lastLog = now;
                        LOGS.logWarn(
                            "[CAMERA MANAGER] Unknown update failure.");
                    }
                }
            },
            &globals::taskCamera,
            { TaskPriority::Critical, DmaPriority::Critical, false, 0.0 });

        fastWorker.addTask(
            "fireportTask",
            []()
            {
                const bool fireportNeeded = aimGlobals::aimEnabled || espGlobals::drawFireportLine;
                const PlayerLocalStateSnapshot localState = ::registeredPlayers.getLocalStateSnapshot();

                if (fireportNeeded && Utils::valid_pointer(localState->instance))
                    g_fireport.update(localState->instance, localState->handsController);
                else
                    g_fireport.clear();
            },
            &globals::taskFireport,
            { TaskPriority::Critical, DmaPriority::High, false, 0.0 });

        fastWorker.addTask(
            "readOnlyAim",
            std::bind(&ReadOnlyAim::aimTask, &readOnlyAim),
            &globals::taskAim,
            { TaskPriority::Critical, DmaPriority::Critical, false, 0.0 });

        fastWorker.addTask(
            "keyManager",
            std::bind(&MainGame::keyManagerTask, &mainGame),
            &globals::taskKeyManager,
            { TaskPriority::Normal, DmaPriority::Normal, false, 0.0 });

        fastWorker.addTask(
            "raidMonitor",
            std::bind(&MainGame::raidMonitorTask, &mainGame),
            &globals::taskRaidMonitor,
            { TaskPriority::Normal, DmaPriority::Normal, false, 40.0 });

        // Live worker: player roster plus the unified position/bone pipeline.
        liveWorker.addTask(
            "playersTask",
            std::bind(&RegisteredPlayers::playersTask, &::registeredPlayers),
            &globals::taskPlayers,
            { TaskPriority::High, DmaPriority::Normal, false, 0.0 });

        liveWorker.addTask(
            "playerBoneTask",
            std::bind(&RegisteredPlayers::boneTask, &::registeredPlayers),
            &globals::taskPlayerPositions,
            { TaskPriority::Critical, DmaPriority::High, false, 0.0 });

        // Background worker: bounded work that may defer to live DMA.
        backgroundWorker.addTask(
            "MemoryManagerTask",
            std::bind(&Memory::RunCacheMaintenance, &mem),
            &globals::taskMemoryManager,
            { TaskPriority::Background, DmaPriority::Background, true, 75.0 });

        backgroundWorker.addTask(
            "ExplosiveManagerTask",
            std::bind(&ExplosiveManager::initManager, &explosiveManager),
            &globals::taskGrenades,
            { TaskPriority::Background, DmaPriority::Background, true, 100.0 });

        backgroundWorker.addTask(
            "TripwireManagerTask",
            std::bind(&ExplosiveManager::refreshTripwires, &explosiveManager),
            &globals::taskTripWire,
            { TaskPriority::Background, DmaPriority::Background, true, 20.0 });

        backgroundWorker.addTask(
            "exfilTask",
            std::bind(&Exfil::exfilTask, &exfil),
            &globals::taskExfil,
            { TaskPriority::Background, DmaPriority::Background, true, 250.0 });

        backgroundWorker.addTask(
            "lootTask",
            std::bind(&loot::lootTask, &Loot),
            &globals::taskLoot,
            { TaskPriority::Background, DmaPriority::Background, true, 50.0, true });

        backgroundWorker.addTask(
            "corpseLootRefreshTask",
            std::bind(&loot::immediateRefreshTask, &Loot),
            &globals::taskCorpseLootRefresh,
            { TaskPriority::Normal, DmaPriority::Background, false, 0.0 });

        slowPlayerWorker.addTask(
            "PlayerEquipmentTask",
            std::bind(&RegisteredPlayers::playerEquipment, &::registeredPlayers),
            &globals::taskPlayersEquipment,
            { TaskPriority::Background, DmaPriority::Background, true, 150.0 });

        slowPlayerWorker.addTask(
            "PlayerMetadataTask",
            std::bind(&RegisteredPlayers::playerMetadataTask, &::registeredPlayers),
            &globals::taskPlayerMetadata,
            { TaskPriority::Background, DmaPriority::Background, true, 225.0 });

        backgroundWorker.addTask(
            "StaticVisibilityTask",
            std::bind(&AtlasVisibilityService::visibilityTask, &atlasVisibility),
            &globals::taskStaticVisibility,
            { TaskPriority::Background, DmaPriority::Background, false, 300.0 });

        backgroundWorker.addTask(
            "questTask",
            std::bind(&QuestManager::updateAndPruneActiveQuests, &questManager),
            &globals::taskQuest,
            { TaskPriority::Background, DmaPriority::Background, true, 350.0 });

        backgroundWorker.addTask(
            "wishManagerTask",
            std::bind(&WishListManager::createWishList, &wishListManager),
            &globals::taskWishManager,
            { TaskPriority::Background, DmaPriority::Background, false, 450.0 });

        LOGS.logInfo("[MAIN][MANAGER] Starting Camera, Fast, Live, Background and Slow Player workers");
        LOGS.logInfo("[MAIN][RaidLog] Sent Raid Log Start");
        watchListManager.logRaidStart(this->selectedLocation.c_str());

        std::jthread cameraWorkerThread(
            [&](std::stop_token workerStop)
            {
                SetThreadPriority(
                    GetCurrentThread(),
                    THREAD_PRIORITY_ABOVE_NORMAL);
                cameraWorker.run(workerStop);
            });

        std::jthread fastWorkerThread(
            [&](std::stop_token workerStop)
            {
                SetThreadPriority(
                    GetCurrentThread(),
                    THREAD_PRIORITY_ABOVE_NORMAL);
                fastWorker.run(workerStop);
            });

        std::jthread liveWorkerThread(
            [&](std::stop_token workerStop)
            {
                liveWorker.run(workerStop);
            });

        std::jthread backgroundWorkerThread(
            [&](std::stop_token workerStop)
            {
                backgroundWorker.run(workerStop);
            });

        std::jthread slowPlayerWorkerThread(
            [&](std::stop_token workerStop)
            {
                SetThreadPriority(
                    GetCurrentThread(),
                    THREAD_PRIORITY_BELOW_NORMAL);
                slowPlayerWorker.run(workerStop);
            });

        LOGS.logNotice(NoticeColour::GREEN, "Raid started, loading radar");

        while (!stopToken.stop_requested() &&
            appGlobals::runThreads.load(std::memory_order_acquire))
        {
            if (!WaitForStop(stopToken, std::chrono::milliseconds(10)))
                break;
        }

        LOGS.logNotice(NoticeColour::GREEN, "Raid ended, stopping and clearing caches");
        appGlobals::runThreads.store(false, std::memory_order_release);

        cameraWorkerThread.request_stop();
        fastWorkerThread.request_stop();
        liveWorkerThread.request_stop();
        backgroundWorkerThread.request_stop();
        slowPlayerWorkerThread.request_stop();

        cameraWorkerThread.join();
        fastWorkerThread.join();
        liveWorkerThread.join();
        backgroundWorkerThread.join();
        slowPlayerWorkerThread.join();
        cameraManagerTest.reset();

        exfil.clearCache();
        Loot.clearCache();
        wishListData.clear();
        explosiveManager.reset();

        LOGS.logInfo("[MAIN][MANAGER] Camera, Fast, Live, Background and Slow Player workers stopped");
        watchListManager.logRaidEnd();
        LOGS.logInfo("[MAIN][RaidLog] Sent Raid Log End");

        LOGS.logInfo("[MAIN][THREADS] Stopping radar");

        if (stopToken.stop_requested())
            break;

        //clear caches
        mainGame.clearCache();
        ::registeredPlayers.clearCache();
        exfil.clearCache();
        g_dogTagCache.ClearProcessedCorpses();

        const bool raidEndRefreshSucceeded = mem.RefreshProcessInformationNow();
        mem.RefreshTarkovPointerSnapshot();
        if (raidEndRefreshSucceeded)
            LOGS.logInfo("[MAIN][DMA] Full memory, TLB and process refresh completed after raid cleanup");
        else
            LOGS.logWarn("[MAIN][DMA] Post-raid full refresh failed; timed recovery remains active");

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
    static bool previousRadarMinViewKeyState = false;

    try
    {
        if (!mem.IsDmaOperational())
        {
            previousFollowKeyState = false;
            previousBattleModeKeyState = false;
            previousRadarMinViewKeyState = false;
            return;
        }

        auto* keyboard = mem.GetKeyboard();

        if (!keyboard)
        {
            previousFollowKeyState = false;
            previousBattleModeKeyState = false;
            previousRadarMinViewKeyState = false;
            return;
        }

        const bool followKeyIsHeld = keyboard->IsKeyDown(static_cast<int>(keyGlobals::toggleFollow));
        const bool battleModeKeyIsHeld = keyboard->IsKeyDown(static_cast<int>(keyGlobals::battleMode));
        const bool radarMinViewKeyIsHeld = keyboard->IsKeyDown(static_cast<int>(keyGlobals::toggleRadarMinView));

        //the follow key has just been pressed
        const bool followKeyPressed = followKeyIsHeld && !previousFollowKeyState;

        //the battle-mode key has just been pressed
        const bool battleModeKeyPressed = battleModeKeyIsHeld && !previousBattleModeKeyState;
        const bool radarMinViewKeyPressed = radarMinViewKeyIsHeld && !previousRadarMinViewKeyState;

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

        if (radarMinViewKeyPressed)
        {
            radarGlobals::minimalView = !radarGlobals::minimalView;

            LOGS.logInfo(
                std::string("[KEYS] Radar min view ") +
                (radarGlobals::minimalView
                    ? "enabled"
                    : "disabled")
            );
        }

        previousFollowKeyState = followKeyIsHeld;
        previousBattleModeKeyState = battleModeKeyIsHeld;
        previousRadarMinViewKeyState = radarMinViewKeyIsHeld;
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
        previousRadarMinViewKeyState = false;
    }
    catch (...)
    {
        LOGS.logError(
            "Unknown exception caught in keyManagerTask. Retrying..."
        );

        previousFollowKeyState = false;
        previousBattleModeKeyState = false;
        previousRadarMinViewKeyState = false;
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
                const PlayerSnapshot snapshotHandle =
                    ::registeredPlayers.getCacheSnapshot();
                const PlayerCollection& snapshot = *snapshotHandle;

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

        auto logIgnoredShutdown = [&](const char* condition, const char* metric, int observed, int cachedActive)
            {
                const auto now = std::chrono::steady_clock::now();

                if (lastIgnoredZeroLog != std::chrono::steady_clock::time_point{} &&
                    (now - lastIgnoredZeroLog) < std::chrono::seconds(10))
                {
                    return;
                }

                lastIgnoredZeroLog = now;

                LOGS.logWarn(
                    std::string("[RAID] Keeping raid active after a transient roster issue (") +
                    "condition=" + condition +
                    ", " + metric + "=" + std::to_string(observed) +
                    ", cachedActivePlayers=" + std::to_string(cachedActive) +
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
                    logIgnoredShutdown(
                        reason,
                        "consecutiveInvalidChecks",
                        invalidListTicks,
                        cachedActivePlayers
                    );
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
                "observedCount",
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


