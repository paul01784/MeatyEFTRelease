#include "../../../UI/includes.h"
#include <glm/glm.hpp>
#include "Loot.h"
#include "LootClassifier.h"
#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "../../../Core/Utilities.h"
#include "../MainGame.h"
#include "../../SDK/EftOffsets.h"
#include "../../Unity/UnityContainers.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"
#include <map>
#include "../../Unity/UnityOffsets.h"
#include "../../Unity/Transform.h"
#include "../QuestManager.h"
#include "WishList.h"
#include "../Player/DogTagCache.h"
#include "../RegisteredPlayers.h"

#include <iomanip>
#include <limits>
#include <sstream>
#include <cctype>

loot::loot()
    : publishedLootCache(
        std::make_shared<const LootCacheCollection>())
{
}

std::vector<LootFilters> lootFilters;
loot Loot;

long GetLootSelectedPrice(const long marketPrice, const long traderPrice)
{
    if (lootGlobals::lootValuePriceSource == 1)
        return traderPrice;

    return marketPrice > 0 ? marketPrice : traderPrice;
}

long GetLootValueFilterPrice(const LootEntity& lootItem)
{
    return GetLootSelectedPrice(
        lootItem.avgMarketPrice,
        lootItem.traderPrice);
}

long GetLootDisplayPrice(const LootEntity& lootItem)
{
    return GetLootValueFilterPrice(lootItem);
}

std::string FormatLootPrice(const long price)
{
    if (price <= 0)
        return {};

    if (price < 1000)
        return std::to_string(price);

    if (price < 1000000)
    {
        const long roundedThousands = (price + 500L) / 1000L;
        return std::to_string(roundedThousands) + "k";
    }

    const double millions = static_cast<double>(price) / 1000000.0;
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(millions >= 10.0 ? 0 : 1) << millions << "m";
    return stream.str();
}

std::string GetLootDisplayName(const LootEntity& lootItem)
{
    const std::string& itemName = lootItem.shortName.empty()
        ? lootItem.longName
        : lootItem.shortName;

    if (!lootGlobals::showLootValue)
        return itemName;

    const std::string formattedPrice = FormatLootPrice(GetLootDisplayPrice(lootItem));
    if (formattedPrice.empty())
        return itemName;

    return itemName + " (" + formattedPrice + ")";
}

namespace
{
    bool equalsIgnoreCase(std::string_view left, std::string_view right)
    {
        if (left.size() != right.size())
            return false;

        for (size_t index = 0; index < left.size(); ++index)
        {
            const auto leftCharacter = static_cast<unsigned char>(left[index]);
            const auto rightCharacter = static_cast<unsigned char>(right[index]);

            if (std::tolower(leftCharacter) != std::tolower(rightCharacter))
                return false;
        }

        return true;
    }
}

std::string GetCorpseOwnerLabel(const LootEntity& corpse)
{
    if (!corpse.isCorpse())
        return {};

    const CorpseLootState& corpseState = corpse.getCorpseState();

    if (!corpseState.ownerName.empty())
        return corpseState.ownerName;

    if (!corpse.longName.empty() &&
        !equalsIgnoreCase(corpse.longName, "Corpse"))
        return corpse.longName;

    if (!corpseState.ownerResolved)
        return "Corpse";

    return corpseState.ownerIsPmc ? "PMC" : "Scav";
}

bool ShouldRenderCorpseOwnerLabel(const std::string_view label)
{
    return !label.empty() &&
        !equalsIgnoreCase(label, "Ai") &&
        !equalsIgnoreCase(label, "Scav") &&
        !equalsIgnoreCase(label, "Corpse") &&
        !equalsIgnoreCase(label, "Body");
}

std::string GetRenderableCorpseOwnerLabel(const LootEntity& corpse)
{
    std::string label = GetCorpseOwnerLabel(corpse);
    return ShouldRenderCorpseOwnerLabel(label)
        ? label
        : std::string{};
}

constexpr std::uint8_t MAX_LOOT_RESOLVE_ATTEMPTS = 20;

constexpr std::chrono::milliseconds LOOT_RESOLVE_RETRY_DELAY{
    500
};

namespace
{
    constexpr int MAX_LOOT_COUNT = 12000;
    constexpr size_t MAX_LOOT_BUFFER_BYTES = 64 * 1024;
    constexpr size_t MAX_LOOT_BUFFER_ITEMS =
        MAX_LOOT_BUFFER_BYTES / sizeof(uint64_t);
    constexpr size_t MAX_LOOT_RESOLVE_PER_TICK = 8;
    constexpr size_t MAX_CORPSE_UPDATES_PER_TICK = 1;
    constexpr size_t MAX_CONTAINER_STATE_UPDATES_PER_TICK = 32;
    constexpr std::uint8_t MISSING_LOOT_SCANS_BEFORE_PRUNE = 2;
    constexpr size_t MIN_CACHE_SIZE_FOR_CATASTROPHIC_DROP_GUARD = 32;
    constexpr int MAX_CORPSE_SLOTS = 128;
    constexpr bool ENABLE_LOOT_TRANSFORM_DIAGNOSTICS = false;
    constexpr size_t MAX_OBJECT_NAME_LENGTH = 64;
    constexpr size_t MAX_CLASS_NAME_LENGTH = 64;

    constexpr std::chrono::milliseconds LOOT_DISCOVERY_INTERVAL{
        1000
    };

    const std::unordered_set<std::string> skipNames =
    {
        "Compass",
        "ArmBand",
        "Pockets",
        "SecuredContainer"
    };

    struct LootShellRead
    {
        uint64_t instance = 0;

        uint64_t monoBehaviour = 0;
        uint64_t interactiveClass = 0;
        uint64_t gameObject = 0;
        uint64_t gameObjectNamePtr = 0;
        uint64_t components = 0;
        uint64_t transformComponent = 0;
        uint64_t transformObjectClass = 0;
        uint64_t transformInternal = 0;
    };

    struct ObservedLootRead
    {
        size_t itemIndex = 0;

        uint64_t itemObject = 0;
        uint64_t itemTemplate = 0;

        MongoID mongoId{};
        bool questItem = false;
    };

    struct ContainerLootRead
    {
        size_t itemIndex = 0;

        uint64_t itemOwner = 0;
        uint64_t rootItem = 0;
        uint64_t itemTemplate = 0;

        MongoID mongoId{};
    };

    struct ContainerOpenedRead
    {
        size_t itemIndex = 0;
        uint64_t interactingPlayer = 0;
    };

    struct CorpseSlotRead
    {
        uint64_t slotPtr = 0;

        uint64_t namePtr = 0;
        uint64_t containedItem = 0;
        uint64_t inventoryTemplate = 0;
        uint64_t dogTagComponent = 0;

        int nameLen = 0;
        MongoID mongoId{};
    };

    bool isValidLootPosition(const glm::vec3& position)
    {
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            return false;
        }

