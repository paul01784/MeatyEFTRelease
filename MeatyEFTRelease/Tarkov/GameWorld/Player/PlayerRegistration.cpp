#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerClassifier.h"
#include "PlayerLookup.h"

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
    constexpr std::size_t maximumNewPlayersPerTick = 4;
    constexpr std::uint32_t maximumAllocationFailures = 5;
    constexpr std::chrono::seconds allocationRetryCooldown{ 2 };
}

void RegisteredPlayers::playersTask()
{
    try
    {
        if (!mem.IsDmaOperational() || !mainGame.updatePlayerList())
            return;

        std::vector<uint64_t> registeredAddresses;
        std::unordered_set<uint64_t> uniqueAddresses;
        registeredAddresses.reserve(mainGame.registeredPlayersCount);
        uniqueAddresses.reserve(mainGame.registeredPlayersCount);

        for (int index = 0; index < mainGame.registeredPlayersCount; ++index)
        {
            const uint64_t instance = mainGame.player_buffer[index];

            if (Utils::valid_pointer(instance) && uniqueAddresses.insert(instance).second)
                registeredAddresses.emplace_back(instance);
        }

        if (registeredAddresses.empty())
            return;

        const auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(playerMutex);
            registeredPlayerScratch = std::move(uniqueAddresses);
            rebuildPlayerCacheIndexLocked();
        }

        std::vector<Player> pendingPlayers;
        pendingPlayers.reserve((std::min)(maximumNewPlayersPerTick, registeredAddresses.size()));
        nextPlayerAllocationCursor %= registeredAddresses.size();

        std::size_t inspected = 0;
        std::size_t attempts = 0;

        for (; inspected < registeredAddresses.size() && attempts < maximumNewPlayersPerTick; ++inspected)
        {
            const std::size_t index = (nextPlayerAllocationCursor + inspected) % registeredAddresses.size();
            const uint64_t instance = registeredAddresses[index];
            bool isCached = false;
            bool shouldRetry = true;

            {
                std::lock_guard<std::mutex> lock(playerMutex);
                isCached = playerCacheIndex.contains(instance);

                if (const auto failure = failedAllocations.find(instance); failure != failedAllocations.end())
                {
                    shouldRetry = failure->second.attempts < maximumAllocationFailures || now - failure->second.lastAttempt >= allocationRetryCooldown;
                }
            }

            if (isCached || !shouldRetry)
                continue;

            ++attempts;
            const bool isLocal = instance == mainGame.localPlayerPtr;
            auto player = buildEntity(instance, isLocal);

            if (!player.has_value())
            {
                std::lock_guard<std::mutex> lock(playerMutex);
                PlayerAllocationFailure& failure = failedAllocations[instance];
                ++failure.attempts;
                failure.lastAttempt = now;
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(playerMutex);
                failedAllocations.erase(instance);
            }

            watchListManager.logAddPlayer(*player);
            pendingPlayers.emplace_back(std::move(*player));
        }

        nextPlayerAllocationCursor = (nextPlayerAllocationCursor + (std::max)(std::size_t{ 1 }, inspected)) % registeredAddresses.size();

        std::vector<uint64_t> addedInstances;

        {
            std::lock_guard<std::mutex> lock(playerMutex);
            rebuildPlayerCacheIndexLocked();

            for (Player& player : pendingPlayers)
            {
                if (!Utils::valid_pointer(player.instance) || playerCacheIndex.contains(player.instance))
                    continue;

                addedInstances.emplace_back(player.instance);
                playerCacheIndex[player.instance] = playerCache.size();
                playerCache.emplace_back(std::move(player));
            }

            tryFindBTR();

            for (uint64_t instance : addedInstances)
            {
                Player* player = PlayerLookup::findByInstance(playerCache, instance);

                if (!player || player->isBTR || player->isDead || player->hasExfiled)
                    continue;

                player->bonePointersNeedResolve = true;
                getBonePtrs(*player, true);
            }
        }

        updateEntity();
        recoverBtrStuckPlayers();
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

    std::string className;

    try
    {
        className = ReadName(instance, 64, false);
    }
    catch (...)
    {
        return std::nullopt;
    }

    if (className.empty())
        return std::nullopt;

    return PlayerClassifier::classify(className, isLocal).tryCreate(instance, className);
}
