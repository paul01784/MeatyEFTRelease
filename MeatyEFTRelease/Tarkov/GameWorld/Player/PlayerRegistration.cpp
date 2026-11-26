#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerClassifier.h"
#include "PlayerMemoryAccess.h"

#include "../../../Core/Utilities.h"
#include "../../../memory/Memory.h"
#include "../../../UI/debug.h"
#include "../MainGame.h"
#include "../../Unity/UnityContainers.h"
#include "WatchList.h"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace
{
    constexpr std::chrono::milliseconds allocationRetryCooldown{ 500 };
}

void RegisteredPlayers::playersTask()
{
    try
    {
        if (softRestartRequested.exchange(false, std::memory_order_acq_rel))
        {
            std::lock_guard<std::mutex> lock(playerMutex);
            mainGame.localGroupId.clear();
            resetCacheLocked();
            publishCacheSnapshotLocked();
            LOGS.logInfo("[PLAYER][CACHE] Soft restart applied on live worker");
        }

        applyPendingPlayerEdits();

        if (!mem.IsDmaOperational())
            return;

        const auto rosterNow = std::chrono::steady_clock::now();
        constexpr auto kMaximumRosterRefreshGap = std::chrono::milliseconds(75);
        if (nextGuaranteedRosterRefresh == std::chrono::steady_clock::time_point{})
            nextGuaranteedRosterRefresh = rosterNow + kMaximumRosterRefreshGap;

        const bool guaranteeFreshRoster = rosterNow >= nextGuaranteedRosterRefresh;
        bool rosterUpdated = false;
        if (guaranteeFreshRoster)
        {
            const ScopedDmaPriority priority(DmaPriority::High);
            rosterUpdated = mainGame.updatePlayerList(true);
            nextGuaranteedRosterRefresh = rosterNow + kMaximumRosterRefreshGap;
        }
        else
        {
            rosterUpdated = mainGame.updatePlayerList(false);
            if (rosterUpdated)
                nextGuaranteedRosterRefresh = rosterNow + kMaximumRosterRefreshGap;
        }

        if (!rosterUpdated)
        {
            updateEntity();
            updateBtrPassengerStates();
            publishCacheSnapshot();
            return;
        }

        const RegisteredPlayerArraySnapshot roster = MainGame::getRegisteredPlayerArraySnapshot();
        if (!roster)
            return;

        std::unordered_set<uint64_t> uniqueAddresses(roster->begin(), roster->end());
        std::vector<uint64_t> registeredAddresses(roster->begin(), roster->end());

        const auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(playerMutex);
            registeredPlayerScratch = std::move(uniqueAddresses);

            if (Utils::valid_pointer(mainGame.localPlayerPtr) && registeredPlayerScratch.contains(mainGame.localPlayerPtr))
            {
                std::erase_if(playerCache, [](const Player& player)
                    {
                        return player.isLocal && player.instance != mainGame.localPlayerPtr;
                    });
            }

            rebuildPlayerCacheIndexLocked();
            const auto lastAttempt = [&](uint64_t instance)
            {
                const auto entry = failedAllocations.find(instance);
                return entry == failedAllocations.end() ? std::chrono::steady_clock::time_point{} : entry->second.lastAttempt;
            };
            std::stable_sort(registeredAddresses.begin(), registeredAddresses.end(),
                [&](uint64_t left, uint64_t right) { return lastAttempt(left) < lastAttempt(right); });
        }

        if (registeredAddresses.empty())
        {
            publishCacheSnapshot();
            return;
        }

        std::vector<Player> pendingPlayers;
        pendingPlayers.reserve(registeredAddresses.size());

        unsigned allocationAttempts = 0;
        for (const uint64_t instance : registeredAddresses)
        {
            bool isCached = false;
            bool shouldRetry = true;

            {
                std::lock_guard<std::mutex> lock(playerMutex);
                isCached = playerCacheIndex.contains(instance);

                if (const auto failure = failedAllocations.find(instance); failure != failedAllocations.end())
                    shouldRetry = now - failure->second.lastAttempt >= allocationRetryCooldown;
            }

            if (isCached || !shouldRetry)
                continue;

            if (allocationAttempts++ >= 2)
                break;

            const bool isLocal = instance == mainGame.localPlayerPtr;
            PlayerMemoryAccess::InitializationReads reads;
            auto player = [&]
            {
                const PlayerMemoryAccess::InitializationScope scope(reads);
                return buildEntity(instance, isLocal);
            }();

            if (!player.has_value())
            {
                std::lock_guard<std::mutex> lock(playerMutex);
                PlayerAllocationFailure& failure = failedAllocations[instance];
                ++failure.attempts;
                failure.lastAttempt = now;
                if (failure.attempts == 1 || failure.attempts == 4 || failure.attempts == 16 ||
                    failure.attempts == 64 || failure.attempts % 256 == 0)
                    LOGS.logInfo("[PLAYER][INIT] instance=" + std::to_string(instance) +
                        " local=" + std::to_string(isLocal) + " attempt=" + std::to_string(failure.attempts) +
                        " stage=" + reads.stage + " failure=" + reads.failure +
                        " address=" + std::to_string(reads.failedAddress) +
                        " direct_recovered=" + std::to_string(reads.directRecovered));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(playerMutex);
                if (failedAllocations.contains(instance) || reads.directRecovered)
                    LOGS.logInfo("[PLAYER][INIT] recovered instance=" + std::to_string(instance) +
                        " local=" + std::to_string(isLocal) +
                        " direct_recovered=" + std::to_string(reads.directRecovered));
                failedAllocations.erase(instance);
            }

            watchListManager.logAddPlayer(*player);
            pendingPlayers.emplace_back(std::move(*player));
        }

        {
            std::lock_guard<std::mutex> lock(playerMutex);
            rebuildPlayerCacheIndexLocked();

            for (Player& player : pendingPlayers)
            {
                if (!Utils::valid_pointer(player.instance) || playerCacheIndex.contains(player.instance))
                    continue;

                player.cacheEntryId = nextCacheEntryId++;
                if (nextCacheEntryId == 0)
                    nextCacheEntryId = 1;

                playerCacheIndex[player.instance] = playerCache.size();
                playerCache.emplace_back(std::move(player));
            }

            tryFindBTR();
        }

        updateEntity();
        updateBtrPassengerStates();
        checkGroupIDs();
        checkExfil();

        if ((++playerRegistryRefreshCount & 0x3F) == 0)
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::erase_if(failedAllocations, [&](const auto& entry)
                {
                    return !registeredPlayerScratch.contains(entry.first);
                });
        }
    }
    catch (const std::exception& exception)
    {
        LOGS.logError("[PLAYERS] Exception in playersTask: " + std::string(exception.what()));
    }
    catch (...)
    {
        LOGS.logError("[PLAYERS] Unknown exception in playersTask");
    }

    publishCacheSnapshot();
}