        return position.x != 0.0f ||
            position.y != 0.0f ||
            position.z != 0.0f;
    }

    int calculateCorpseValue(const CorpseLootState& corpseState)
    {
        int value = 0;

        for (const auto& entry : corpseState.equipment)
        {
            if (!corpseState.isEquipmentLootable(entry))
                continue;

            value += entry.value;
        }

        return value;
    }

    std::string getCorpseOwnerDisplayName(const Player& player)
    {
        if (player.isBlackDivision)
            return "B.Division";

        if (player.isBoss)
            return !player.name.empty() && player.name != "Ai"
                ? player.name
                : "Boss";

        if (player.isPlayerScav)
            return !player.name.empty()
                ? player.name
                : "PScav";

        if (player.isPlayer && !player.isAi)
            return !player.name.empty()
                ? player.name
                : "PMC";

        if (player.isAi)
            return !player.name.empty() && player.name != "Ai"
                ? player.name
                : "Scav";

        return !player.name.empty()
            ? player.name
            : "Scav";
    }

    bool isGenericCorpseOwnerName(const std::string_view name)
    {
        if (name.empty() ||
            equalsIgnoreCase(name, "PMC") ||
            equalsIgnoreCase(name, "PScav") ||
            equalsIgnoreCase(name, "Scav") ||
            equalsIgnoreCase(name, "Ai") ||
            equalsIgnoreCase(name, "Boss"))
        {
            return true;
        }

        constexpr std::string_view pmcPrefix = "PMC";
        if (name.size() <= pmcPrefix.size())
            return false;

        for (std::size_t index = 0; index < pmcPrefix.size(); ++index)
        {
            if (std::tolower(static_cast<unsigned char>(name[index])) !=
                std::tolower(static_cast<unsigned char>(pmcPrefix[index])))
            {
                return false;
            }
        }

        const unsigned char suffix = static_cast<unsigned char>(name[pmcPrefix.size()]);
        return std::isspace(suffix) || std::isdigit(suffix);
    }

    void setCorpseOwnerName(LootEntity& lootItem, CorpseLootState& corpseState, const std::string_view name, const bool authoritative)
    {
        if (name.empty() ||
            (!authoritative &&
                !corpseState.ownerName.empty() &&
                !isGenericCorpseOwnerName(corpseState.ownerName)))
        {
            return;
        }

        corpseState.ownerName = name;
        lootItem.longName = name;
    }

    bool updateCorpseOwnerFromDogTagCache(LootEntity& lootItem)
    {
        CorpseLootState& corpseState = lootItem.getCorpseState();

        if (corpseState.ownerProfileId.empty())
            return false;

        const auto cachedDogTag = g_dogTagCache.GetByProfileId(corpseState.ownerProfileId);

        if (!cachedDogTag.has_value())
            return false;

        const std::string nickname = TrimEFT(cachedDogTag->nickname);
        if (nickname.empty())
            return false;

        corpseState.ownerResolved = true;
        corpseState.ownerIsPmc = true;
        setCorpseOwnerName(lootItem, corpseState, nickname, true);
        return true;
    }

    bool updateCorpseOwnerFromDogTag(LootEntity& lootItem, const std::uint64_t dogTagComponent)
    {
        if (!Utils::valid_pointer(dogTagComponent))
            return false;

        CorpseLootState& corpseState = lootItem.getCorpseState();
        const std::string profileId = TrimEFT(mem.readUnityStringField(
            dogTagComponent + sdk::DogtagComponent::ProfileId,
            256,
            DmaCacheMode::Uncached));
        const std::string nickname = TrimEFT(mem.readUnityStringField(
            dogTagComponent + sdk::DogtagComponent::Nickname,
            256,
            DmaCacheMode::Uncached));

        if (profileId.empty() && nickname.empty())
            return false;

        corpseState.ownerResolved = true;
        corpseState.ownerIsPmc = true;

        if (!profileId.empty())
            corpseState.ownerProfileId = profileId;

        if (!nickname.empty())
        {
            setCorpseOwnerName(lootItem, corpseState, nickname, true);
            return true;
        }

        return updateCorpseOwnerFromDogTagCache(lootItem);
    }

    bool corpsePointerMatchesPlayer(const LootEntity& lootItem, const Player& player)
    {
        if (!Utils::valid_pointer(lootItem.m_interactiveClass) || !Utils::valid_pointer(player.P_CorpseClass))
        {
            return false;
        }

        return lootItem.m_interactiveClass == player.P_CorpseClass;
    }

    bool updateCorpseOwnerFromPlayerCache(LootEntity& lootItem, const PlayerCollection& playerCache)
    {
        CorpseLootState& corpseState = lootItem.getCorpseState();

        for (const Player& player : playerCache)
        {
            if (!corpsePointerMatchesPlayer(lootItem, player))
                continue;

            const bool ownerIsPmc =
                player.isPlayer &&
                !player.isPlayerScav &&
                !player.isAi;

            corpseState.ownerResolved = true;
            corpseState.ownerIsPmc = ownerIsPmc;

            if (!player.profileId.empty())
                corpseState.ownerProfileId = player.profileId;

            if (updateCorpseOwnerFromDogTagCache(lootItem))
                return true;

            const std::string ownerName = getCorpseOwnerDisplayName(player);

            if (!ownerName.empty())
                setCorpseOwnerName(lootItem, corpseState, ownerName, false);

            return true;
        }

        return false;
    }

    struct CachedMarketItem
    {
        std::string name;
        std::string shortName;
        long traderPrice = 0;
        long marketPrice = 0;
    };

    const CachedMarketItem* findMarketItem(const std::string& bsgid)
    {
        static std::unordered_map<std::string, CachedMarketItem> itemById;
        static std::uint64_t indexedRevision = 0;
        static bool hasIndex = false;
        const std::uint64_t marketRevision = marketListRevision.load(std::memory_order_acquire);

        if (!hasIndex || indexedRevision != marketRevision)
        {
            itemById.clear();
            itemById.reserve(marketList.size());

            for (const auto& marketItem : marketList)
            {
                if (marketItem.bsgid.empty())
                    continue;

                itemById.insert_or_assign(
                    marketItem.bsgid,
                    CachedMarketItem{
                        marketItem.name,
                        marketItem.shortName,
                        marketItem.traderPrice,
                        marketItem.marketPrice
                    }
                );
            }

            indexedRevision = marketRevision;
            hasIndex = true;
        }

        const auto it = itemById.find(bsgid);
        return it != itemById.end() ? &it->second : nullptr;
    }

    bool tryGetBattlePassInfoDocumentName(std::string_view gameObjectName, std::string& displayName)
    {
        constexpr std::string_view prefix = "item_barter_info_";

        std::string normalized;
        normalized.reserve(gameObjectName.size());

        for (const unsigned char character : gameObjectName)
            normalized.push_back(static_cast<char>(std::tolower(character)));

        constexpr std::string_view cloneSuffix = "(clone)";
        const size_t clonePosition = normalized.find(cloneSuffix);
        if (clonePosition != std::string::npos)
            normalized.erase(clonePosition);

        while (!normalized.empty() &&
            std::isspace(static_cast<unsigned char>(normalized.back())))
        {
            normalized.pop_back();
        }

        if (!normalized.starts_with(prefix) || normalized.size() == prefix.size())
            return false;

        const std::string_view documentType = std::string_view(normalized).substr(prefix.size());

        displayName.clear();
        displayName.reserve(documentType.size() + sizeof(" document"));

        bool capitalizeNext = true;
        for (const char character : documentType)
        {
            if (character == '_')
            {
                displayName.push_back(' ');
                capitalizeNext = true;
                continue;
            }

            displayName.push_back(capitalizeNext
                ? static_cast<char>(std::toupper(static_cast<unsigned char>(character)))
                : character);
            capitalizeNext = false;
        }

        displayName += " doc";
        return true;
    }

    bool isBattlePassInfoDocument(const LootEntity& item)
    {
        std::string displayName;
        return tryGetBattlePassInfoDocumentName(item.gameObjectName, displayName);
    }

    bool applyMarketDetails(const std::string& bsgid, LootEntity& item)
    {
        if (const CachedMarketItem* marketItem = findMarketItem(bsgid))
        {
            item.longName = marketItem->name;
            item.shortName = marketItem->shortName;
            item.traderPrice = static_cast<int>(marketItem->traderPrice);
            item.avgMarketPrice = static_cast<int>(marketItem->marketPrice);

            return true;
        }

        std::string infoDocumentName;
        if (tryGetBattlePassInfoDocumentName(item.gameObjectName, infoDocumentName))
        {
            item.shortName = infoDocumentName;
            item.longName = infoDocumentName;
            return false;
        }

        if (item.shortName.empty())
            item.shortName = item.gameObjectName;

        if (item.longName.empty())
            item.longName = item.gameObjectName;

        return false;
    }
}

LootEntity updateLootDetails(std::string bsgid, LootEntity& item)
{
    applyMarketDetails(bsgid, item);
    return item;
}

std::string getContainerName(const std::string& bsgid)
{
    static const std::unordered_map<std::string, std::string> containerNames =
    {
        { "578f87a3245977356274f2cb", "Duffle Bag" },
        { "578f87b7245977356274f2cd", "Drawer" },
        { "578f8782245977354405a1e3", "Safe" },

        { "5909d89086f77472591234a0", "Weapon Box" },
        { "5909d7cf86f77470ee57d75a", "Weapon Box" },
        { "5909d76c86f77471e53d2adf", "Weapon Box" },
        { "5909d5ef86f77467974efbd8", "Weapon Box" },

        { "5d6fd45b86f774317075ed43", "Technical Crate" },
        { "5d6fd13186f77424ad2a8c69", "Ration Crate" },
        { "5d6fe50986f77449d97f7463", "Medical Crate" },

        { "578f8778245977358849a9b5", "Jacket" },
        { "5937ef2b86f77408a47244b3", "Jacket" },
        { "59387ac686f77401442ddd61", "Jacket" },

        { "5909d4c186f7746ad34e805a", "Med Package" },
        { "5909d24f86f77466f56e6855", "Med Box" },
        { "5909d50c86f774659e6aaebe", "Toolbox" },
        { "5909d36d86f774660f0bb900", "Grenade Box" },

        { "5d6d2bb386f774785b07a77a", "Buried Stash" },
        { "5d6d2b5486f774785c2ba8ea", "Ground Cache" },
        { "578f87ad245977356274f2cc", "Wooden Crate" },
        { "5c052cea86f7746b2101e8d8", "Suitcase" },
        { "5909d45286f77465a8136dc6", "Ammo Box" },
        { "5909e4b686f7747f5b744fa4", "Dead Body" },
        { "59139c2186f77411564f8e42", "PC Block" },
        { "578f879c24597735401e6bc6", "Register" },

        { "6582e6c6edf14c4c6023adf2", "Dead Body" },
        { "6582e6d7b14c3f72eb071420", "Dead Body" }
    };

    const auto it = containerNames.find(bsgid);

    if (it != containerNames.end())
        return it->second;

    return bsgid;
}

