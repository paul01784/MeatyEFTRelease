#include "RegisteredPlayers.h"
#include "../../UI/debug.h"
#include "MainGame.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <sstream>

std::mutex playerMutex;
RegisteredPlayers registeredPlayers;

bool RegisteredPlayers::groupIDSet = false;

RegisteredPlayers::RegisteredPlayers()
    : publishedPlayerSnapshot(
        std::make_shared<const PlayerCollection>()),
    publishedLocalStateSnapshot(
        std::make_shared<const PlayerLocalState>())
{
}

void RegisteredPlayers::rebuildPlayerCacheIndexLocked()
{
    playerCacheIndex.clear();
    playerCacheIndex.reserve(playerCache.size());

    for (std::size_t index = 0; index < playerCache.size(); ++index)
    {
        if (Utils::valid_pointer(playerCache[index].instance))
            playerCacheIndex[playerCache[index].instance] = index;
    }
}

namespace
{
    constexpr std::uint64_t RosterCountMask = 0xFFFFULL;
    constexpr unsigned int AliveRosterCountShift = 16;
    constexpr unsigned int RosterWarningShift = 32;
    constexpr std::chrono::seconds RosterMismatchDelay{ 7 };

    static std::int64_t SteadyClockTicks() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static std::uint64_t PackRosterState(std::size_t registeredCount, std::size_t aliveCachedCount, bool possibleMissingEntities) noexcept
    {
        const std::uint64_t registered = (std::min)(static_cast<std::uint64_t>(registeredCount), RosterCountMask);
        const std::uint64_t aliveCached = (std::min)(static_cast<std::uint64_t>(aliveCachedCount), RosterCountMask);
        return registered | (aliveCached << AliveRosterCountShift) | (static_cast<std::uint64_t>(possibleMissingEntities) << RosterWarningShift);
    }

}