std::optional<Player> RegisteredPlayers::buildEntity(uint64_t instance, bool isLocal)
{
    if (!mem.vHandle || !Utils::valid_pointer(instance))
        return std::nullopt;

    uint64_t classNamePointer = 0;
    if (!PlayerMemoryAccess::tryReadChain(instance, { 0x0, 0x10 }, classNamePointer, DmaCacheMode::Uncached))
        return std::nullopt;

    PlayerMemoryAccess::initializationStage("class name");
    const std::string className = PlayerMemoryAccess::readUtf8(classNamePointer, 64, DmaCacheMode::Uncached);

    if (className.empty())
        return std::nullopt;

    PlayerMemoryAccess::initializationStage("player class/required metadata");
    auto player = PlayerClassifier::classify(className, isLocal).tryCreate(instance, className);
    if (!player.has_value())
        return std::nullopt;

    uint64_t handsController = 0;
    if (Utils::valid_pointer(player->P_HandsControllerAddr) &&
        PlayerMemoryAccess::tryReadPointer(
            player->P_HandsControllerAddr,
            handsController,
            DmaCacheMode::Uncached))
    {
        player->P_HandsController = handsController;
        player->lastHeldItemHandsController = handsController;

        try
        {
            player->observedHandsInfo.update(*player);
        }
        catch (...)
        {
            player->observedHandsInfo.reset();
        }
    }

    player->nextHeldItemRefresh = {};
    return player;
}