std::string GetQuestItemDisplayName(const std::string& itemId)
{
    if (itemId.empty())
        return "";

    static std::unordered_map<std::string, std::string> questNameByItemId;

    const auto cachedIt = questNameByItemId.find(itemId);
    if (cachedIt != questNameByItemId.end())
        return cachedIt->second;

    for (const auto& task : tarkovDevTasksData)
    {
        for (const auto& obj : task.objectives)
        {
            if (!obj.questItemId.empty() && obj.questItemId == itemId)
            {
                questNameByItemId.insert_or_assign(itemId, task.qName);
                return task.qName;
            }
        }
    }

    return "";
}

std::vector<LootEntity> loot::getCacheLoot() const
{
    return *getCacheSnapshot();
}

LootCacheSnapshot loot::getCacheSnapshot() const noexcept
{
    LootCacheSnapshot snapshot = publishedLootCache.load(std::memory_order_acquire);

    if (snapshot)
        return snapshot;

    static const LootCacheSnapshot emptySnapshot = std::make_shared<const LootCacheCollection>();

    return emptySnapshot;
}

void loot::publishCacheSnapshotLocked()
{
    publishedLootCache.store(
        std::make_shared<const LootCacheCollection>(lootList),
        std::memory_order_release);
}

void loot::clearCache()
{
    std::unique_lock lock(lootMutex);

    lootList.clear();
    loot_buffer.clear();
    liveLootPointers.clear();
    stagedLootPointers.clear();
    missingLootDiscoveryCounts.clear();

    lootListP = 0;
    lootListPtr = 0;
    lootCount = 0;
    lootBufferScanCursor = 0;
    lootBufferScanListPtr = 0;

    nextLootDiscovery = {};
    lastDogTagUpdate = {};
    publishCacheSnapshotLocked();
    corpseRefreshCursor = 0;
    dogTagRefreshCursor = 0;
    containerRefreshCursor = 0;
}

void loot::markFailed(
    LootEntity& item,
    std::string reason,
    const bool retryable) const
{
    item.failed = true;
    item.retryableFailure = retryable;
    item.failureReason = std::move(reason);
    item.wanted = false;
}

void loot::setLootWanted(const uint64_t instance, const bool wanted, const glm::vec4& colour)
{
    if (instance == 0)
        return;

    const WantedLookup lookup = buildWantedLookup();

    std::unique_lock<std::shared_mutex> lock(lootMutex);

    const auto it = std::find_if(
        lootList.begin(),
        lootList.end(),
        [instance](const LootEntity& item)
        {
            return item.instance == instance;
        }
    );

    if (it == lootList.end())
        return;

    
    applyWantedState(*it, lookup);

    if (wanted && it->filterWanted)
    {
        publishCacheSnapshotLocked();
        return;
    }

    it->forceWanted = wanted;

    if (wanted)
    {
        it->forceColor = colour;
    }

    applyWantedState(*it, lookup);
    publishCacheSnapshotLocked();
}

std::optional<glm::vec3> loot::focusClosestLootItem(const uint64_t instance, const std::string& bsgId, const glm::vec4& colour)
{
    if (instance == 0 && bsgId.empty())
        return std::nullopt;

    const WantedLookup lookup = buildWantedLookup();
    std::unique_lock<std::shared_mutex> lock(lootMutex);

    const auto isMatch = [instance, &bsgId](const LootEntity& item)
    {
        return !bsgId.empty()
            ? item.bsgId == bsgId
            : item.instance == instance;
    };

    LootEntity* closest = nullptr;
    float closestDistanceSquared = std::numeric_limits<float>::max();

    for (LootEntity& item : lootList)
    {
        if (!isMatch(item))
            continue;

        applyWantedState(item, lookup);

        if (item.forceWanted)
        {
            item.forceWanted = false;
            applyWantedState(item, lookup);
        }

        if (item.pendingResolve || item.failed || !item.hasValidPosition)
            continue;

        const glm::vec3 difference = item.worldLocation - mainGame.localLocation;
        const float distanceSquared =
            difference.x * difference.x +
            difference.y * difference.y +
            difference.z * difference.z;

        if (distanceSquared < closestDistanceSquared)
        {
            closestDistanceSquared = distanceSquared;
            closest = &item;
        }
    }

    if (!closest)
    {
        publishCacheSnapshotLocked();
        return std::nullopt;
    }

    if (!closest->filterWanted)
    {
        closest->forceWanted = true;
        closest->forceColor = colour;
        applyWantedState(*closest, lookup);
    }

    publishCacheSnapshotLocked();
    return closest->worldLocation;
}

bool loot::tryUpdateLootPosition(LootEntity& item, bool markAsFailedOnError)
{
    item.lastPositionUpdate = std::chrono::steady_clock::now();

    const auto invalidateAirdropPosition = [&item]()
        {
            if (!item.isAirdrop())
                return;

            item.worldLocation = {};
            item.hasValidPosition = false;
        };

    auto LogTransformDebug = [&](const char* stage, const char* reason,
        const glm::vec3* position = nullptr)
        {
            if (!ENABLE_LOOT_TRANSFORM_DIAGNOSTICS)
                return;

            const uint64_t transformPtr =
                item.m_pointerToTransform1;

            int transformIndex = -999999;
            int parentIndex = -999999;

            uint64_t hierarchyPtr = 0;
            uint64_t verticesPtr = 0;
            uint64_t indicesPtr = 0;

            try
            {
                if (Utils::valid_pointer(transformPtr))
                {
                    transformIndex = mem.Read<int>(
                        transformPtr +
                        UnityOffsets::TransformAccess_IndexOffset,
                        DmaCacheMode::Uncached
                    );

                    hierarchyPtr = mem.Read<uint64_t>(
                        transformPtr +
                        UnityOffsets::TransformAccess_HierarchyOffset,
                        DmaCacheMode::Uncached
                    );

                    if (Utils::valid_pointer(hierarchyPtr))
                    {
                        verticesPtr = mem.Read<uint64_t>(
                            hierarchyPtr +
                            UnityOffsets::Hierarchy_VerticesOffset,
                            DmaCacheMode::Uncached
                        );

                        indicesPtr = mem.Read<uint64_t>(
                            hierarchyPtr +
                            UnityOffsets::Hierarchy_IndicesOffset,
                            DmaCacheMode::Uncached
                        );

                        if (Utils::valid_pointer(indicesPtr) &&
                            transformIndex >= 0 &&
                            transformIndex < 1'000'000)
                        {
                            parentIndex = mem.Read<int>(
                                indicesPtr +
                                static_cast<uint64_t>(
                                    transformIndex
                                    ) * sizeof(int),
                                DmaCacheMode::Uncached
                            );
                        }
                    }
                }
            }
            catch (...)
            {
            }

            std::ostringstream ss;

            ss << "[LOOT][TRANSFORM] "
                << stage
                << " | "
                << reason
                << " | instance=0x"
                << std::hex << item.instance
                << " | transform=0x"
                << transformPtr
                << " | hierarchy=0x"
                << hierarchyPtr
                << " | vertices=0x"
                << verticesPtr
                << " | indices=0x"
                << indicesPtr
                << std::dec
                << " | index="
                << transformIndex
                << " | parent="
                << parentIndex
                << " | name='"
                << item.gameObjectName
                << "'";

            if (position)
            {
                ss << " | position=("
                    << position->x << ", "
                    << position->y << ", "
                    << position->z << ")";
            }

            LOGS.logError(ss.str());
        };

    if (!Utils::valid_pointer(item.m_pointerToTransform1))
    {
        LogTransformDebug(
            "PreCheck",
            "Invalid transform pointer"
        );

        if (markAsFailedOnError)
            markFailed(item, "Invalid transform pointer");
        else
            invalidateAirdropPosition();

        return false;
    }

    try
    {
        uint64_t nativeTransform = 0;

        if (!UnityTransform::TryResolveNative(
                item.m_pointerToTransform1,
                nativeTransform,
                false))
        {
            LogTransformDebug(
                "Resolve",
                "Unable to resolve native transform"
            );

            if (markAsFailedOnError)
                markFailed(item, "Unable to resolve native transform");
            else
                invalidateAirdropPosition();

            return false;
        }

        UnityTransform transform(
            nativeTransform,
            false
        );

        if (!transform.IsValid())
        {
            LogTransformDebug(
                "Construct",
                "Invalid transform hierarchy"
            );

            if (markAsFailedOnError)
                markFailed(item, "Invalid transform hierarchy");
            else
                invalidateAirdropPosition();

            return false;
        }

        const glm::vec3 newPosition =
            transform.UpdatePosition();

        if (!transform.IsValid())
        {
            LogTransformDebug(
                "UpdatePosition",
                "Transform became invalid while resolving parent chain",
                &newPosition
            );

            if (markAsFailedOnError)
            {
                markFailed(
                    item,
                    "Transform invalid during position update"
                );
            }
            else
            {
                invalidateAirdropPosition();
            }

            return false;
        }

        if (!isValidLootPosition(newPosition))
        {
            LogTransformDebug(
                "UpdatePosition",
                "Transform returned invalid world position",
                &newPosition
            );

            if (markAsFailedOnError)
            {
                markFailed(
                    item,
                    "Transform returned invalid position"
                );
            }
            else
            {
                invalidateAirdropPosition();
            }

            return false;
        }

        item.worldLocation = newPosition;
        item.hasValidPosition = true;

        return true;
    }
    catch (const std::exception& e)
    {
        LogTransformDebug(
            "Exception",
            e.what()
        );

        if (markAsFailedOnError)
        {
            markFailed(
                item,
                "Transform exception: " +
                std::string(e.what())
            );
        }
        else
        {
            invalidateAirdropPosition();
        }

        return false;
    }
    catch (...)
    {
        LogTransformDebug(
            "Exception",
            "Unknown transform exception"
        );

        if (markAsFailedOnError)
            markFailed(item, "Unknown transform exception");
        else
            invalidateAirdropPosition();

        return false;
    }
}

