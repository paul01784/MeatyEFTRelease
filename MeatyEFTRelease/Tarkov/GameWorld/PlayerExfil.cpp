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

    const bool localPlayerMissingFromRoster =
        Utils::valid_pointer(mainGame.localPlayerPtr) &&
        !mainGame.localGroupId.empty() &&
        !alivePlayers.contains(mainGame.localPlayerPtr);

    bool localPlayerIsInactive = false;
    for (const Player& cachedPlayer : cache)
    {
        const bool isLocalPlayer =
            cachedPlayer.isLocal ||
            (Utils::valid_pointer(mainGame.localPlayerPtr) &&
                cachedPlayer.instance == mainGame.localPlayerPtr);

        if (isLocalPlayer &&
            (cachedPlayer.isDead || cachedPlayer.hasExfiled))
        {
            localPlayerIsInactive = true;
            break;
        }
    }

    const bool localGroupProtectionActive =
        !mainGame.localGroupId.empty() &&
        (localPlayerMissingFromRoster || localPlayerIsInactive);

    
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    static constexpr std::uint32_t kMinimumRosterMisses = 3;
    static constexpr Milliseconds kRosterMissingConfirmation{ 2000 };

    const Clock::time_point now = Clock::now();

    for (auto& cachedPlayer : cache)
    {
        if (cachedPlayer.isBTR)
            continue;

        if (!Utils::valid_pointer(cachedPlayer.instance))
            continue;

        const bool stillRegistered =
            alivePlayers.find(cachedPlayer.instance) != alivePlayers.end();

        const bool isProtectedGroupMember =
            localGroupProtectionActive &&
            !cachedPlayer.isLocal &&
            cachedPlayer.instance != mainGame.localPlayerPtr &&
            cachedPlayer.groupId == mainGame.localGroupId;

        if (isProtectedGroupMember)
        {
            cachedPlayer.hasExfiled = false;
            cachedPlayer.consecutiveRosterMisses = 0;
            cachedPlayer.rosterMissingSince = {};
            continue;
        }

        if (cachedPlayer.isLocal ||
            cachedPlayer.instance == mainGame.localPlayerPtr)
        {
            if (!stillRegistered)
                cachedPlayer.hasExfiled = false;

            cachedPlayer.consecutiveRosterMisses = 0;
            cachedPlayer.rosterMissingSince = {};
            continue;
        }

        if (cachedPlayer.isDead)
            continue;

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
