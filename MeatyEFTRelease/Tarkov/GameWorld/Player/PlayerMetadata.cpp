#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerAppearance.h"
#include "ObservedPlayer.h"
#include "../../../UI/debug.h"
#include "../../../UI/globals.h"
#include "DogTagCache.h"
#include "../MainGame.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"
#include "../../../Core/Utilities.h"
#include "WatchList.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    int calculateKd(uint32_t kills, uint32_t deaths)
    {
        if (deaths == 0)
            return static_cast<int>(kills);

        const float kd = static_cast<float>(kills) / static_cast<float>(deaths);
        return static_cast<int>(std::round(kd));
    }

    double calculatePkd(uint32_t kills, uint32_t deaths)
    {
        if (deaths == 0)
            return static_cast<double>(kills);

        const double pkd = static_cast<double>(kills) / static_cast<double>(deaths);
        return std::round(pkd * 100.0) / 100.0;
    }

    int convertXpToLevel(int xp)
    {
        for (int level = 1; level < static_cast<int>(LevelXpThresholds.size()); ++level)
        {
            if (xp < LevelXpThresholds[level])
                return level;
        }

        return static_cast<int>(LevelXpThresholds.size());
    }
}

void RegisteredPlayers::playerMetadataTask()
{
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    static constexpr Milliseconds kLocalHeldItemRefreshInterval{ 100 };
    static constexpr Milliseconds kRemoteHeldItemRefreshInterval{ 5000 };
    static constexpr Milliseconds kFailedReadRetryInterval{ 500 };
    static constexpr Milliseconds kLocalFailedReadRetryInterval{ 100 };
    static constexpr Milliseconds kAiRoleRetryInterval{ 2000 };
    // Bound optional work on this background lane; never add reads to the pose/aim lane.
    unsigned int aiRoleBudget = 2;

    struct MetadataJob
    {
        uint64_t instance = 0;
        Player player;
        int profileMode = 0;
        bool updateHands = false;
        bool updateWatchStatus = false;
        bool lookupDogTag = false;
        bool lookupProfile = false;
        bool profileLookupSucceeded = false;
        bool handsSucceeded = false;
        bool refreshAiRole = false;
        bool aiRoleSucceeded = false;
    };

    const Clock::time_point now = Clock::now();
    const PlayerSnapshot cacheSnapshot = getCacheSnapshot();
    std::vector<MetadataJob> jobs;
    jobs.reserve(cacheSnapshot->size());

    for (const Player& player : *cacheSnapshot)
    {
        if (!Utils::valid_pointer(player.instance) ||
            player.isBTR ||
            player.isDead ||
            player.hasExfiled)
        {
            continue;
        }

        MetadataJob job{};
        job.instance = player.instance;
        job.player = player;

        job.refreshAiRole = aiRoleBudget > 0 && player.getKind() == PlayerKind::Observed &&
            player.isAi && player.playerSide == EPlayerSide::Savage && player.voice.empty() &&
            now >= player.nextAiRoleRefresh;
        if (job.refreshAiRole)
        {
            --aiRoleBudget;
            job.player.nextAiRoleRefresh = now + kAiRoleRetryInterval;
        }

        if (Utils::valid_pointer(player.P_HandsController))
        {
            job.updateHands =
                player.nextHeldItemRefresh == Clock::time_point{} ||
                now >= player.nextHeldItemRefresh;

            if (job.updateHands)
            {
                const Milliseconds refreshInterval = player.isLocal
                    ? kLocalHeldItemRefreshInterval
                    : kRemoteHeldItemRefreshInterval;

                job.player.nextHeldItemRefresh =
                    now + refreshInterval;
            }
        }

        job.updateWatchStatus = player.isPlayer || player.isPlayerScav;

        job.lookupDogTag =
            player.isPlayer &&
            !player.profileId.empty() &&
            !player.dogTagCacheLookupAttempted;

        if (job.lookupDogTag)
            job.player.dogTagCacheLookupAttempted = true;

        const int profileMode = std::clamp(radarGlobals::tarkovDevDataMode, 0, 1);
        const unsigned int profileModeBit = 1u << static_cast<unsigned int>(profileMode);
        job.lookupProfile = player.isPlayer && !player.profileId.empty() &&
            !player.accountId.empty() &&
            radarGlobals::getPlayerStats == TRUE &&
            player.profileDataMode != profileMode && (player.attemptedProfileDataModes & profileModeBit) == 0;

        if (job.lookupProfile)
        {
            job.profileMode = profileMode;
            job.player.attemptedProfileDataModes |= profileModeBit;
        }

        if (!job.updateHands &&
            !job.refreshAiRole &&
            !job.updateWatchStatus &&
            !job.lookupDogTag &&
            !job.lookupProfile)
        {
            continue;
        }

        jobs.emplace_back(std::move(job));
    }

    for (MetadataJob& job : jobs)
    {
        if (job.refreshAiRole)
            job.aiRoleSucceeded = ObservedPlayer::tryRefreshAiRole(job.player);

        if (job.updateHands)
        {
            try
            {
                job.handsSucceeded =
                    job.player.observedHandsInfo.update(job.player);
            }
            catch (...)
            {
                job.handsSucceeded = false;
                LOGS.logError(
                    "[PLAYERS][METADATA] Hands update failed");
            }
        }

        if (job.updateWatchStatus)
        {
            try
            {
                watchListManager.UpdateWatchStatus(job.player);
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYERS][METADATA] Watch status update failed");
            }
        }

        if (job.lookupDogTag)
        {
            try
            {
                const auto result =
                    g_dogTagCache.GetByProfileId(job.player.profileId);

                if (result.has_value())
                {
                    if (!result->nickname.empty())
                        job.player.name = result->nickname;

                    job.player.accountId = result->accountId;
                    job.player.foundDogTagCache = true;
                }
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYERS][METADATA] Dogtag cache lookup failed");
            }
        }

        if (job.lookupProfile)
        {
            try
            {
                auto profile = getCachedProfileApiResult(job.player.accountId, job.profileMode);

                if (!profile && tryBeginProfileApiRequest(job.player.accountId, job.profileMode))
                {
                    profile = TarkovDevProfileClient::GetProfileForAccountId(job.player.accountId, job.profileMode);

                    if (profile)
                        cacheProfileApiResult(job.player.accountId, job.profileMode, *profile);
                }

                if (profile)
                {
                    job.player.profileStats = *profile;
                    job.player.hasProfileData = true;
                    job.player.profileDataMode = job.profileMode;
                    job.player.DT_lvl = convertXpToLevel(profile->experience);
                    job.player.kd = calculateKd(profile->Kills, profile->deathsPMC);
                    job.player.pkd = calculatePkd(profile->killedPMC, profile->deathsPMC);
                    job.player.hours = profile->hoursPlayed;
                    job.profileLookupSucceeded = true;
                }
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYERS][METADATA] Tarkov profile lookup failed");
            }
        }
    }

    for (MetadataJob& job : jobs)
    {
        const bool raidEntryNeedsRefresh = (job.lookupDogTag && job.player.foundDogTagCache) || job.profileLookupSucceeded;
        if (raidEntryNeedsRefresh)
            watchListManager.logAddPlayer(job.player);

        const uint64_t instance = job.instance;
        const uint64_t cacheEntryId = job.player.cacheEntryId;
        queuePlayerEdit(instance, cacheEntryId, [job = std::move(job), now](Player& player) mutable
            {
                if (player.isBTR || player.isDead || player.hasExfiled)
                    return;

                if (job.refreshAiRole && player.voice.empty())
                {
                    player.nextAiRoleRefresh = job.player.nextAiRoleRefresh;
                    player.aiRoleReadAttempts = job.player.aiRoleReadAttempts;
                    player.aiRoleReadStatus = job.player.aiRoleReadStatus;
                    if (job.aiRoleSucceeded)
                    {
                        player.voice = std::move(job.player.voice);
                        player.name = std::move(job.player.name);
                        player.type = job.player.type;
                        player.isZombie = job.player.isZombie;
                        player.isBoss = job.player.isBoss;
                        player.isBlackDivision = job.player.isBlackDivision;
                        player.isCultist = job.player.isCultist;
                    }
                }

                if (job.updateHands)
                {
                    player.nextHeldItemRefresh = job.player.nextHeldItemRefresh;

                    if (job.handsSucceeded)
                        player.observedHandsInfo = std::move(job.player.observedHandsInfo);
                    else
                    {
                        player.itemInHand.clear();
                        player.nextHeldItemRefresh = now + (player.isLocal
                            ? kLocalFailedReadRetryInterval
                            : kFailedReadRetryInterval);
                    }
                }

                if (job.updateWatchStatus)
                {
                    player.isWatched = job.player.isWatched;
                    player.isFriend = job.player.isFriend;

                    if (player.isFriend && !player.isLocal)
                    {
                        if (!player.friendGroupOverride)
                        {
                            player.groupIdBeforeFriend = player.groupId;
                            player.friendGroupOverride = true;
                        }

                        player.groupId = mainGame.localGroupId;
                    }
                    else if (player.friendGroupOverride)
                    {
                        player.groupId = std::move(player.groupIdBeforeFriend);
                        player.groupIdBeforeFriend.clear();
                        player.friendGroupOverride = false;
                    }
                }

                if (job.lookupDogTag)
                {
                    player.name = std::move(job.player.name);
                    player.accountId = std::move(job.player.accountId);
                    player.foundDogTagCache = job.player.foundDogTagCache;
                    player.dogTagCacheLookupAttempted = true;
                }

                if (job.lookupProfile)
                    player.attemptedProfileDataModes = job.player.attemptedProfileDataModes;

                if (job.profileLookupSucceeded)
                {
                    player.profileStats = std::move(job.player.profileStats);
                    player.hasProfileData = true;
                    player.profileDataMode = job.player.profileDataMode;
                    player.DT_lvl = job.player.DT_lvl;
                    player.kd = job.player.kd;
                    player.pkd = job.player.pkd;
                    player.hours = job.player.hours;
                }

                PlayerAppearance::updateColour(player);
            });
    }
}