void loot::mergeResolveResults(
    std::vector<LootEntity>& workingCache,
    std::vector<LootEntity>&& results,
    const std::chrono::steady_clock::time_point now)
{
    std::unordered_map<uint64_t, size_t> cacheIndex;
    cacheIndex.reserve(workingCache.size() + results.size());

    for (size_t i = 0; i < workingCache.size(); ++i)
    {
        if (Utils::valid_pointer(workingCache[i].instance))
            cacheIndex.emplace(workingCache[i].instance, i);
    }

    for (auto& result : results)
    {
        if (!Utils::valid_pointer(result.instance))
            continue;

        const auto existingIt = cacheIndex.find(result.instance);

        LootEntity* previousItem = nullptr;
        std::uint8_t previousAttempts = 0;

        if (existingIt != cacheIndex.end())
        {
            previousItem = &workingCache[existingIt->second];
            previousAttempts = previousItem->resolveAttempts;

            if (!Utils::valid_pointer(result.m_interactiveClass))
                result.m_interactiveClass = previousItem->m_interactiveClass;

            if (!Utils::valid_pointer(result.m_gameObject))
                result.m_gameObject = previousItem->m_gameObject;

            if (!Utils::valid_pointer(result.m_pGameObjectName))
                result.m_pGameObjectName = previousItem->m_pGameObjectName;

            if (!Utils::valid_pointer(result.m_pointerToTransform1))
                result.m_pointerToTransform1 = previousItem->m_pointerToTransform1;

            if (result.m_objectClassName.empty())
                result.m_objectClassName = previousItem->m_objectClassName;

            if (result.gameObjectName.empty())
                result.gameObjectName = previousItem->gameObjectName;

            if (result.bsgId.empty())
                result.bsgId = previousItem->bsgId;

            if (result.shortName.empty())
                result.shortName = previousItem->shortName;

            if (result.longName.empty())
                result.longName = previousItem->longName;

            if (!result.hasValidPosition && previousItem->hasValidPosition)
            {
                result.worldLocation = previousItem->worldLocation;
                result.hasValidPosition = true;
            }

            if (result.kind == LootEntityKind::Unknown)
            {
                LootClassifier::initialize(result, previousItem->kind);
                result.state = previousItem->state;
            }

            if (result.lastPositionUpdate ==
                std::chrono::steady_clock::time_point{})
            {
                result.lastPositionUpdate =
                    previousItem->lastPositionUpdate;
            }
        }

        const bool attemptFailed = result.failed;
        const bool retryableFailure = result.retryableFailure;

        result.resolveAttempts = static_cast<std::uint8_t>(
            std::min<int>(
                static_cast<int>(previousAttempts) + 1,
                MAX_LOOT_RESOLVE_ATTEMPTS
            )
            );

        if (attemptFailed)
        {
            result.wanted = false;

            if (!retryableFailure ||
                result.resolveAttempts >= MAX_LOOT_RESOLVE_ATTEMPTS)
            {
                result.pendingResolve = false;
                result.failed = true;
                result.nextResolveAttempt = {};
            }
            else
            {
                //retry it later.
                result.pendingResolve = true;
                result.failed = false;
                result.nextResolveAttempt =
                    now + LOOT_RESOLVE_RETRY_DELAY;
            }
        }
        else
        {
            //Success
            result.pendingResolve = false;
            result.failed = false;
            result.retryableFailure = true;
            result.failureReason.clear();
            result.nextResolveAttempt = {};
        }

        if (previousItem)
        {
            *previousItem = std::move(result);
        }
        else
        {
            const size_t newIndex = workingCache.size();

            cacheIndex.emplace(result.instance, newIndex);
            workingCache.emplace_back(std::move(result));
        }
    }
}

bool loot::buildPointers()
{
    if (Utils::valid_pointer(lootListP))
        return true;

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return false;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        uint64_t nextLootList = 0;

        if (!mem.TryRead<uint64_t>(
            mainGame.localGameWorld +
                sdk::ClientLocalGameWorld::LootList,
            nextLootList,
            DmaCacheMode::Uncached) ||
            !Utils::valid_pointer(nextLootList))
        {
            if (attempt < 2)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }

        lootListP = nextLootList;
        return true;
    }

    return false;
}

bool loot::refreshLootListHeader()
{
    if (!Utils::valid_pointer(lootListP))
        return false;

    uint64_t nextLootListPtr = 0;
    int nextLootCount = 0;

    ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

    if (!batch.Add(lootListP + 0x10, nextLootListPtr) ||
        !batch.Add(lootListP + 0x18, nextLootCount) ||
        !batch.Execute())
    {
        return false;
    }

    if (!Utils::valid_pointer(nextLootListPtr))
        return false;

    if (nextLootCount <= 0 || nextLootCount > MAX_LOOT_COUNT)
        return false;

    // Publish the pair only after both values were read and validated.
    lootListPtr = nextLootListPtr;
    lootCount = nextLootCount;

    if (lootBufferScanListPtr != nextLootListPtr ||
        static_cast<size_t>(nextLootCount) < lootBufferScanCursor)
    {
        lootBufferScanCursor = 0;
        stagedLootPointers.clear();
    }

    lootBufferScanListPtr = nextLootListPtr;

    return true;
}

bool loot::buildLootBuffer()
{
    if (lootCount <= 0 || lootCount > MAX_LOOT_COUNT)
        return false;

    if (!Utils::valid_pointer(lootListPtr))
        return false;

    const size_t itemCount = static_cast<size_t>(lootCount);

    if (lootBufferScanCursor >= itemCount)
    {
        lootBufferScanCursor = 0;
        stagedLootPointers.clear();
    }

    const size_t itemsToRead = std::min(MAX_LOOT_BUFFER_ITEMS, itemCount - lootBufferScanCursor);

    if (itemsToRead == 0)
        return false;

    loot_buffer.assign(itemsToRead, 0);

    const size_t bytes = sizeof(uint64_t) * itemsToRead;
    const uint64_t bufferAddress = lootListPtr + 0x20 + (sizeof(uint64_t) * lootBufferScanCursor);

    if (!mem.Read(
            bufferAddress,
            loot_buffer.data(),
            bytes,
            DmaCacheMode::Uncached,
            "Loot pointer buffer"))
        return false;

    lootBufferScanCursor += itemsToRead;

    return true;
}

