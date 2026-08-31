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
        std::make_shared<const PlayerCollection>())
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
    static std::int64_t SteadyClockTicks() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
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
    publishCacheSnapshotLocked();

    LOGS.logInfo("[PLAYER][CACHE] Data cleared");
}

void RegisteredPlayers::softRestart()
{
    std::lock_guard<std::mutex> lock(playerMutex);

    registeredPlayers.groupIDSet = false;
    mainGame.localGroupId = "";
    this->playerCache.clear();
    this->playerCacheIndex.clear();
    this->failedAllocations.clear();
    this->registeredPlayerScratch.clear();
    this->playerGroups.clear();
    boneResolveCursor = 0;
    nextFullBoneUpdate = {};
    lastBoneRefreshLog = {};
    boneRefreshesSinceLastLog = 0;
    publishCacheSnapshotLocked();
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

void RegisteredPlayers::publishCacheSnapshotLocked(bool motionUpdated)
{
    const PlayerSnapshot snapshot =
        std::make_shared<const PlayerCollection>(playerCache);
    const std::int64_t nowTicks = SteadyClockTicks();

    publishedPlayerSnapshot.store(snapshot, std::memory_order_release);
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

    std::lock_guard<std::mutex> lock(playerMutex);

    for (const auto& [instance, newGroupId] : edits)
    {
        auto player = std::find_if(
            playerCache.begin(),
            playerCache.end(),
            [&](const Player& candidate)
            {
                return candidate.instance == instance;
            });

        if (player == playerCache.end() ||
            player->isDead ||
            player->hasExfiled)
        {
            continue;
        }

        player->groupId = newGroupId;

        if (player->isLocal || player->instance == mainGame.localPlayerPtr)
        {
            if (newGroupId.empty() || newGroupId == "0")
                mainGame.localGroupId.clear();
            else
                mainGame.localGroupId = newGroupId;
        }

        std::ostringstream message;
        message << "[PLAYERS][GROUP EDIT] "
            << player->name
            << " instance: 0x"
            << std::hex << player->instance
            << " groupId: "
            << (newGroupId.empty() ? "none" : newGroupId);

        LOGS.logInfo(message.str());
    }

    publishCacheSnapshotLocked();
}

void RegisteredPlayers::applyVisibilityEdits(
    const std::vector<PlayerVisibilityEdit>& edits)
{
    if (edits.empty())
        return;

    std::lock_guard<std::mutex> lock(playerMutex);
    bool changed = false;

    for (const PlayerVisibilityEdit& edit : edits)
    {
        const auto index = playerCacheIndex.find(edit.instance);

        if (index == playerCacheIndex.end() || index->second >= playerCache.size())
            continue;

        Player& player = playerCache[index->second];

        if (player.isLocal || player.visibleToLocal == edit.visibleToLocal)
            continue;

        player.visibleToLocal = edit.visibleToLocal;
        changed = true;
    }

    if (changed)
        publishCacheSnapshotLocked();
}

std::vector<PlayerGroups>& RegisteredPlayers::getGroupCache()
{
    return playerGroups;
}