int RegisteredPlayers::getDistance(glm::vec3 point1, glm::vec3 point2)
{
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string RegisteredPlayers::voice2Name(std::string voiceName)
{
    if (voiceName.find("BossSanitar") != std::string::npos)
    {
        return "Sanitar";
    }
    else if (voiceName.find("BossBully") != std::string::npos)
    {
        return "BossBully";
    }
    else if (voiceName.find("BossGluhar") != std::string::npos)
    {
        return "Gluhar";
    }
    else if (voiceName.find("SectantPriest") != std::string::npos)
    {
        return "Priest";
    }
    else if (voiceName.find("SectantWarrior") != std::string::npos)
    {
        return "Warrior";
    }
    else if (voiceName.find("BossKilla") != std::string::npos)
    {
        return "Killa";
    }
    else if (voiceName.find("BossTagilla") != std::string::npos)
    {
        return "Tagilla";
    }
    else if (voiceName.find("Boss_Partizan") != std::string::npos)
    {
        return "Partizan";
    }
    else if (voiceName.find("BossBigPipe") != std::string::npos)
    {
        return "BigPipe";
    }
    else if (voiceName.find("BossBirdEye") != std::string::npos)
    {
        return "BirdEye";
    }
    else if (voiceName.find("BossKnight") != std::string::npos)
    {
        return "Knight";
    }
    else if (voiceName.find("Arena_Guard_1") != std::string::npos)
    {
        return "Arena Guard";
    }
    else if (voiceName.find("Arena_Guard_2") != std::string::npos)
    {
        return "Arena Guard";
    }
    else if (voiceName.find("Boss_Kaban") != std::string::npos)
    {
        return "Kaban";
    }
    else if (voiceName.find("Boss_Kollontay") != std::string::npos)
    {
        return "Kollontay";
    }
    else if (voiceName.find("Boss_Sturman") != std::string::npos)
    {
        return "Sturman";
    }
    else if (voiceName.find("Zombie_Generic") != std::string::npos)
    {
        return "Zombie";
    }
    else if (voiceName.find("BossZombieTagilla") != std::string::npos)
    {
        return "ZombieTagilla";
    }
    else if (voiceName.find("Zombie_Fast") != std::string::npos)
    {
        return "Zombie F";
    }
    else if (voiceName.find("Zombie_Medium") != std::string::npos)
    {
        return "Zombie M";
    }
    else
        return "Ai";
}

void RegisteredPlayers::clearCache()
{
    std::lock_guard<std::mutex> lock(playerMutex);
    resetCacheLocked();

    {
        std::lock_guard<std::mutex> editLock(deferredPlayerEditsMutex);
        deferredPlayerEdits.clear();
    }

    {
        std::lock_guard<std::mutex> apiLock(playerApiAttemptMutex);
        attemptedProfileApiRequests.clear();
        attemptedDogTagApiProfiles.clear();
        cachedProfileApiResults.clear();
        cachedDogTagApiResults.clear();
    }

    softRestartRequested.store(false, std::memory_order_release);
    publishCacheSnapshotLocked();

    LOGS.logInfo("[PLAYER][CACHE] Data cleared");
}

void RegisteredPlayers::resetCacheLocked()
{
    this->playerCache.clear();
    this->playerCacheIndex.clear();
    this->failedAllocations.clear();
    this->registeredPlayerScratch.clear();
    this->playerGroups.clear();
    boneResolveCursor = 0;
    nextFullBoneUpdate = {};
    lastBoneRefreshLog = {};
    boneRefreshesSinceLastLog = 0;
    registeredPlayers.groupIDSet = false;
}

void RegisteredPlayers::softRestart()
{
    softRestartRequested.store(true, std::memory_order_release);
}

std::vector<Player>& RegisteredPlayers::getCache()
{
    return playerCache;
}

PlayerSnapshot RegisteredPlayers::getCacheSnapshot() const noexcept
{
    PlayerSnapshot snapshot = publishedPlayerSnapshot.load(
        std::memory_order_acquire);

    if (snapshot)
        return snapshot;

    static const PlayerSnapshot emptySnapshot =
        std::make_shared<const PlayerCollection>();

    return emptySnapshot;
}

PlayerSnapshotTelemetry RegisteredPlayers::getSnapshotTelemetry() const noexcept
{
    PlayerSnapshotTelemetry telemetry{};
    const PlayerSnapshot snapshot = getCacheSnapshot();
    const std::int64_t nowTicks = SteadyClockTicks();
    const std::int64_t snapshotTicks =
        publishedSnapshotTicks.load(std::memory_order_acquire);
    const std::int64_t motionTicks =
        publishedMotionTicks.load(std::memory_order_acquire);

    telemetry.snapshotVersion =
        publishedSnapshotVersion.load(std::memory_order_relaxed);
    telemetry.motionVersion =
        publishedMotionVersion.load(std::memory_order_relaxed);
    telemetry.playerCount = snapshot->size();
    telemetry.averageMotionIntervalMs =
        averageMotionIntervalMs.load(std::memory_order_relaxed);

    if (snapshotTicks > 0)
        telemetry.snapshotAgeMs =
            static_cast<double>(nowTicks - snapshotTicks) / 1'000'000.0;

    if (motionTicks > 0)
        telemetry.motionAgeMs =
            static_cast<double>(nowTicks - motionTicks) / 1'000'000.0;

    return telemetry;
}

PlayerRosterStatus RegisteredPlayers::getRosterStatus() const noexcept
{
    const std::uint64_t state = publishedRosterState.load(std::memory_order_acquire);

    PlayerRosterStatus status{};
    status.registeredCount = static_cast<std::size_t>(state & RosterCountMask);
    status.aliveCachedCount = static_cast<std::size_t>((state >> AliveRosterCountShift) & RosterCountMask);
    status.possibleMissingEntities = ((state >> RosterWarningShift) & 1ULL) != 0;
    return status;
}

PlayerLocalStateSnapshot RegisteredPlayers::getLocalStateSnapshot() const noexcept
{
    PlayerLocalStateSnapshot snapshot = publishedLocalStateSnapshot.load(std::memory_order_acquire);

    if (snapshot)
        return snapshot;

    static const PlayerLocalStateSnapshot emptySnapshot = std::make_shared<const PlayerLocalState>();
    return emptySnapshot;
}

void RegisteredPlayers::publishCacheSnapshotLocked(bool motionUpdated)
{
    const std::size_t aliveCachedCount = static_cast<std::size_t>(std::count_if(playerCache.begin(), playerCache.end(), [&](const Player& player)
        {
            return registeredPlayerScratch.contains(player.instance) && !player.isDead && !player.hasExfiled;
        }));
    const std::size_t registeredCount = registeredPlayerScratch.size();
    const auto now = std::chrono::steady_clock::now();
    const bool rosterMismatch = registeredCount > aliveCachedCount;

    if (!rosterMismatch)
        rosterMismatchSince = {};
    else if (rosterMismatchSince == std::chrono::steady_clock::time_point{})
        rosterMismatchSince = now;

    const bool possibleMissingEntities = rosterMismatch && now - rosterMismatchSince >= RosterMismatchDelay;
    publishedRosterState.store(PackRosterState(registeredCount, aliveCachedCount, possibleMissingEntities), std::memory_order_release);

    const PlayerSnapshot snapshot =
        std::make_shared<const PlayerCollection>(playerCache);
    auto localState = std::make_shared<PlayerLocalState>();

    const auto localPlayer = std::find_if(playerCache.begin(), playerCache.end(), [](const Player& player)
        {
            return player.isLocal;
        });

    if (localPlayer != playerCache.end())
    {
        localState->location = localPlayer->location;
        localState->rotation = localPlayer->rotation;
        localState->groupId = localPlayer->groupId;
        localState->instance = localPlayer->instance;
        localState->handsController = localPlayer->P_HandsController;
        localState->profile = localPlayer->P_Profile;
        localState->isSavage = localPlayer->isPlayerScav;
        localState->isScoped = localPlayer->isAiming;
    }

    const std::int64_t nowTicks = SteadyClockTicks();

    publishedPlayerSnapshot.store(snapshot, std::memory_order_release);
    publishedLocalStateSnapshot.store(std::move(localState), std::memory_order_release);
    publishedSnapshotTicks.store(nowTicks, std::memory_order_release);
    publishedSnapshotVersion.fetch_add(1, std::memory_order_relaxed);

    if (!motionUpdated)
        return;

    const std::int64_t previousTicks =
        publishedMotionTicks.exchange(nowTicks, std::memory_order_acq_rel);

    if (previousTicks > 0 && nowTicks > previousTicks)
    {
        const double intervalMs =
            static_cast<double>(nowTicks - previousTicks) / 1'000'000.0;
        const double currentAverage =
            averageMotionIntervalMs.load(std::memory_order_relaxed);
        const double nextAverage = currentAverage <= 0.0
            ? intervalMs
            : (currentAverage * 0.88) + (intervalMs * 0.12);

        averageMotionIntervalMs.store(
            nextAverage,
            std::memory_order_relaxed);
    }

    publishedMotionVersion.fetch_add(1, std::memory_order_relaxed);
}

void RegisteredPlayers::publishCacheSnapshot(bool motionUpdated)
{
    std::lock_guard<std::mutex> lock(playerMutex);
    publishCacheSnapshotLocked(motionUpdated);
}

void RegisteredPlayers::applyGroupEdits(
    const std::vector<std::pair<uint64_t, std::string>>& edits)
{
    if (edits.empty())
        return;

    for (const auto& [instance, newGroupId] : edits)
    {
        queuePlayerEdit(instance, [newGroupId](Player& player)
            {
                if (player.isDead || player.hasExfiled)
                    return;

                player.groupId = newGroupId;

                if (player.isLocal || player.instance == mainGame.localPlayerPtr)
                {
                    if (newGroupId.empty() || newGroupId == "0")
                        mainGame.localGroupId.clear();
                    else
                        mainGame.localGroupId = newGroupId;
                }

                std::ostringstream message;
                message << "[PLAYERS][GROUP EDIT] " << player.name << " instance: 0x" << std::hex << player.instance << " groupId: "
                    << (newGroupId.empty() ? "none" : newGroupId);
                LOGS.logInfo(message.str());
            });
    }
}

void RegisteredPlayers::applyVisibilityEdits(
    const std::vector<PlayerVisibilityEdit>& edits)
{
    if (edits.empty())
        return;

    for (const PlayerVisibilityEdit& edit : edits)
    {
        queuePlayerEdit(edit.instance, [visibleToLocal = edit.visibleToLocal](Player& player)
            {
                if (!player.isLocal)
                    player.visibleToLocal = visibleToLocal;
            });
    }
}

void RegisteredPlayers::queuePlayerEdit(std::uint64_t instance, std::function<void(Player&)> edit)
{
    queuePlayerEdit(instance, 0, std::move(edit));
}

void RegisteredPlayers::queuePlayerEdit(std::uint64_t instance, std::uint64_t cacheEntryId, std::function<void(Player&)> edit)
{
    if (!Utils::valid_pointer(instance) || !edit)
        return;

    std::lock_guard<std::mutex> lock(deferredPlayerEditsMutex);
    deferredPlayerEdits.push_back({ instance, cacheEntryId, std::move(edit) });
}

bool RegisteredPlayers::applyPendingPlayerEdits()
{
    std::vector<DeferredPlayerEdit> edits;

    {
        std::unique_lock<std::mutex> lock(deferredPlayerEditsMutex, std::try_to_lock);
        if (!lock.owns_lock() || deferredPlayerEdits.empty())
            return false;

        edits.swap(deferredPlayerEdits);
    }

    bool changed = false;

    for (DeferredPlayerEdit& edit : edits)
    {
        const auto index = playerCacheIndex.find(edit.instance);
        if (index == playerCacheIndex.end() || index->second >= playerCache.size())
            continue;

        Player& player = playerCache[index->second];
        if (edit.cacheEntryId != 0 && player.cacheEntryId != edit.cacheEntryId)
            continue;

        edit.apply(player);
        changed = true;
    }

    return changed;
}

bool RegisteredPlayers::tryBeginProfileApiRequest(const std::string& accountId, int profileMode)
{
    if (accountId.empty())
        return false;

    const std::string requestKey = accountId + ':' + std::to_string(profileMode);
    std::lock_guard<std::mutex> lock(playerApiAttemptMutex);
    return attemptedProfileApiRequests.insert(requestKey).second;
}

std::optional<PlayerProfileStats> RegisteredPlayers::getCachedProfileApiResult(const std::string& accountId, int profileMode)
{
    const std::string requestKey = accountId + ':' + std::to_string(profileMode);
    std::lock_guard<std::mutex> lock(playerApiAttemptMutex);

    const auto result = cachedProfileApiResults.find(requestKey);
    if (result == cachedProfileApiResults.end())
        return std::nullopt;

    return result->second;
}

void RegisteredPlayers::cacheProfileApiResult(const std::string& accountId, int profileMode, const PlayerProfileStats& profile)
{
    const std::string requestKey = accountId + ':' + std::to_string(profileMode);
    std::lock_guard<std::mutex> lock(playerApiAttemptMutex);
    cachedProfileApiResults.insert_or_assign(requestKey, profile);
}

bool RegisteredPlayers::tryBeginDogTagApiRequest(const std::string& profileId)
{
    if (profileId.empty())
        return false;

    std::lock_guard<std::mutex> lock(playerApiAttemptMutex);
    return attemptedDogTagApiProfiles.insert(profileId).second;
}

std::optional<RegisteredPlayers::DogTagApiResult> RegisteredPlayers::getCachedDogTagApiResult(const std::string& profileId)
{
    std::lock_guard<std::mutex> lock(playerApiAttemptMutex);

    const auto result = cachedDogTagApiResults.find(profileId);
    if (result == cachedDogTagApiResults.end())
        return std::nullopt;

    return result->second;
}

void RegisteredPlayers::cacheDogTagApiResult(const std::string& profileId, const DogTagApiResult& result)
{
    std::lock_guard<std::mutex> lock(playerApiAttemptMutex);
    cachedDogTagApiResults.insert_or_assign(profileId, result);
}

std::vector<PlayerGroups>& RegisteredPlayers::getGroupCache()
{
    return playerGroups;
}