bool loot::buildNewLootItemsScatter(
    const std::vector<uint64_t>& newPointers,
    std::vector<LootEntity>& outItems)
{
    outItems.clear();

    if (newPointers.empty())
        return true;

    std::vector<LootShellRead> shellReads;
    std::vector<LootEntity> candidates;

    shellReads.reserve(newPointers.size());
    candidates.reserve(newPointers.size());

    for (const uint64_t pointer : newPointers)
    {
        if (!Utils::valid_pointer(pointer))
            continue;

        LootShellRead shell{};
        shell.instance = pointer;
        shellReads.emplace_back(shell);

        LootEntity item{};
        item.instance = pointer;
        candidates.emplace_back(std::move(item));
    }

    if (shellReads.empty())
        return true;

    // MonoBehaviour.
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& shell : shellReads)
            batch.Add(shell.instance + 0x10, shell.monoBehaviour);

        if (!batch.Execute())
        {
            for (auto& item : candidates)
                markFailed(item, "MonoBehaviour scatter execution failed");

            outItems = std::move(candidates);
            return true;
        }
    }

    // interactive class and GameObject.
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.monoBehaviour))
                continue;

            batch.Add(
                shell.monoBehaviour + UnityOffsets::Component_ObjectClassOffset,
                shell.interactiveClass
            );

            batch.Add(
                shell.monoBehaviour + UnityOffsets::Component_GameObjectOffset,
                shell.gameObject
            );
        }

        if (!batch.Execute())
        {
            for (auto& item : candidates)
                markFailed(item, "Object pointer scatter execution failed");

            outItems = std::move(candidates);
            return true;
        }
    }

    // name pointer and components.
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.gameObject))
                continue;

            batch.Add(
                shell.gameObject + UnityOffsets::GameObject_NameOffset,
                shell.gameObjectNamePtr
            );

            batch.Add(
                shell.gameObject + UnityOffsets::GameObject_ComponentsOffset,
                shell.components
            );
        }

        if (!batch.Execute())
        {
            for (auto& item : candidates)
                markFailed(item, "GameObject scatter execution failed");

            outItems = std::move(candidates);
            return true;
        }
    }

    // transform component.
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.components))
                continue;

            batch.Add(shell.components + 0x8, shell.transformComponent);
        }

        if (!batch.Execute())
        {
            for (size_t i = 0; i < candidates.size(); ++i)
                shellReads[i].transformComponent = 0;
        }
    }

    // transform object class.
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.transformComponent))
                continue;

            batch.Add(
                shell.transformComponent +
                UnityOffsets::Component_ObjectClassOffset,
                shell.transformObjectClass
            );
        }

        if (!batch.Execute())
        {
            for (auto& shell : shellReads)
                shell.transformObjectClass = 0;
        }
    }

    // native transform access.
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.transformObjectClass))
                continue;

            batch.Add(
                shell.transformObjectClass + 0x10,
                shell.transformInternal
            );
        }

        if (!batch.Execute())
        {
            for (auto& shell : shellReads)
                shell.transformInternal = 0;
        }
    }

    // item data.
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        LootEntity& item = candidates[i];
        const LootShellRead& shell = shellReads[i];

        item.m_interactiveClass = shell.interactiveClass;
        item.m_gameObject = shell.gameObject;
        item.m_pGameObjectName = shell.gameObjectNamePtr;
        item.m_pointerToTransform1 = shell.transformInternal;

        if (!Utils::valid_pointer(shell.monoBehaviour))
        {
            markFailed(item, "Invalid MonoBehaviour pointer");
            continue;
        }

        if (!Utils::valid_pointer(shell.interactiveClass))
        {
            markFailed(item, "Invalid interactive class pointer");
            continue;
        }

        if (!Utils::valid_pointer(shell.gameObject))
        {
            markFailed(item, "Invalid GameObject pointer");
            continue;
        }

        if (!Utils::valid_pointer(shell.gameObjectNamePtr))
        {
            markFailed(item, "Invalid GameObject name pointer");
            continue;
        }

        try
        {
            item.m_objectClassName = ReadName(
                item.instance,
                MAX_CLASS_NAME_LENGTH,
                false
            );

            item.gameObjectName = mem.readString(
                item.m_pGameObjectName,
                MAX_OBJECT_NAME_LENGTH,
                DmaCacheMode::Uncached
            );
        }
        catch (const std::exception& e)
        {
            markFailed(
                item,
                "Name resolution exception: " + std::string(e.what())
            );

            continue;
        }
        catch (...)
        {
            markFailed(item, "Unknown name resolution exception");
            continue;
        }

        if (item.m_objectClassName.empty())
        {
            markFailed(item, "Empty object class name");
            continue;
        }

        if (item.gameObjectName.empty())
        {
            markFailed(item, "Empty GameObject name");
            continue;
        }

        if (Utils::Text::containsIgnoreCase(item.gameObjectName, "script"))
        {
            markFailed(item, "Skipped script object", false);
            continue;
        }
    }

    classifyObservedLootItemsScatter(candidates);
    classifyLootableContainersScatter(candidates);
    classifyCorpseLootItems(candidates);

    for (auto& item : candidates)
    {
        if (!item.failed)
        {
            const bool recognized =
                item.isItem() ||
                item.isQuestItem() ||
                item.isContainer() ||
                item.isCorpse();

            if (!recognized)
            {
                markFailed(
                    item,
                    "Unsupported class: " + item.m_objectClassName,
                    false
                );
            }
        }

        if (!item.failed)
        {
            tryUpdateLootPosition(item, !item.isAirdrop());
        }

        outItems.emplace_back(std::move(item));
    }

    return true;
}

void loot::classifyObservedLootItemsScatter(std::vector<LootEntity>& items)
{
    std::vector<ObservedLootRead> reads;
    reads.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        if (items[i].failed)
            continue;

        if (items[i].m_objectClassName != "ObservedLootItem")
            continue;

        ObservedLootRead read{};
        read.itemIndex = i;
        reads.emplace_back(read);
    }

    if (reads.empty())
        return;

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            LootEntity& item = items[read.itemIndex];

            batch.Add(
                item.m_interactiveClass + sdk::InteractiveLootItem::Item,
                read.itemObject
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Item object scatter failed");

            return;
        }
    }

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemObject))
                continue;

            batch.Add(
                read.itemObject + sdk::LootItem::Template,
                read.itemTemplate
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Item template scatter failed");

            return;
        }
    }

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            batch.Add(
                read.itemTemplate + sdk::ItemTemplate::_id,
                read.mongoId
            );

            batch.Add(
                read.itemTemplate + sdk::ItemTemplate::QuestItem,
                read.questItem
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Item metadata scatter failed");

            return;
        }
    }

    for (auto& read : reads)
    {
        LootEntity& item = items[read.itemIndex];

        if (!Utils::valid_pointer(read.itemObject))
        {
            markFailed(item, "Invalid item object pointer");
            continue;
        }

        if (!Utils::valid_pointer(read.itemTemplate))
        {
            markFailed(item, "Invalid item template pointer");
            continue;
        }

        item.m_itemObject = read.itemObject;

        try
        {
            item.bsgId = TrimEFT(
                read.mongoId.ReadString(mem, 128, false)
            );
        }
        catch (...)
        {
            markFailed(item, "Item ID resolution exception");
            continue;
        }

        if (item.bsgId.empty())
        {
            markFailed(item, "Empty item ID");
            continue;
        }

        if (read.questItem)
        {
            LootClassifier::initialize(item, LootEntityKind::QuestItem);

            const std::string questName =
                GetQuestItemDisplayName(item.bsgId);

            item.shortName = !questName.empty()
                ? questName
                : item.gameObjectName;

            item.longName = item.gameObjectName;
        }
        else
        {
            LootClassifier::initialize(item, LootEntityKind::Item);

            applyMarketDetails(item.bsgId, item);
        }
    }
}

void loot::classifyLootableContainersScatter(std::vector<LootEntity>& items)
{
    std::vector<ContainerLootRead> reads;
    reads.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        LootEntity& item = items[i];

        if (item.failed)
            continue;

        if (item.m_objectClassName != "LootableContainer")
            continue;

        LootClassifier::initialize(item, LootEntityKind::Container);

        if (Utils::Text::containsIgnoreCase(
            item.gameObjectName,
            "loot_collider"))
        {
            LootClassifier::initialize(item, LootEntityKind::Airdrop);
            item.shortName = "AirDrop";
            item.longName = "AirDrop";
            continue;
        }

        ContainerLootRead read{};
        read.itemIndex = i;
        reads.emplace_back(read);
    }

    if (reads.empty())
        return;

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            LootEntity& item = items[read.itemIndex];

            batch.Add(
                item.m_interactiveClass + sdk::LootableContainer::ItemOwner,
                read.itemOwner
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container owner scatter failed");

            return;
        }
    }

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemOwner))
                continue;

            batch.Add(
                read.itemOwner + sdk::LootableContainerItemOwner::RootItem,
                read.rootItem
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container root item scatter failed");

            return;
        }
    }

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.rootItem))
                continue;

            batch.Add(
                read.rootItem + sdk::LootItem::Template,
                read.itemTemplate
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container template scatter failed");

            return;
        }
    }

    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            batch.Add(
                read.itemTemplate + sdk::ItemTemplate::_id,
                read.mongoId
            );
        }

        if (!batch.Execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container ID scatter failed");

            return;
        }
    }

    for (auto& read : reads)
    {
        LootEntity& item = items[read.itemIndex];

        if (!Utils::valid_pointer(read.itemOwner))
        {
            markFailed(item, "Invalid container item owner");
            continue;
        }

        if (!Utils::valid_pointer(read.rootItem))
        {
            markFailed(item, "Invalid container root item");
            continue;
        }

        if (!Utils::valid_pointer(read.itemTemplate))
        {
            markFailed(item, "Invalid container item template");
            continue;
        }

        try
        {
            item.bsgId = TrimEFT(
                read.mongoId.ReadString(mem, 128, false)
            );
        }
        catch (...)
        {
            markFailed(item, "Container ID resolution exception");
            continue;
        }

        if (item.bsgId.empty())
        {
            markFailed(item, "Empty container ID");
            continue;
        }

        if (!applyMarketDetails(item.bsgId, item))
        {
            item.shortName = getContainerName(item.bsgId);
            item.longName = item.shortName;
        }
    }
}

