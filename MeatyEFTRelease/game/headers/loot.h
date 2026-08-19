#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <glm/glm.hpp>
#include <unordered_set>
#include <chrono>

struct corpseEquipment
{
    std::string equipmentName;
    int value = 0;
    bool wanted = false;
};

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

struct LootList
{
    uint64_t instance = 0;

    uint64_t m_itemObject = 0;
    uint64_t m_interactiveClass = 0;
    uint64_t m_baseObject = 0;
    uint64_t m_gameObject = 0;
    uint64_t m_pGameObjectName = 0;
    std::string m_objectClassName;
    uint64_t m_objectClass = 0;
    uint64_t m_pointerToTransform1 = 0;
    uint64_t m_pointerToTransform2 = 0;

    glm::vec3 worldLocation{};

    std::string gameObjectName;
    std::string bsgId;
    std::string longName;
    std::string shortName;

    int avgMarketPrice = 0;
    int traderPrice = 0;
    int corpseValue = 0;
    int distance = 0;

    bool isItem = false;
    bool isContainer = false;
    bool isQuestItem = false;
    bool isCorpse = false;
    bool isAirdrop = false;

    std::vector<corpseEquipment> corpseEquip;

    bool wanted = false;
    bool forceWanted = false;
    glm::vec4 color{};

    // Resolution state.
    bool failed = false;
    bool retryableFailure = true;
    bool hasValidPosition = false;
    std::string failureReason;
    bool pendingResolve = false;
    std::uint8_t resolveAttempts = 0;

    std::chrono::steady_clock::time_point nextResolveAttempt{};
    std::chrono::steady_clock::time_point lastPositionUpdate{};
    std::chrono::steady_clock::time_point lastCorpseEquipmentUpdate{};
};

using LootCacheCollection = std::vector<LootList>;
using LootCacheSnapshot = std::shared_ptr<const LootCacheCollection>;

extern std::vector<LootFilters> lootFilters;

class loot
{
public:
    loot();

    void lootTask();
    void clearCache();

    void markLootWanted(const std::vector<uint64_t>& instances, const glm::vec4& colour);
    void setLootWanted(uint64_t instance, bool wanted, const glm::vec4& colour);

    [[nodiscard]] std::vector<LootList> getCacheLoot() const;
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
    };

private:
    bool buildPointers();
    bool refreshLootListHeader();
    bool buildLootBuffer();

    bool buildNewLootItemsScatter(
        const std::vector<uint64_t>& newPointers,
        std::vector<LootList>& outItems
    );

    void classifyObservedLootItemsScatter(std::vector<LootList>& items);
    void classifyLootableContainersScatter(std::vector<LootList>& items);
    void classifyCorpseLootItems(std::vector<LootList>& items);

    void updateExistingLootItems(std::vector<LootList>& workingCache);
    void updateCorpseRequirements(std::vector<LootList>& workingCache);

    void scanCorpseEquipment(uint64_t interactive, LootList& lootItem, bool update = false);

    [[nodiscard]] WantedLookup buildWantedLookup() const;
    void applyWantedState(LootList& lootItem, const WantedLookup& lookup) const;

    [[nodiscard]] bool isContainerEnabled(const std::string& name) const;
    void cleanupMissingLoot(
        std::vector<LootList>& workingCache,
        const std::unordered_set<uint64_t>& livePointers
    ) const;

    void markFailed(
        LootList& item,
        std::string reason,
        bool retryable = true
    ) const;

    bool tryUpdateLootPosition(
        LootList& item,
        bool markAsFailedOnError
    );

    void mergeResolveResults(
        std::vector<LootList>& workingCache,
        std::vector<LootList>&& results,
        std::chrono::steady_clock::time_point now
    );

private:
    mutable std::shared_mutex lootMutex;

    std::vector<LootList> lootList;
    std::atomic<LootCacheSnapshot> publishedLootCache;
    std::vector<uint64_t> loot_buffer;
    std::unordered_set<uint64_t> liveLootPointers;

    std::chrono::steady_clock::time_point nextLootDiscovery{};
    std::chrono::steady_clock::time_point lastDogTagUpdate{};

    size_t corpseRefreshCursor = 0;
    size_t dogTagRefreshCursor = 0;

    void publishCacheSnapshotLocked();
};

extern loot Loot;

std::string getContainerName(const std::string& bsgid);
std::string GetQuestItemDisplayName(const std::string& itemId);
LootList updateLootDetails(std::string bsgid, LootList& item);
