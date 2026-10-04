#pragma once

#include "Player/Player.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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

struct PlayerRosterStatus
{
    std::size_t registeredCount{ 0 };
    std::size_t aliveCachedCount{ 0 };
    bool possibleMissingEntities{ false };
};

struct PlayerLocalState
{
    glm::vec3 location{};
    glm::vec2 rotation{};
    std::string groupId;
    std::uint64_t instance{ 0 };
    std::uint64_t handsController{ 0 };
    std::uint64_t profile{ 0 };
    bool isSavage{ false };
    bool isScoped{ false };
};

using PlayerLocalStateSnapshot = std::shared_ptr<const PlayerLocalState>;

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
    [[nodiscard]] PlayerRosterStatus getRosterStatus() const noexcept;
    [[nodiscard]] PlayerLocalStateSnapshot getLocalStateSnapshot() const noexcept;

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
    std::atomic<PlayerLocalStateSnapshot> publishedLocalStateSnapshot;
    std::atomic<std::uint64_t> publishedSnapshotVersion{ 0 };
    std::atomic<std::uint64_t> publishedMotionVersion{ 0 };
    std::atomic<std::int64_t> publishedSnapshotTicks{ 0 };
    std::atomic<std::int64_t> publishedMotionTicks{ 0 };
    std::atomic<double> averageMotionIntervalMs{ 0.0 };
    std::atomic<std::uint64_t> publishedRosterState{ 0 };
    std::chrono::steady_clock::time_point rosterMismatchSince{};
    std::size_t boneResolveCursor{ 0 };
    std::chrono::steady_clock::time_point nextFullBoneUpdate{};
    std::chrono::steady_clock::time_point lastBoneRefreshLog{};
    std::uint64_t boneRefreshesSinceLastLog{ 0 };
    std::size_t nextPlayerAllocationCursor{ 0 };
    std::uint64_t playerRegistryRefreshCount{ 0 };
    std::uint64_t nextCacheEntryId{ 1 };
    std::atomic<bool> softRestartRequested{ false };

    struct DeferredPlayerEdit
    {
        std::uint64_t instance{ 0 };
        std::uint64_t cacheEntryId{ 0 };
        std::function<void(Player&)> apply;
    };

    std::mutex deferredPlayerEditsMutex;
    std::vector<DeferredPlayerEdit> deferredPlayerEdits;
    std::mutex playerApiAttemptMutex;
    std::unordered_set<std::string> attemptedProfileApiRequests;
    std::unordered_set<std::string> attemptedDogTagApiProfiles;
    std::unordered_map<std::string, PlayerProfileStats> cachedProfileApiResults;

    struct DogTagApiResult
    {
        std::string accountId;
        std::string nickname;
        int level{ 0 };
    };

    std::unordered_map<std::string, DogTagApiResult> cachedDogTagApiResults;

    void publishCacheSnapshotLocked(bool motionUpdated = false);
    void rebuildPlayerCacheIndexLocked();
    void resetCacheLocked();
    void queuePlayerEdit(std::uint64_t instance, std::function<void(Player&)> edit);
    void queuePlayerEdit(std::uint64_t instance, std::uint64_t cacheEntryId, std::function<void(Player&)> edit);
    bool applyPendingPlayerEdits();
    bool pruneInvalidCachedPlayersLocked();
    bool tryBeginProfileApiRequest(const std::string& accountId, int profileMode);
    bool tryBeginDogTagApiRequest(const std::string& profileId);
    std::optional<PlayerProfileStats> getCachedProfileApiResult(const std::string& accountId, int profileMode);
    void cacheProfileApiResult(const std::string& accountId, int profileMode, const PlayerProfileStats& profile);
    std::optional<DogTagApiResult> getCachedDogTagApiResult(const std::string& profileId);
    void cacheDogTagApiResult(const std::string& profileId, const DogTagApiResult& result);
    std::string voice2Name(std::string voiceName);
    std::optional<Player> buildEntity(uint64_t instance, bool isLocal);
    void tryFindBTR();
    void updateBtrPassengerStates();
    void updateEntity();
    void checkGroupIDs();
    void checkExfil();
    uint64_t getPlayerHealthControllerPtr(uint64_t instance);
    uint64_t getPlayerBoneMatrixPtr(uint64_t instance);
};

extern RegisteredPlayers registeredPlayers;
extern std::mutex playerMutex;