void loot::classifyCorpseLootItems(std::vector<LootEntity>& items)
{
    for (auto& item : items)
    {
        if (item.failed)
            continue;

        if (item.m_objectClassName != "Corpse" &&
            item.m_objectClassName != "ObservedCorpse")
        {
            continue;
        }

        LootClassifier::initialize(item, LootEntityKind::Corpse);

        item.bsgId.clear();
        item.shortName = "Corpse";

        scanCorpseEquipment(item.m_interactiveClass, item, false);
        item.getCorpseState().value = calculateCorpseValue(item.getCorpseState());
    }
}

loot::WantedLookup loot::buildWantedLookup() const
{
    WantedLookup lookup{};

    if (lootGlobals::enableQuestLoot)
    {
        const std::vector<std::string> questItems = GetMasterItemsSnapshot();

        for (const auto& questItem : questItems)
        {
            if (!questItem.empty())
                lookup.questIds.insert(questItem);
        }
    }

    if (lootGlobals::enableWishListLoot)
    {
        for (const auto& wishlistItem : wishListData)
        {
            if (!wishlistItem.bsgId.empty())
                lookup.wishlistIds.insert(wishlistItem.bsgId);
        }
    }

    for (const auto& filter : lootFilters)
    {
        if (!filter.active)
            continue;

        for (const auto& filterItem : filter.lootItems)
        {
            if (filterItem.bsgid.empty())
                continue;

            lookup.activeFilterItems.emplace(filterItem.bsgid, filter.filterColour);
        }
    }

    if (!lootGlobals::selectedLootCategories.empty())
    {
        const std::unordered_set<std::string> selectedCategories(
            lootGlobals::selectedLootCategories.begin(),
            lootGlobals::selectedLootCategories.end());

        lookup.battlePassInfoDocumentsSelected = selectedCategories.contains("Battle Pass");

        lookup.categoryLootIds.reserve(marketList.size());

        for (const auto& marketItem : marketList)
        {
            const bool categoryMatches = std::any_of(
                marketItem.bsgCategory.begin(),
                marketItem.bsgCategory.end(),
                [&selectedCategories](const std::string& category)
                {
                    return selectedCategories.contains(category);
                });

            if (categoryMatches)
                lookup.categoryLootIds.insert(marketItem.bsgid);
        }
    }

    return lookup;
}

void loot::applyWantedState(LootEntity& lootItem, const WantedLookup& lookup) const
{
    lootItem.filterWanted = false;
    lootItem.filterMatch = LootFilterMatch::None;
    lootItem.wanted = false;

    glm::vec4 filterColour{};

    if (LootClassifier::get(lootItem).canApplyWantedState())
    {
        if (lookup.questIds.contains(lootItem.bsgId))
        {
            lootItem.filterWanted = true;
            lootItem.filterMatch = LootFilterMatch::Quest;
            filterColour = coloursGlobals::questColour;
        }
        else if (lookup.wishlistIds.contains(lootItem.bsgId))
        {
            lootItem.filterWanted = true;
            lootItem.filterMatch = LootFilterMatch::Wishlist;
            filterColour = coloursGlobals::wishListColour;
        }
        else if (const auto filterIt = lookup.activeFilterItems.find(lootItem.bsgId);
            filterIt != lookup.activeFilterItems.end())
        {
            lootItem.filterWanted = true;
            lootItem.filterMatch = LootFilterMatch::Other;
            filterColour = filterIt->second;
        }
        else if (lookup.categoryLootIds.contains(lootItem.bsgId) ||
            (lookup.battlePassInfoDocumentsSelected &&
             isBattlePassInfoDocument(lootItem)))
        {
            lootItem.filterWanted = true;
            lootItem.filterMatch = LootFilterMatch::Other;
            filterColour = lootGlobals::categoryLootColour;
        }
        else if (lootGlobals::enableValueLoot &&
            GetLootValueFilterPrice(lootItem) >= lootGlobals::valueLootFrom)
        {
            lootItem.filterWanted = true;
            lootItem.filterMatch = LootFilterMatch::Value;
            filterColour = coloursGlobals::valueLootColour;
        }
    }

    lootItem.wanted = lootItem.forceWanted || lootItem.filterWanted;

    if (lootItem.forceWanted)
    {
        lootItem.color = lootItem.forceColor;
        return;
    }

    if (lootItem.filterWanted)
        lootItem.color = filterColour;
}

bool loot::isContainerEnabled(const std::string& name) const
{
    if (name == "AirDrop")         return lootGlobals::drawAirDrops;
    if (name == "Duffle Bag")      return lootGlobals::drawDuffle;
    if (name == "Drawer")          return lootGlobals::drawDrawer;
    if (name == "Safe")            return lootGlobals::drawSafe;
    if (name == "Weapon Box")      return lootGlobals::drawWeaponBox;
    if (name == "Technical Crate") return lootGlobals::drawTechCrate;
    if (name == "Ration Crate")    return lootGlobals::drawRationCrate;
    if (name == "Medical Crate")   return lootGlobals::drawMedicalCrate;
    if (name == "Jacket")          return lootGlobals::drawJacket;
    if (name == "Med Package")     return lootGlobals::drawMedPackage;
    if (name == "Med Box")         return lootGlobals::drawMedBox;
    if (name == "Toolbox")         return lootGlobals::drawToolbox;
    if (name == "Grenade Box")     return lootGlobals::drawGrenadeBox;
    if (name == "Buried Stash")    return lootGlobals::drawBuriedStash;
    if (name == "Ground Cache")    return lootGlobals::drawGroundCache;
    if (name == "Wooden Crate")    return lootGlobals::drawWoodenCrate;
    if (name == "Suitcase")        return lootGlobals::drawSuitcase;
    if (name == "Ammo Box")        return lootGlobals::drawAmmoBox;
    if (name == "Dead Body")       return lootGlobals::drawDeadBody;
    if (name == "PC Block")        return lootGlobals::drawPCBlock;
    if (name == "Register")        return lootGlobals::drawRegister;

    return false;
}

void loot::updateLootableContainerStates(std::vector<LootEntity>& workingCache)
{
    if (workingCache.empty())
    {
        containerRefreshCursor = 0;
        return;
    }

    std::vector<ContainerOpenedRead> reads;
    reads.reserve((std::min)(workingCache.size(), MAX_CONTAINER_STATE_UPDATES_PER_TICK));

    for (size_t checked = 0;
        checked < workingCache.size() &&
        reads.size() < MAX_CONTAINER_STATE_UPDATES_PER_TICK;
        ++checked)
    {
        const size_t i = containerRefreshCursor++ % workingCache.size();
        LootEntity& item = workingCache[i];

        if (item.failed || !LootClassifier::get(item).needsContainerStateUpdate())
            continue;

        if (!Utils::valid_pointer(item.m_interactiveClass))
        {
            item.getContainerState().opened = false;
            continue;
        }

        ContainerOpenedRead read{};
        read.itemIndex = i;
        reads.emplace_back(read);
    }

    if (reads.empty())
        return;

    ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot container state");

    for (auto& read : reads)
    {
        const LootEntity& item = workingCache[read.itemIndex];

        batch.Add(
            item.m_interactiveClass + sdk::LootableContainer::InteractingPlayer,
            read.interactingPlayer
        );
    }

    if (!batch.Execute())
        return;

    for (const auto& read : reads)
        workingCache[read.itemIndex].getContainerState().opened = read.interactingPlayer != 0;
}

