#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct PlayerCache;

class WatchListManager final
{
public:
    struct WatchListEntry
    {
        std::string dateAdded;
        std::string profileId;
        std::string name;
        std::string reason;
    };

    struct RaidPlayerEntry
    {
        //temporary identity until profileId is available
        uint64_t instance = 0;

        std::string profileId;
        std::string accountId;
        std::string name;
        std::string groupId;
        std::string side;

        std::string firstSeenAt;
        std::string lastUpdatedAt;

        bool isLocal = false;
        bool isWatched = false;
        bool isDead = false;
        bool hasExfiled = false;

        int level = 0;
        int kd = 0;
        double pmcKd = 0.0;
        int hours = 0;
        int playerValue = 0;

        std::string itemInHand;
    };

    struct RaidRecord
    {
        std::string raidId;
        std::string mapName;

        std::string startedAt;
        std::string endedAt;

        long long durationSeconds = 0;
        std::string durationText = "00:00:00";

        bool active = false;

        std::vector<RaidPlayerEntry> players;
    };

public:
    explicit WatchListManager(std::filesystem::path watchListPath = "configs\\watchlist.json");

    ~WatchListManager() = default;

    WatchListManager(const WatchListManager&) = delete;
    WatchListManager& operator=(const WatchListManager&) = delete;

    WatchListManager(WatchListManager&&) = delete;
    WatchListManager& operator=(WatchListManager&&) = delete;

    bool LoadWatchList();

    bool SaveWatchList() const;

    bool AddPlayer(PlayerCache& player, const std::string& reason);
    bool AddPlayer(const std::string& profileId, const std::string& name, const std::string& reason);

    bool RemovePlayer(const std::string& profileId);

    bool UpdateReason(
        const std::string& profileId,
        const std::string& newReason
    );

    bool IsWatched(const std::string& profileId) const;

    std::optional<WatchListEntry> GetWatchedPlayer(
        const std::string& profileId
    ) const;

    std::vector<WatchListEntry> GetWatchList() const;

    std::size_t GetWatchListCount() const;

    bool ClearWatchList();

    void UpdateWatchStatus(PlayerCache& player) const;

    bool logRaidStart(const std::string& mapName);

    // Ends the current raid and records its end time and duration.
    bool logRaidEnd();

    // Adds or updates a PMC in the active raid
    bool logAddPlayer(PlayerCache& player);

    // Call this after profileId becomes available
    bool logUpdatePlayerPID(PlayerCache& player);

    bool HasActiveRaid() const;

    std::optional<RaidRecord> GetActiveRaid() const;

    std::vector<RaidRecord> GetRaidHistory() const;

    std::string GetLastError() const;

    std::filesystem::path GetWatchListPath() const;

    void RenderWindow();

private:
    bool SaveWatchListUnlocked() const;

    void RebuildWatchIndexUnlocked();

    bool UpsertRaidPlayerUnlocked(
        PlayerCache& player,
        bool requireProfileId
    );

    void CopyPlayerDataUnlocked(
        RaidPlayerEntry& destination,
        const PlayerCache& player
    ) const;

    std::optional<std::size_t> FindRaidPlayerByInstanceUnlocked(
        const RaidRecord& raid,
        uint64_t instance
    ) const;

    std::optional<std::size_t> FindRaidPlayerByProfileIdUnlocked(
        const RaidRecord& raid,
        const std::string& profileId
    ) const;

    void EndActiveRaidUnlocked(
        const std::chrono::system_clock::time_point& systemNow,
        const std::chrono::steady_clock::time_point& steadyNow
    );

    static bool IsEligiblePlayer(const PlayerCache& player);

    static std::string ResolveProfileId(const PlayerCache& player);

    static std::string ResolveAccountId(const PlayerCache& player);

    static std::string ResolvePlayerName(const PlayerCache& player);

    static std::string FormatTimestamp(
        const std::chrono::system_clock::time_point& time
    );

    static std::string FormatRaidId(
        std::size_t raidNumber,
        const std::chrono::system_clock::time_point& time
    );

    static std::string FormatDuration(long long durationSeconds);

private:
    mutable std::shared_mutex mutex_;

    std::filesystem::path watchListPath_;

    std::vector<WatchListEntry> watchList_;

    std::unordered_map<std::string, std::size_t> watchIndex_;

    std::vector<RaidRecord> raidHistory_;

    std::optional<std::size_t> activeRaidIndex_;

    std::chrono::steady_clock::time_point activeRaidStartedSteady_{};

    std::size_t raidCounter_ = 0;

    mutable std::string lastError_;
};

extern WatchListManager watchListManager;