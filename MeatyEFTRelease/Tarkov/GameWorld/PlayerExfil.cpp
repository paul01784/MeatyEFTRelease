#include "../../UI/includes.h"
#include "RegisteredPlayers.h"

#include "../../UI/globals.h"
#include "MainGame.h"
#include "../../Core/Utilities.h"
#include "../../memory/Memory.h"

#include <unordered_set>

void RegisteredPlayers::checkExfil()
{
    if (!mem.vHandle)
        return;

    std::lock_guard<std::mutex> lock(playerMutex);

    std::vector<Player>& cache = registeredPlayers.getCache();

    if (cache.empty())
        return;

    constexpr int MAX_REGISTERED_PLAYERS_SAFE = 512;

    const int registeredCount = mainGame.registeredPlayersCount;

    if (registeredCount <= 0 || registeredCount > MAX_REGISTERED_PLAYERS_SAFE)
    {
        LOGS.logError("[PLAYERS][EXFIL] Invalid registeredPlayersCount, skipping exfil check");
        return;
    }

    std::unordered_set<uint64_t> alivePlayers;
    alivePlayers.reserve(static_cast<size_t>(registeredCount));

    for (int i = 0; i < registeredCount; i++)
    {
        const uint64_t playerInstance = mainGame.player_buffer[i];

        if (!Utils::valid_pointer(playerInstance))
            continue;

        alivePlayers.insert(playerInstance);
    }

    if (alivePlayers.empty())
    {
        LOGS.logError("[PLAYERS][EXFIL] No valid registered players found, skipping exfil check");
        return;
    }

    // updatePlayerList() only commits a roster after uncached root, header,
    // contents and verification reads all succeed. Even a valid fresh roster
    // can briefly omit an entity while the game mutates the list, so absence
    // must persist across independently committed snapshots before it is
    // treated as an exfil.
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    static constexpr std::uint32_t kMinimumRosterMisses = 3;
    static constexpr Milliseconds kRosterMissingConfirmation{ 2000 };

    const Clock::time_point now = Clock::now();

    for (auto& cachedPlayer : cache)
    {
        if (cachedPlayer.isBTR)
            continue;

        if (cachedPlayer.isDead)
            continue;

        if (!Utils::valid_pointer(cachedPlayer.instance))
            continue;

        const bool stillRegistered =
            alivePlayers.find(cachedPlayer.instance) != alivePlayers.end();

        if (stillRegistered)
        {
            cachedPlayer.consecutiveRosterMisses = 0;
            cachedPlayer.rosterMissingSince = {};

            if (cachedPlayer.hasExfiled)
            {
                cachedPlayer.hasExfiled = false;
                LOGS.logWarn(
                    "[PLAYERS][EXFIL] Restored player after roster recovery: " +
                    cachedPlayer.name);
            }

            continue;
        }

        if (cachedPlayer.rosterMissingSince == Clock::time_point{})
            cachedPlayer.rosterMissingSince = now;

        if (cachedPlayer.consecutiveRosterMisses <
            (std::numeric_limits<std::uint32_t>::max)())
        {
            ++cachedPlayer.consecutiveRosterMisses;
        }

        if (cachedPlayer.consecutiveRosterMisses < kMinimumRosterMisses ||
            now - cachedPlayer.rosterMissingSince < kRosterMissingConfirmation)
        {
            continue;
        }

        if (cachedPlayer.hasExfiled)
            continue;

        cachedPlayer.hasExfiled = true;

        LOGS.logInfo(
            "[PLAYERS][EXFIL] Confirmed player absent from fresh roster: " +
            cachedPlayer.name);

        if (cachedPlayer.isLocal)
        {
            LOGS.logInfo("[PLAYERS][EXFIL] Local player no longer registered");
        }
    }
}