void loot::updateExistingLootItems(std::vector<LootEntity>& workingCache)
{
    const WantedLookup lookup = buildWantedLookup();
    const auto now = std::chrono::steady_clock::now();

    const bool updateDogTags =
        now - lastDogTagUpdate > std::chrono::seconds(1);

    if (updateDogTags)
        lastDogTagUpdate = now;

    for (auto& item : workingCache)
    {
        if (item.pendingResolve)
        {
            item.wanted = false;
            continue;
        }

        if (item.failed)
        {
            item.wanted = false;
            continue;
        }

        const LootEntityModel& model = LootClassifier::get(item);

        if (model.needsPositionRefresh() &&
            now - item.lastPositionUpdate >= std::chrono::seconds(10))
        {
            tryUpdateLootPosition(item, false);
        }

        if (item.hasValidPosition)
        {
            item.distance = static_cast<int>(
                std::trunc(
                    glm::distance(
                        mainGame.localLocation,
                        item.worldLocation
                    )
                )
                );
        }
        else
        {
            item.distance = 0;
        }

        if (model.needsCorpseUpdate())
        {
            item.wanted = espGlobals::drawCorpse;

            if (item.wanted)
                item.color = coloursGlobals::playerCorpse;

            continue;
        }

        if (item.isContainer())
        {
            item.wanted = isContainerEnabled(item.shortName);

            if (lootGlobals::hideSearched &&
                model.needsContainerStateUpdate() &&
                item.getContainerState().opened)
                item.wanted = false;

            if (item.wanted)
                item.color = coloursGlobals::containerColour;

            continue;
        }

        if (model.canApplyWantedState())
        {
            applyWantedState(item, lookup);
            continue;
        }
    }

    if (!updateDogTags || workingCache.empty())
        return;

    // A failed dog-tag read can be expensive. Try only one corpse per
    // update so several bad corpses cannot form one large DMA burst.
    for (size_t checked = 0;
        checked < workingCache.size();
        ++checked)
    {
        const size_t index =
            dogTagRefreshCursor++ % workingCache.size();

        const LootEntity& item = workingCache[index];

        if (item.pendingResolve || item.failed ||
            !LootClassifier::get(item).needsCorpseUpdate())
            continue;

        g_dogTagCache.ReadFromCorpse(item.m_interactiveClass);
        break;
    }
}

void loot::updateCorpseRequirements(std::vector<LootEntity>& workingCache)
{
    if (workingCache.empty())
        return;

    const auto now = std::chrono::steady_clock::now();
    const PlayerSnapshot playerCacheSnapshot =
        registeredPlayers.getCacheSnapshot();
    const PlayerCollection& playerCache = *playerCacheSnapshot;

    size_t updated = 0;

    for (size_t checked = 0;
        checked < workingCache.size() &&
        updated < MAX_CORPSE_UPDATES_PER_TICK;
        ++checked)
    {
        const size_t index =
            corpseRefreshCursor++ % workingCache.size();

        LootEntity& item = workingCache[index];

        if (item.pendingResolve || item.failed)
            continue;

        if (!LootClassifier::get(item).needsCorpseUpdate())
            continue;

        updateCorpseOwnerFromDogTagCache(item);
        updateCorpseOwnerFromPlayerCache(item, playerCache);

        CorpseLootState& corpseState = item.getCorpseState();
        corpseState.value = calculateCorpseValue(corpseState);

        if (now - item.lastCorpseEquipmentUpdate <
            std::chrono::seconds(20))
        {
            continue;
        }

        scanCorpseEquipment(item.m_interactiveClass, item, true);

        corpseState.value = calculateCorpseValue(corpseState);

        ++updated;
    }
}

void loot::cleanupMissingLoot(std::vector<LootEntity>& workingCache, const std::unordered_set<uint64_t>& livePointers)
{
    
    if (workingCache.size() >= MIN_CACHE_SIZE_FOR_CATASTROPHIC_DROP_GUARD &&
        livePointers.size() * 2 < workingCache.size())
    {
        missingLootDiscoveryCounts.clear();
        return;
    }

    workingCache.erase(
        std::remove_if(
            workingCache.begin(),
            workingCache.end(),
            [this, &livePointers](const LootEntity& item)
            {
                if (livePointers.contains(item.instance))
                {
                    missingLootDiscoveryCounts.erase(item.instance);
                    return false;
                }

                std::uint8_t& missingCount = missingLootDiscoveryCounts[item.instance];

                missingCount = std::min<std::uint8_t>(
                    MISSING_LOOT_SCANS_BEFORE_PRUNE,
                    static_cast<std::uint8_t>(missingCount + 1));

                if (missingCount < MISSING_LOOT_SCANS_BEFORE_PRUNE)
                    return false;

                missingLootDiscoveryCounts.erase(item.instance);
                return true;
            }
        ),
        workingCache.end()
    );
}

void loot::scanCorpseEquipment(uint64_t interactive, LootEntity& lootItem, bool update)
{
    lootItem.lastCorpseEquipmentUpdate =
        std::chrono::steady_clock::now();

    if (!Utils::valid_pointer(interactive))
        return;

    try
    {
        CorpseLootState& corpseState = lootItem.getCorpseState();
        const PlayerSnapshot playerCacheSnapshot =
            registeredPlayers.getCacheSnapshot();
        const PlayerCollection& playerCache = *playerCacheSnapshot;

        updateCorpseOwnerFromPlayerCache(lootItem, playerCache);

        uint64_t itemBase = 0;
        uint64_t slotsPtr = 0;

        {
            ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");
            batch.Add(interactive + sdk::InteractiveLootItem::Item, itemBase);

            if (!batch.Execute())
                return;
        }

        if (!Utils::valid_pointer(itemBase))
            return;

        {
            ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");
            batch.Add(itemBase + sdk::LootItemMod::Slots, slotsPtr);

            if (!batch.Execute())
                return;
        }

        if (!Utils::valid_pointer(slotsPtr))
            return;

        UnityArray<uint64_t> slotsRead(
            slotsPtr,
            "Loot corpse slots",
            MAX_CORPSE_SLOTS,
            DmaCacheMode::Uncached);

        if (slotsRead.count <= 0)
            return;

        std::vector<CorpseSlotRead> slotReads;
        slotReads.reserve(slotsRead.count);

        for (const uint64_t slotPtr : slotsRead)
        {
            if (!Utils::valid_pointer(slotPtr))
                continue;

            CorpseSlotRead read{};
            read.slotPtr = slotPtr;
            slotReads.emplace_back(read);
        }

        if (slotReads.empty())
            return;

        // slot
        {
            ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

            for (auto& read : slotReads)
            {
                batch.Add(read.slotPtr + sdk::Slot::ID, read.namePtr);
                batch.Add(read.slotPtr + sdk::Slot::ContainedItem, read.containedItem);
            }

            if (!batch.Execute())
                return;
        }

        // name template.
        {
            ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

            for (auto& read : slotReads)
            {
                if (Utils::valid_pointer(read.namePtr))
                    batch.Add(read.namePtr + 0x10, read.nameLen);

                if (Utils::valid_pointer(read.containedItem))
                {
                    batch.Add(read.containedItem + sdk::LootItem::Template, read.inventoryTemplate);
                    batch.Add(read.containedItem + sdk::BarterOtherOffsets::Dogtag, read.dogTagComponent);
                }
            }

            if (!batch.Execute())
                return;
        }

        // mongo id.
        {
            ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Loot");

            for (auto& read : slotReads)
            {
                if (Utils::valid_pointer(read.inventoryTemplate))
                    batch.Add(read.inventoryTemplate + sdk::ItemTemplate::_id, read.mongoId);
            }

            if (!batch.Execute())
                return;
        }

        std::vector<CorpseEquipment> newCorpseEquip;
        newCorpseEquip.reserve(slotReads.size());

        const WantedLookup wantedLookup = buildWantedLookup();

        for (auto& read : slotReads)
        {
            if (!Utils::valid_pointer(read.namePtr))
                continue;

            if (read.nameLen <= 0 || read.nameLen > 128)
                continue;

            const std::string slotName = TrimEFT(
                mem.readUnicodeString(
                    read.namePtr + 0x14,
                    read.nameLen,
                    DmaCacheMode::Uncached
                )
            );

            if (slotName.empty())
                continue;

            if (slotName == "Dogtag")
                updateCorpseOwnerFromDogTag(lootItem, read.dogTagComponent);

            if (skipNames.contains(slotName))
                continue;

            if (!Utils::valid_pointer(read.containedItem))
                continue;

            if (!Utils::valid_pointer(read.inventoryTemplate))
                continue;

            const std::string id = TrimEFT(
                read.mongoId.ReadString(mem, 128, false)
            );

            if (id.empty())
                continue;

            CorpseEquipment corpseEq{};
            corpseEq.slotName = slotName;

            if (const CachedMarketItem* marketItem = findMarketItem(id))
            {
                corpseEq.name = marketItem->shortName;
                corpseEq.value = static_cast<int>(GetLootSelectedPrice(
                    marketItem->marketPrice,
                    marketItem->traderPrice));
            }

            corpseEq.wanted =
                wantedLookup.activeFilterItems.contains(id) ||
                wantedLookup.questIds.contains(id) ||
                wantedLookup.wishlistIds.contains(id);

            if (!corpseEq.wanted &&
                lootGlobals::enableValueLoot &&
                corpseEq.value >= lootGlobals::valueLootFromEquip)
            {
                corpseEq.wanted = true;
            }

            if (corpseEq.name.empty())
                corpseEq.name = slotName;

            newCorpseEquip.emplace_back(std::move(corpseEq));
        }

        lootItem.getCorpseState().equipment = std::move(newCorpseEquip);
    }
    catch (...)
    {
        std::cout << "[LootCorpse] exception while processing corpse\n";
    }
}

