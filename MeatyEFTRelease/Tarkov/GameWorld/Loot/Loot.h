#pragma once

#include "LootEntity.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct lootFilterItems
{
    std::string bsgid;
    std::string name;
    std::string shortName;
    long traderPrice = 0;
    long marketPrice = 0;
};

struct LootFilters
{
    long id = 0;
    bool active = false;

    std::string filterName;
    glm::vec4 filterColour{};

    std::vector<lootFilterItems> lootItems;
};

using LootCacheCollection = std::vector<LootEntity>;
using LootCacheSnapshot = std::shared_ptr<const LootCacheCollection>;

extern std::vector<LootFilters> lootFilters;

class loot
{
public:
    loot();

    void lootTask();
    void clearCache();

    void setLootWanted(uint64_t instance, bool wanted, const glm::vec4& colour);
    [[nodiscard]] std::optional<glm::vec3> focusClosestLootItem(
        uint64_t instance,
        const std::string& bsgId,
        const glm::vec4& colour);

    [[nodiscard]] std::vector<LootEntity> getCacheLoot() const;
    [[nodiscard]] LootCacheSnapshot getCacheSnapshot() const noexcept;

    uint64_t lootListP = 0;
    uint64_t lootListPtr = 0;
    long lootCount = 0;

private:
    struct WantedLookup
    {
        std::unordered_set<std::string> questIds;
        std::unordered_set<std::string> wishlistIds;
        std::unordered_map<std::string, glm::vec4> activeFilterItems;
        std::unordered_set<std::string> categoryLootIds;
    };

private:
    bool buildPointers();
    bool refreshLootListHeader();
    bool buildLootBuffer();

    bool buildNewLootItemsScatter(
        const std::vector<uint64_t>& newPointers,
        std::vector<LootEntity>& outItems
    );

    void classifyObservedLootItemsScatter(std::vector<LootEntity>& items);
    void classifyLootableContainersScatter(std::vector<LootEntity>& items);
    void classifyCorpseLootItems(std::vector<LootEntity>& items);

    void updateExistingLootItems(std::vector<LootEntity>& workingCache);
    void updateLootableContainerStates(std::vector<LootEntity>& workingCache);
    void updateCorpseRequirements(std::vector<LootEntity>& workingCache);

    void scanCorpseEquipment(uint64_t interactive, LootEntity& lootItem, bool update = false);

    [[nodiscard]] WantedLookup buildWantedLookup() const;
    void applyWantedState(LootEntity& lootItem, const WantedLookup& lookup) const;

    [[nodiscard]] bool isContainerEnabled(const std::string& name) const;
    void cleanupMissingLoot(
        std::vector<LootEntity>& workingCache,
        const std::unordered_set<uint64_t>& livePointers
    );

    void markFailed(
        LootEntity& item,
        std::string reason,
        bool retryable = true
    ) const;

    bool tryUpdateLootPosition(
        LootEntity& item,
        bool markAsFailedOnError
    );

    void mergeResolveResults(
        std::vector<LootEntity>& workingCache,
        std::vector<LootEntity>&& results,
        std::chrono::steady_clock::time_point now
    );

private:
    mutable std::shared_mutex lootMutex;

    std::vector<LootEntity> lootList;
    std::atomic<LootCacheSnapshot> publishedLootCache;
    std::vector<uint64_t> loot_buffer;
    std::unordered_set<uint64_t> liveLootPointers;
    std::unordered_set<uint64_t> stagedLootPointers;
    std::unordered_map<uint64_t, std::uint8_t> missingLootDiscoveryCounts;

    size_t lootBufferScanCursor = 0;
    uint64_t lootBufferScanListPtr = 0;

    std::chrono::steady_clock::time_point nextLootDiscovery{};
    std::chrono::steady_clock::time_point lastDogTagUpdate{};

    size_t corpseRefreshCursor = 0;
    size_t dogTagRefreshCursor = 0;
    size_t containerRefreshCursor = 0;

    void publishCacheSnapshotLocked();
};

extern loot Loot;

long GetLootSelectedPrice(long marketPrice, long traderPrice);
long GetLootDisplayPrice(const LootEntity& lootItem);
std::string FormatLootPrice(long price);
std::string GetLootDisplayName(const LootEntity& lootItem);

std::string getContainerName(const std::string& bsgid);
std::string GetQuestItemDisplayName(const std::string& itemId);
LootEntity updateLootDetails(std::string bsgid, LootEntity& item);
