#pragma once

#include "Player/Player.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct PlayerGroup
{
    int id{ 0 };
    std::string groupId;
};

using PlayerGroups = PlayerGroup;

struct PlayerSnapshotTelemetry
{
    std::uint64_t snapshotVersion{ 0 };
    std::uint64_t motionVersion{ 0 };
    std::size_t playerCount{ 0 };
    double snapshotAgeMs{ -1.0 };
    double motionAgeMs{ -1.0 };
    double averageMotionIntervalMs{ 0.0 };
};

struct PlayerAllocationFailure
{
    std::uint32_t attempts{ 0 };
    std::chrono::steady_clock::time_point lastAttempt{};
};

struct PlayerVisibilityEdit
{
    uint64_t instance{};
    bool visibleToLocal{};
};

class RegisteredPlayers
{
public:
    RegisteredPlayers();

    void clearCache();
    void softRestart();

    std::vector<Player>& getCache();
    [[nodiscard]] PlayerSnapshot getCacheSnapshot() const noexcept;
    [[nodiscard]] PlayerSnapshotTelemetry getSnapshotTelemetry() const noexcept;

    void publishCacheSnapshot(bool motionUpdated = false);
    void applyGroupEdits(const std::vector<std::pair<uint64_t, std::string>>& edits);
    void applyVisibilityEdits(const std::vector<PlayerVisibilityEdit>& edits);

    std::vector<PlayerGroups>& getGroupCache();
    int getDistance(glm::vec3 point1, glm::vec3 point2);

    void playersTask();
    void boneTask();
    void playerEquipment();
    void playerMetadataTask();

    static bool groupIDSet;

    bool getBonePtrs(Player& player, bool forceResolve = false);

private:
    std::vector<Player> playerCache;
    std::unordered_map<uint64_t, std::size_t> playerCacheIndex;
    std::unordered_map<uint64_t, PlayerAllocationFailure> failedAllocations;
    std::unordered_set<uint64_t> registeredPlayerScratch;
    std::vector<PlayerGroups> playerGroups;
    std::atomic<PlayerSnapshot> publishedPlayerSnapshot;
    std::atomic<std::uint64_t> publishedSnapshotVersion{ 0 };
    std::atomic<std::uint64_t> publishedMotionVersion{ 0 };
    std::atomic<std::int64_t> publishedSnapshotTicks{ 0 };
    std::atomic<std::int64_t> publishedMotionTicks{ 0 };
    std::atomic<double> averageMotionIntervalMs{ 0.0 };
    std::size_t boneResolveCursor{ 0 };
    std::chrono::steady_clock::time_point nextFullBoneUpdate{};
    std::chrono::steady_clock::time_point lastBoneRefreshLog{};
    std::uint64_t boneRefreshesSinceLastLog{ 0 };
    std::size_t nextPlayerAllocationCursor{ 0 };
    std::uint64_t playerRegistryRefreshCount{ 0 };

    void publishCacheSnapshotLocked(bool motionUpdated = false);
    void rebuildPlayerCacheIndexLocked();
    std::string voice2Name(std::string voiceName);
    void readDogTagComponent(Player& player, bool force = false);
    std::optional<Player> buildEntity(uint64_t instance, bool isLocal);
    void tryFindBTR();
    void recoverBtrStuckPlayers();
    void updateEntity();
    void checkGroupIDs();
    void checkExfil();
    uint64_t getPlayerHealthControllerPtr(uint64_t instance);
    uint64_t getPlayerBoneMatrixPtr(uint64_t instance);
};

extern RegisteredPlayers registeredPlayers;
extern std::mutex playerMutex;