void loot::lootTask()
{
    static int lootFailStreak = 0;
    static int lootCountFailStreak = 0;
    static int lootBufferFailStreak = 0;
    static int unexpectedFailureStreak = 0;
    static std::chrono::steady_clock::time_point lootBackoffUntil{};

    try
    {
        if (!mem.IsDmaOperational())
            return;

        if (!Utils::valid_pointer(mainGame.localPlayerPtr))
            return;

        const bool filteredLootEnabled =
            lootGlobals::enableQuestLoot ||
            lootGlobals::enableWishListLoot ||
            lootGlobals::enableValueLoot;

        const bool containerLootEnabled =
            lootGlobals::drawDrawer ||
            lootGlobals::drawDuffle ||
            lootGlobals::drawSafe ||
            lootGlobals::drawWeaponBox ||
            lootGlobals::drawTechCrate ||
            lootGlobals::drawRationCrate ||
            lootGlobals::drawMedicalCrate ||
            lootGlobals::drawJacket ||
            lootGlobals::drawMedPackage ||
            lootGlobals::drawMedBox ||
            lootGlobals::drawToolbox ||
            lootGlobals::drawGrenadeBox ||
            lootGlobals::drawBuriedStash ||
            lootGlobals::drawGroundCache ||
            lootGlobals::drawWoodenCrate ||
            lootGlobals::drawSuitcase ||
            lootGlobals::drawAmmoBox ||
            lootGlobals::drawDeadBody ||
            lootGlobals::drawPCBlock ||
            lootGlobals::drawRegister ||
            lootGlobals::drawAirDrops;

        if (!radarGlobals::drawLoot &&
            !espGlobals::drawLoot &&
            !espGlobals::drawCorpse &&
            !espGlobals::drawQuestHelper &&
            !filteredLootEnabled &&
            !containerLootEnabled)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now < lootBackoffUntil)
            return;

        bool completedLootDiscovery = false;
        const bool discoveryDue = nextLootDiscovery == std::chrono::steady_clock::time_point{} || now >= nextLootDiscovery;

        if (discoveryDue)
        {
            if (!buildPointers())
            {
                lootFailStreak++;
                if (lootFailStreak == 1 || lootFailStreak % 10 == 0)
                    LOGS.logError("[LOOT] Pointer Build Error");

                if (lootFailStreak >= 5)
                    lootBackoffUntil = now + std::chrono::seconds(20);

                nextLootDiscovery = now + LOOT_DISCOVERY_INTERVAL;
                return;
            }

            lootFailStreak = 0;

            if (!refreshLootListHeader())
            {
                lootCountFailStreak++;
                if (lootCountFailStreak >= 3)
                {
                    lootListP = 0;
                    lootListPtr = 0;
                }
                if (lootCountFailStreak == 1 ||
                    lootCountFailStreak % 10 == 0)
                {
                    LOGS.logError("[LOOT] List Header Error");
                }

                if (lootCountFailStreak >= 5)
                    lootBackoffUntil =
                        now + std::chrono::seconds(20);

                nextLootDiscovery = now + LOOT_DISCOVERY_INTERVAL;
                return;
            }

            lootCountFailStreak = 0;

            if (!buildLootBuffer())
            {
                lootBufferFailStreak++;
                if (lootBufferFailStreak == 1 ||
                    lootBufferFailStreak % 10 == 0)
                {
                    LOGS.logError("[LOOT] Loot Buffer Error");
                }

                if (lootBufferFailStreak >= 5)
                    lootListPtr = 0;

                if (lootBufferFailStreak >= 3)
                    lootBackoffUntil =
                        now + std::chrono::seconds(15);

                nextLootDiscovery = now + LOOT_DISCOVERY_INTERVAL;
                return;
            }

            lootBufferFailStreak = 0;

            stagedLootPointers.reserve(stagedLootPointers.size() + loot_buffer.size());

            for (const uint64_t pointer : loot_buffer)
            {
                if (Utils::valid_pointer(pointer))
                    stagedLootPointers.insert(pointer);
            }

            if (lootBufferScanCursor >= static_cast<size_t>(lootCount))
            {
                
                if (!stagedLootPointers.empty())
                {
                    liveLootPointers = std::move(stagedLootPointers);
                    stagedLootPointers.clear();
                    completedLootDiscovery = true;
                }

                lootBufferScanCursor = 0;
            }

            nextLootDiscovery = now + LOOT_DISCOVERY_INTERVAL;
        }

        unexpectedFailureStreak = 0;

        const auto& livePointers = liveLootPointers;

        if (livePointers.empty())
            return;

        std::vector<LootEntity> workingCache;

        {
            std::shared_lock lock(lootMutex);
            workingCache = lootList;
        }

        std::unordered_set<uint64_t> existingPointers;
        existingPointers.reserve(workingCache.size());

        for (const auto& item : workingCache)
        {
            if (Utils::valid_pointer(item.instance))
                existingPointers.insert(item.instance);
        }

        // A set prevents a pointer being queued twice.
        std::unordered_set<uint64_t> resolvePointerSet;
        resolvePointerSet.reserve(
            livePointers.size() + workingCache.size()
        );

        // Add completely new pointers.
        for (const uint64_t pointer : livePointers)
        {
            if (!existingPointers.contains(pointer))
                resolvePointerSet.insert(pointer);
        }

        // Add pending pointers that are ready for another attempt.
        for (const auto& item : workingCache)
        {
            if (!item.pendingResolve)
                continue;

            if (item.failed)
                continue;

            if (!livePointers.contains(item.instance))
                continue;

            if (now < item.nextResolveAttempt)
                continue;

            resolvePointerSet.insert(item.instance);
        }

        std::vector<uint64_t> pointersToResolve;
        pointersToResolve.reserve(resolvePointerSet.size());

        for (const uint64_t pointer : resolvePointerSet)
            pointersToResolve.emplace_back(pointer);

        if (pointersToResolve.size() > MAX_LOOT_RESOLVE_PER_TICK)
            pointersToResolve.resize(MAX_LOOT_RESOLVE_PER_TICK);

        if (!pointersToResolve.empty())
        {
            std::vector<LootEntity> resolveResults;

            if (!buildNewLootItemsScatter(
                pointersToResolve,
                resolveResults))
            {
                // failed attempt results so every pointer still follows
                resolveResults.clear();
                resolveResults.reserve(pointersToResolve.size());

                for (const uint64_t pointer : pointersToResolve)
                {
                    LootEntity failedAttempt{};
                    failedAttempt.instance = pointer;
                    failedAttempt.failed = true;
                    failedAttempt.failureReason =
                        "Complete scatter resolution failed";

                    resolveResults.emplace_back(
                        std::move(failedAttempt)
                    );
                }
            }

            mergeResolveResults(
                workingCache,
                std::move(resolveResults),
                now
            );
        }

        updateLootableContainerStates(workingCache);
        updateCorpseRequirements(workingCache);
        updateExistingLootItems(workingCache);
        if (completedLootDiscovery)
            cleanupMissingLoot(workingCache, livePointers);

        {
            std::unique_lock lock(lootMutex);

            std::unordered_map<uint64_t, const LootEntity*> currentByInstance;
            currentByInstance.reserve(lootList.size());

            for (const LootEntity& current : lootList)
            {
                if (current.instance != 0)
                    currentByInstance.emplace(current.instance, &current);
            }

            for (LootEntity& item : workingCache)
            {
                const auto currentIt = currentByInstance.find(item.instance);

                if (currentIt == currentByInstance.end())
                    continue;

                const LootEntity& current = *currentIt->second;

                if (current.forceWanted != item.forceWanted)
                {
                    
                    item.forceWanted = current.forceWanted;
                    item.forceColor = current.forceColor;
                    item.filterWanted = current.filterWanted;
                    item.filterMatch = current.filterMatch;
                    item.wanted = current.wanted;
                    item.color = current.color;
                }
            }

            lootList = std::move(workingCache);
            publishCacheSnapshotLocked();
        }

    }
    catch (const std::exception& e)
    {
        ++unexpectedFailureStreak;

        if (unexpectedFailureStreak == 1 || unexpectedFailureStreak % 10 == 0)
        {
            LOGS.logError(
                "[LOOT] Exception while updating; retaining the current cache: " +
                std::string(e.what()));
        }
    }
    catch (...)
    {
        ++unexpectedFailureStreak;

        if (unexpectedFailureStreak == 1 || unexpectedFailureStreak % 10 == 0)
        {
            LOGS.logError(
                "[LOOT] Unknown exception while updating; retaining the current cache");
        }
    }
}

