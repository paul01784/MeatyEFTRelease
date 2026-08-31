#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerAppearance.h"
#include "PlayerLookup.h"

#include "../../../UI/DogTagAPI.h"
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

    static constexpr Milliseconds kHeldItemRefreshInterval{ 3000 };
    static constexpr Milliseconds kPredictionHeldItemRefreshInterval{ 250 };
    static constexpr Milliseconds kFailedReadRetryInterval{ 500 };

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
    };

    const Clock::time_point now = Clock::now();
    std::vector<MetadataJob> jobs;

    {
        std::lock_guard<std::mutex> lock(playerMutex);
        jobs.reserve(playerCache.size());

        for (Player& player : playerCache)
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

            if (Utils::valid_pointer(player.P_HandsController))
            {
                job.updateHands =
                    player.nextHeldItemRefresh == Clock::time_point{} ||
                    now >= player.nextHeldItemRefresh;

                if (job.updateHands)
                {
                    const Milliseconds refreshInterval =
                        player.isLocal &&
                        aimGlobals::predictionEnabled
                        ? kPredictionHeldItemRefreshInterval
                        : kHeldItemRefreshInterval;

                    player.nextHeldItemRefresh =
                        now + refreshInterval;
                }
            }

            job.updateWatchStatus = player.isPlayer || player.isPlayerScav;

            job.lookupDogTag =
                player.isPlayer &&
                !player.profileId.empty() &&
                !player.foundDogTagCache &&
                player.name.contains("PMC") &&
                now - player.lastDogTagLookup >
                    std::chrono::seconds(5);

            if (job.lookupDogTag)
                player.lastDogTagLookup = now;

            const int profileMode = std::clamp(radarGlobals::tarkovDevDataMode, 0,  1);
            const unsigned int profileModeBit = 1u << static_cast<unsigned int>(profileMode);
            job.lookupProfile = player.isPlayer && !player.profileId.empty() &&
                !player.accountId.empty() &&
                radarGlobals::getPlayerStats == TRUE &&
                player.profileDataMode != profileMode && (player.attemptedProfileDataModes & profileModeBit) == 0;

            if (job.lookupProfile)
            {
                job.profileMode = profileMode;
                player.attemptedProfileDataModes |= profileModeBit;
            }

            if (!job.updateHands &&
                !job.updateWatchStatus &&
                !job.lookupDogTag &&
                !job.lookupProfile)
            {
                continue;
            }

            job.player = player;
            jobs.emplace_back(std::move(job));
        }
    }

    for (MetadataJob& job : jobs)
    {
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
                const auto profile =
                    TarkovDevProfileClient::GetProfileForAccountId(job.player.accountId, job.profileMode);

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

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (const MetadataJob& job : jobs)
        {
            Player* player =
                PlayerLookup::findByInstance(playerCache, job.instance);

            if (!player)
                continue;

            bool raidEntryNeedsRefresh = false;

            if (job.updateHands)
            {
                if (job.handsSucceeded)
                {
                    player->observedHandsInfo =
                        job.player.observedHandsInfo;
                }
                else
                {
                    player->itemInHand.clear();
                    player->nextHeldItemRefresh =
                        now + kFailedReadRetryInterval;
                }
            }

            if (job.updateWatchStatus)
            {
                player->isWatched = job.player.isWatched;
                player->isFriend = job.player.isFriend;

                if (player->isFriend && !player->isLocal)
                {
                    if (!player->friendGroupOverride)
                    {
                        player->groupIdBeforeFriend = player->groupId;
                        player->friendGroupOverride = true;
                    }

                    player->groupId = mainGame.localGroupId;
                }
                else if (player->friendGroupOverride)
                {
                    player->groupId = std::move(player->groupIdBeforeFriend);
                    player->groupIdBeforeFriend.clear();
                    player->friendGroupOverride = false;
                }
            }

            if (job.lookupDogTag)
            {
                player->name = job.player.name;
                player->accountId = job.player.accountId;
                player->foundDogTagCache =
                    job.player.foundDogTagCache;
                raidEntryNeedsRefresh = player->foundDogTagCache;
            }

            if (job.lookupProfile && job.profileLookupSucceeded)
            {
                player->profileStats = job.player.profileStats;
                player->hasProfileData = true;
                player->profileDataMode = job.player.profileDataMode;
                player->DT_lvl = job.player.DT_lvl;
                player->kd = job.player.kd;
                player->pkd = job.player.pkd;
                player->hours = job.player.hours;
                raidEntryNeedsRefresh = true;
            }

            PlayerAppearance::updateColour(*player);

            
            if (raidEntryNeedsRefresh)
                watchListManager.logAddPlayer(*player);
        }
    }

    publishCacheSnapshot();
}
