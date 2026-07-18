#include "../app/includes.h"
#include <glm/glm.hpp>
#include "../game/headers/loot.h"
#include "../app/globals.h"
#include "../memory/Memory.h"
#include "../game/headers/utils.h"
#include "../game/headers/maingame.h"
#include "../game/headers/sdk.h"
#include "../game/headers/unityHelper.h"
#include "../game/headers/tarkovdevquery.h"
#include <map>
#include "headers/unitysdk.h"
#include "headers/transform.h"
#include "headers/questManager.h"
#include "headers/wishlist.h"
#include "headers/dogtag.h"
#include "headers/players.h"


std::vector<LootFilters> lootFilters;
loot Loot;

constexpr std::uint8_t MAX_LOOT_RESOLVE_ATTEMPTS = 20;

constexpr std::chrono::milliseconds LOOT_RESOLVE_RETRY_DELAY{
    500
};

namespace
{
    constexpr int MAX_LOOT_COUNT = 12000;
    constexpr int MAX_LOOT_BUFFER_ITEMS = 8000;
    constexpr size_t MAX_LOOT_RESOLVE_PER_TICK = 32;
    constexpr size_t MAX_OBJECT_NAME_LENGTH = 64;
    constexpr size_t MAX_CLASS_NAME_LENGTH = 64;

    const std::unordered_set<std::string> skipNames =
    {
        "Compass",
        "ArmBand",
        "Pockets",
        "SecuredContainer"
    };

    class ScatterBatch
    {
    public:
        explicit ScatterBatch(bool useCache = false)
            : useCache_(useCache)
        {
            handle_ = mem.CreateScatterHandle(useCache_);
        }

        ~ScatterBatch()
        {
            if (handle_)
                mem.CloseScatterHandle(handle_);
        }

        ScatterBatch(const ScatterBatch&) = delete;
        ScatterBatch& operator=(const ScatterBatch&) = delete;

        template <typename T>
        bool add(uint64_t address, T& out)
        {
            static_assert(std::is_trivially_copyable_v<T>, "Scatter read type must be trivially copyable");

            if (!handle_)
                return false;

            if (!Utils::valid_pointer(address))
                return false;

            const bool added = mem.AddScatterReadRequest(
                handle_,
                address,
                &out,
                sizeof(T)
            );

            if (added)
                queued_ = true;
            else
                ok_ = false;

            return added;
        }

        bool execute()
        {
            if (!handle_)
                return false;

            if (!queued_)
                return true;

            if (!ok_)
                return false;

            return mem.ExecuteReadScatter(handle_, 0, useCache_);
        }

    private:
        VMMDLL_SCATTER_HANDLE handle_ = nullptr;
        bool useCache_ = false;
        bool queued_ = false;
        bool ok_ = true;
    };

    struct LootShellRead
    {
        uint64_t instance = 0;

        uint64_t monoBehaviour = 0;
        uint64_t interactiveClass = 0;
        uint64_t gameObject = 0;
        uint64_t gameObjectNamePtr = 0;
        uint64_t components = 0;
        uint64_t transform = 0;
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

    struct CorpseSlotRead
    {
        uint64_t slotPtr = 0;

        uint64_t namePtr = 0;
        uint64_t containedItem = 0;
        uint64_t inventoryTemplate = 0;

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

    int calculateCorpseValue(const std::vector<corpseEquipment>& equipment)
    {
        int value = 0;

        for (const auto& entry : equipment)
            value += entry.value;

        return value;
    }

    bool applyMarketDetails(const std::string& bsgid, LootList& item)
    {
        for (const auto& ml : marketList)
        {
            if (ml.bsgid != bsgid)
                continue;

            item.longName = ml.name;
            item.shortName = ml.shortName;
            item.traderPrice = static_cast<int>(ml.traderPrice);
            item.avgMarketPrice = static_cast<int>(ml.marketPrice);

            return true;
        }

        if (item.shortName.empty())
            item.shortName = item.gameObjectName;

        if (item.longName.empty())
            item.longName = item.gameObjectName;

        return false;
    }
}

LootList updateLootDetails(std::string bsgid, LootList& item)
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
        { "6582e6d7b14c3f72eb071420", "Dead Body" },

        { "67614e3a6a90e4f10b0b140d", "Xmas Loot" }
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

    for (const auto& task : tarkovDevTasksData)
    {
        for (const auto& obj : task.objectives)
        {
            if (!obj.questItemId.empty() && obj.questItemId == itemId)
                return task.qName;
        }
    }

    return "";
}

std::vector<LootList> loot::getCacheLoot() const
{
    std::shared_lock lock(lootMutex);
    return lootList;
}

void loot::clearCache()
{
    std::unique_lock lock(lootMutex);

    lootList.clear();
    loot_buffer.clear();

    lootListP = 0;
    lootListPtr = 0;
    lootCount = 0;
}

void loot::markFailed(LootList& item, std::string reason) const
{
    item.failed = true;
    item.failureReason = std::move(reason);
    item.wanted = false;
}

void loot::markLootWanted(
    const std::vector<uint64_t>& instances,
    const glm::vec4& colour)
{
    if (instances.empty())
        return;

    std::unordered_set<uint64_t> instanceSet;
    instanceSet.reserve(instances.size());

    for (const uint64_t instance : instances)
    {
        if (instance != 0)
            instanceSet.insert(instance);
    }

    if (instanceSet.empty())
        return;

    std::unique_lock<std::shared_mutex> lock(lootMutex);

    for (LootList& loot : lootList)
    {
        if (loot.instance == 0)
            continue;

        if (instanceSet.find(loot.instance) == instanceSet.end())
            continue;

        loot.wanted = true;
        loot.forceWanted = true;
        loot.color = colour;
    }
}

bool loot::tryUpdateLootPosition(
    LootList& item,
    bool markAsFailedOnError)
{
    item.lastPositionUpdate =
        std::chrono::steady_clock::now();

    auto LogTransformDebug = [&](const char* stage,
        const char* reason,
        const glm::vec3* position = nullptr)
        {
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
                        UnityOffsets::TransformAccess_IndexOffset
                    );

                    hierarchyPtr = mem.Read<uint64_t>(
                        transformPtr +
                        UnityOffsets::TransformAccess_HierarchyOffset
                    );

                    if (Utils::valid_pointer(hierarchyPtr))
                    {
                        verticesPtr = mem.Read<uint64_t>(
                            hierarchyPtr +
                            UnityOffsets::Hierarchy_VerticesOffset
                        );

                        indicesPtr = mem.Read<uint64_t>(
                            hierarchyPtr +
                            UnityOffsets::Hierarchy_IndicesOffset
                        );

                        if (Utils::valid_pointer(indicesPtr) &&
                            transformIndex >= 0 &&
                            transformIndex < 1'000'000)
                        {
                            parentIndex = mem.Read<int>(
                                indicesPtr +
                                static_cast<uint64_t>(
                                    transformIndex
                                    ) * sizeof(int)
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

        return false;
    }

    try
    {
        UnityTransform transform(
            item.m_pointerToTransform1
        );

        if (!transform.IsValid())
        {
            LogTransformDebug(
                "Construct",
                "Invalid transform hierarchy"
            );

            if (markAsFailedOnError)
                markFailed(item, "Invalid transform hierarchy");

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

        return false;
    }
}

void loot::mergeResolveResults(
    std::vector<LootList>& workingCache,
    std::vector<LootList>&& results,
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

        LootList* previousItem = nullptr;
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

            if (!result.isItem &&
                !result.isQuestItem &&
                !result.isContainer &&
                !result.isCorpse)
            {
                result.isItem = previousItem->isItem;
                result.isQuestItem = previousItem->isQuestItem;
                result.isContainer = previousItem->isContainer;
                result.isCorpse = previousItem->isCorpse;
                result.isAirdrop = previousItem->isAirdrop;
            }

            if (result.lastPositionUpdate ==
                std::chrono::steady_clock::time_point{})
            {
                result.lastPositionUpdate =
                    previousItem->lastPositionUpdate;
            }
        }

        const bool attemptFailed = result.failed;

        result.resolveAttempts = static_cast<std::uint8_t>(
            std::min<int>(
                static_cast<int>(previousAttempts) + 1,
                MAX_LOOT_RESOLVE_ATTEMPTS
            )
            );

        if (attemptFailed)
        {
            result.wanted = false;

            if (result.resolveAttempts >= MAX_LOOT_RESOLVE_ATTEMPTS)
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
    if (Utils::valid_pointer(lootListP) && Utils::valid_pointer(lootListPtr))
        return true;

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return false;

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        lootListP = mem.Read<uint64_t>(
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::LootList
        );

        if (!Utils::valid_pointer(lootListP))
        {
            if (attempt < 2)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }

        int count = 0;

        {
            ScatterBatch batch;
            batch.add(lootListP + 0x10, lootListPtr);
            batch.add(lootListP + 0x18, count);

            if (!batch.execute())
            {
                if (attempt < 2)
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                continue;
            }
        }

        lootCount = count;

        if (!Utils::valid_pointer(lootListPtr))
            continue;

        if (lootCount <= 0 || lootCount > MAX_LOOT_COUNT)
            return false;

        return true;
    }

    return false;
}

bool loot::get_lootCount()
{
    if (!Utils::valid_pointer(lootListP))
        return lootCount > 0;

    int count = 0;

    if (!mem.TryRead<int>(lootListP + 0x18, count, true))
        return lootCount > 0;

    if (count <= 0 || count > MAX_LOOT_COUNT)
    {
        if (lootCount > 0)
            return true;
        return false;
    }

    lootCount = count;
    return true;
}

bool loot::buildLootBuffer()
{
    if (lootCount <= 0 || lootCount > MAX_LOOT_COUNT)
    {
        clearCache();
        return false;
    }

    if (!Utils::valid_pointer(lootListPtr))
    {
        clearCache();
        return false;
    }

    const int itemsToRead = (lootCount < MAX_LOOT_BUFFER_ITEMS) ? lootCount : MAX_LOOT_BUFFER_ITEMS;
    loot_buffer.assign(static_cast<size_t>(itemsToRead), 0);

    const size_t bytes = sizeof(uint64_t) * static_cast<size_t>(itemsToRead);

    if (!mem.Read(lootListPtr + 0x20, loot_buffer.data(), bytes))
        return false;

    return true;
}

bool loot::buildNewLootItemsScatter(
    const std::vector<uint64_t>& newPointers,
    std::vector<LootList>& outItems)
{
    outItems.clear();

    if (newPointers.empty())
        return true;

    std::vector<LootShellRead> shellReads;
    std::vector<LootList> candidates;

    shellReads.reserve(newPointers.size());
    candidates.reserve(newPointers.size());

    for (const uint64_t pointer : newPointers)
    {
        if (!Utils::valid_pointer(pointer))
            continue;

        LootShellRead shell{};
        shell.instance = pointer;
        shellReads.emplace_back(shell);

        LootList item{};
        item.instance = pointer;
        candidates.emplace_back(std::move(item));
    }

    if (shellReads.empty())
        return true;

    // MonoBehaviour.
    {
        ScatterBatch batch;

        for (auto& shell : shellReads)
            batch.add(shell.instance + 0x10, shell.monoBehaviour);

        if (!batch.execute())
        {
            for (auto& item : candidates)
                markFailed(item, "MonoBehaviour scatter execution failed");

            outItems = std::move(candidates);
            return true;
        }
    }

    // interactive class and GameObject.
    {
        ScatterBatch batch;

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.monoBehaviour))
                continue;

            batch.add(
                shell.monoBehaviour + UnityOffsets::Component_ObjectClassOffset,
                shell.interactiveClass
            );

            batch.add(
                shell.monoBehaviour + UnityOffsets::Component_GameObjectOffset,
                shell.gameObject
            );
        }

        if (!batch.execute())
        {
            for (auto& item : candidates)
                markFailed(item, "Object pointer scatter execution failed");

            outItems = std::move(candidates);
            return true;
        }
    }

    // name pointer and components.
    {
        ScatterBatch batch;

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.gameObject))
                continue;

            batch.add(
                shell.gameObject + UnityOffsets::GameObject_NameOffset,
                shell.gameObjectNamePtr
            );

            batch.add(
                shell.gameObject + UnityOffsets::GameObject_ComponentsOffset,
                shell.components
            );
        }

        if (!batch.execute())
        {
            for (auto& item : candidates)
                markFailed(item, "GameObject scatter execution failed");

            outItems = std::move(candidates);
            return true;
        }
    }

    //transform.
    {
        ScatterBatch batch;

        for (auto& shell : shellReads)
        {
            if (!Utils::valid_pointer(shell.components))
                continue;

            batch.add(shell.components + 0x8, shell.transform);
        }

        if (!batch.execute())
        {
            for (size_t i = 0; i < candidates.size(); ++i)
                shellReads[i].transform = 0;
        }
    }

    // item data.
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        LootList& item = candidates[i];
        const LootShellRead& shell = shellReads[i];

        item.m_interactiveClass = shell.interactiveClass;
        item.m_gameObject = shell.gameObject;
        item.m_pGameObjectName = shell.gameObjectNamePtr;
        item.m_pointerToTransform1 = shell.transform;

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
                MAX_CLASS_NAME_LENGTH
            );

            item.gameObjectName = mem.readString(
                item.m_pGameObjectName,
                MAX_OBJECT_NAME_LENGTH
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
            markFailed(item, "Skipped script object");
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
                item.isItem ||
                item.isQuestItem ||
                item.isContainer ||
                item.isCorpse;

            if (!recognized)
            {
                markFailed(
                    item,
                    "Unsupported class: " + item.m_objectClassName
                );
            }
        }

        if (!item.failed)
        {
            tryUpdateLootPosition(item, !item.isAirdrop);
        }

        outItems.emplace_back(std::move(item));
    }

    return true;
}

void loot::classifyObservedLootItemsScatter(std::vector<LootList>& items)
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
        ScatterBatch batch;

        for (auto& read : reads)
        {
            LootList& item = items[read.itemIndex];

            batch.add(
                item.m_interactiveClass + sdk::InteractiveLootItem::Item,
                read.itemObject
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Item object scatter failed");

            return;
        }
    }

    {
        ScatterBatch batch;

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemObject))
                continue;

            batch.add(
                read.itemObject + sdk::LootItem::Template,
                read.itemTemplate
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Item template scatter failed");

            return;
        }
    }

    {
        ScatterBatch batch;

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            batch.add(
                read.itemTemplate + sdk::ItemTemplate::_id,
                read.mongoId
            );

            batch.add(
                read.itemTemplate + sdk::ItemTemplate::QuestItem,
                read.questItem
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Item metadata scatter failed");

            return;
        }
    }

    for (auto& read : reads)
    {
        LootList& item = items[read.itemIndex];

        item.isContainer = false;
        item.isCorpse = false;

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
            item.bsgId = TrimEFT(read.mongoId.ReadString(mem));
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
            item.isItem = false;
            item.isQuestItem = true;

            const std::string questName =
                GetQuestItemDisplayName(item.bsgId);

            item.shortName = !questName.empty()
                ? questName
                : item.gameObjectName;

            item.longName = item.gameObjectName;
        }
        else
        {
            item.isItem = true;
            item.isQuestItem = false;

            applyMarketDetails(item.bsgId, item);
        }
    }
}

void loot::classifyLootableContainersScatter(std::vector<LootList>& items)
{
    std::vector<ContainerLootRead> reads;
    reads.reserve(items.size());

    for (size_t i = 0; i < items.size(); ++i)
    {
        LootList& item = items[i];

        if (item.failed)
            continue;

        if (item.m_objectClassName != "LootableContainer")
            continue;

        item.isContainer = true;
        item.isCorpse = false;
        item.isItem = false;
        item.isQuestItem = false;

        if (Utils::Text::containsIgnoreCase(
            item.gameObjectName,
            "loot_collider"))
        {
            item.isAirdrop = true;
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
        ScatterBatch batch;

        for (auto& read : reads)
        {
            LootList& item = items[read.itemIndex];

            batch.add(
                item.m_interactiveClass + sdk::LootableContainer::ItemOwner,
                read.itemOwner
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container owner scatter failed");

            return;
        }
    }

    {
        ScatterBatch batch;

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemOwner))
                continue;

            batch.add(
                read.itemOwner + sdk::LootableContainerItemOwner::RootItem,
                read.rootItem
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container root item scatter failed");

            return;
        }
    }

    {
        ScatterBatch batch;

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.rootItem))
                continue;

            batch.add(
                read.rootItem + sdk::LootItem::Template,
                read.itemTemplate
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container template scatter failed");

            return;
        }
    }

    {
        ScatterBatch batch;

        for (auto& read : reads)
        {
            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            batch.add(
                read.itemTemplate + sdk::ItemTemplate::_id,
                read.mongoId
            );
        }

        if (!batch.execute())
        {
            for (const auto& read : reads)
                markFailed(items[read.itemIndex], "Container ID scatter failed");

            return;
        }
    }

    for (auto& read : reads)
    {
        LootList& item = items[read.itemIndex];

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
            item.bsgId = TrimEFT(read.mongoId.ReadString(mem));
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

        item.shortName = getContainerName(item.bsgId);
        item.longName = item.shortName;
    }
}

void loot::classifyCorpseLootItems(std::vector<LootList>& items)
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

        item.isContainer = false;
        item.isCorpse = true;
        item.isItem = false;
        item.isQuestItem = false;
        item.isAirdrop = false;

        item.bsgId.clear();
        item.shortName = "Corpse";

        scanCorpseEquipment(item.m_interactiveClass, item, false);
        item.corpseValue = calculateCorpseValue(item.corpseEquip);
    }
}

loot::WantedLookup loot::buildWantedLookup() const
{
    WantedLookup lookup{};

    if (lootGlobals::enableQuestLoot)
    {
        for (const auto& questItem : masterItems)
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

    return lookup;
}

void loot::applyWantedState(LootList& lootItem, const WantedLookup& lookup) const
{
    if (!lootItem.isItem && !lootItem.isQuestItem)
        return;

    lootItem.wanted = lootItem.forceWanted;

    if (lootItem.bsgId.empty())
        return;

    if (lookup.questIds.contains(lootItem.bsgId))
    {
        lootItem.wanted = true;
        lootItem.color = coloursGlobals::questColour;
        return;
    }

    if (lookup.wishlistIds.contains(lootItem.bsgId))
    {
        lootItem.wanted = true;
        lootItem.color = coloursGlobals::wishListColour;
        return;
    }

    if (lootGlobals::enableValueLoot &&
        lootItem.avgMarketPrice > lootGlobals::valueLootFrom)
    {
        lootItem.wanted = true;
        lootItem.color = coloursGlobals::valueLootColour;
        return;
    }

    const auto filterIt = lookup.activeFilterItems.find(lootItem.bsgId);

    if (filterIt != lookup.activeFilterItems.end())
    {
        lootItem.wanted = true;
        lootItem.color = filterIt->second;
        return;
    }

    if (!lootItem.forceWanted)
        lootItem.wanted = false;
}

bool loot::isContainerEnabled(const std::string& name) const
{
    if (name == "AirDrop")         return drawAirDrops;
    if (name == "Duffle Bag")      return drawDuffle;
    if (name == "Drawer")          return drawDrawer;
    if (name == "Safe")            return drawSafe;
    if (name == "Weapon Box")      return drawWeaponBox;
    if (name == "Technical Crate") return drawTechCrate;
    if (name == "Ration Crate")    return drawRationCrate;
    if (name == "Medical Crate")   return drawMedicalCrate;
    if (name == "Jacket")          return drawJacket;
    if (name == "Med Package")     return drawMedPackage;
    if (name == "Med Box")         return drawMedBox;
    if (name == "Toolbox")         return drawToolbox;
    if (name == "Grenade Box")     return drawGrenadeBox;
    if (name == "Buried Stash")    return drawBuriedStash;
    if (name == "Ground Cache")    return drawGroundCache;
    if (name == "Wooden Crate")    return drawWoodenCrate;
    if (name == "Suitcase")        return drawSuitcase;
    if (name == "Ammo Box")        return drawAmmoBox;
    if (name == "Dead Body")       return drawDeadBody;
    if (name == "PC Block")        return drawPCBlock;
    if (name == "Register")        return drawRegister;

    return false;
}

void loot::updateExistingLootItems(
    std::vector<LootList>& workingCache)
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

        if (item.isAirdrop &&
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

        if (item.isCorpse)
        {
            item.wanted = espGlobals::drawCorpse;

            if (item.wanted)
                item.color = coloursGlobals::playerCorpse;

            if (updateDogTags)
                g_dogTagCache.ReadFromCorpse(
                    item.m_interactiveClass
                );

            continue;
        }

        if (item.isContainer)
        {
            item.wanted = isContainerEnabled(item.shortName);
            continue;
        }

        if (item.isItem || item.isQuestItem)
        {
            applyWantedState(item, lookup);
            continue;
        }
    }
}

void loot::updateCorpseRequirements(
    std::vector<LootList>& workingCache)
{
    const auto now = std::chrono::steady_clock::now();

    if (now - lastCorpseEquipUpdate <= std::chrono::seconds(20))
        return;

    lastCorpseEquipUpdate = now;

    for (auto& item : workingCache)
    {
        if (item.pendingResolve || item.failed)
            continue;

        if (!item.isCorpse)
            continue;

        scanCorpseEquipment(
            item.m_interactiveClass,
            item,
            true
        );

        item.corpseValue =
            calculateCorpseValue(item.corpseEquip);
    }
}

void loot::cleanupMissingLoot(
    std::vector<LootList>& workingCache,
    const std::unordered_set<uint64_t>& livePointers) const
{
    workingCache.erase(
        std::remove_if(
            workingCache.begin(),
            workingCache.end(),
            [&livePointers](const LootList& item)
            {
                return !livePointers.contains(item.instance);
            }
        ),
        workingCache.end()
    );
}

void loot::scanCorpseEquipment(uint64_t interactive, LootList& lootItem, bool update)
{
    if (!Utils::valid_pointer(interactive))
        return;

    try
    {
        uint64_t itemBase = 0;
        uint64_t slotsPtr = 0;

        {
            ScatterBatch batch;
            batch.add(interactive + sdk::InteractiveLootItem::Item, itemBase);

            if (!batch.execute())
                return;
        }

        if (!Utils::valid_pointer(itemBase))
            return;

        {
            ScatterBatch batch;
            batch.add(itemBase + sdk::LootItemMod::Slots, slotsPtr);

            if (!batch.execute())
                return;
        }

        if (!Utils::valid_pointer(slotsPtr))
            return;

        UnityArray<uint64_t> slotsRead(slotsPtr);

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
            ScatterBatch batch;

            for (auto& read : slotReads)
            {
                batch.add(read.slotPtr + sdk::Slot::ID, read.namePtr);
                batch.add(read.slotPtr + sdk::Slot::ContainedItem, read.containedItem);
            }

            if (!batch.execute())
                return;
        }

        // name template.
        {
            ScatterBatch batch;

            for (auto& read : slotReads)
            {
                if (Utils::valid_pointer(read.namePtr))
                    batch.add(read.namePtr + 0x10, read.nameLen);

                if (Utils::valid_pointer(read.containedItem))
                    batch.add(read.containedItem + sdk::LootItem::Template, read.inventoryTemplate);
            }

            if (!batch.execute())
                return;
        }

        // mongo id.
        {
            ScatterBatch batch;

            for (auto& read : slotReads)
            {
                if (Utils::valid_pointer(read.inventoryTemplate))
                    batch.add(read.inventoryTemplate + sdk::ItemTemplate::_id, read.mongoId);
            }

            if (!batch.execute())
                return;
        }

        std::vector<corpseEquipment> newCorpseEquip;
        newCorpseEquip.reserve(slotReads.size());

        bool isPMC = false;

        const std::vector<PlayerCache> playerCache = players.getCacheSnapshot();

        for (const auto& player : playerCache)
        {
            if (interactive != player.P_CorpseClass)
                continue;

            if (player.isPlayer)
                isPMC = true;

            if (player.isPlayer || player.isBoss)
                lootItem.longName = player.name;

            break;
        }

        for (auto& read : slotReads)
        {
            if (!Utils::valid_pointer(read.namePtr))
                continue;

            if (!Utils::valid_pointer(read.containedItem))
                continue;

            if (!Utils::valid_pointer(read.inventoryTemplate))
                continue;

            if (read.nameLen <= 0 || read.nameLen > 128)
                continue;

            const std::string slotName = TrimEFT(
                mem.readUnicodeString(read.namePtr + 0x14, read.nameLen)
            );

            if (slotName.empty())
                continue;

            if (skipNames.contains(slotName))
                continue;

            if (isPMC && slotName == "Scabbard")
                continue;

            const std::string id = TrimEFT(read.mongoId.ReadString(mem));

            if (id.empty())
                continue;

            corpseEquipment corpseEq{};

            for (const auto& ml : marketList)
            {
                if (ml.bsgid != id)
                    continue;

                corpseEq.equipmentName = ml.shortName;
                corpseEq.value = (ml.marketPrice == 0)
                    ? static_cast<int>(ml.traderPrice)
                    : static_cast<int>(ml.marketPrice);

                break;
            }

            for (const auto& filter : lootFilters)
            {
                if (!filter.active)
                    continue;

                bool found = false;

                for (const auto& filterItem : filter.lootItems)
                {
                    if (id == filterItem.bsgid)
                    {
                        corpseEq.wanted = true;
                        found = true;
                        break;
                    }
                }

                if (found)
                    break;
            }

            if (!corpseEq.wanted)
            {
                for (const auto& quest : masterItems)
                {
                    if (quest == id)
                    {
                        corpseEq.wanted = true;
                        break;
                    }
                }
            }

            if (!corpseEq.wanted)
            {
                for (const auto& wishlist : wishListData)
                {
                    if (wishlist.bsgId == id)
                    {
                        corpseEq.wanted = true;
                        break;
                    }
                }
            }

            if (!corpseEq.wanted &&
                lootGlobals::enableValueLoot &&
                corpseEq.value > lootGlobals::valueLootFrom)
            {
                corpseEq.wanted = true;
            }

            if (corpseEq.equipmentName.empty())
                corpseEq.equipmentName = slotName;

            newCorpseEquip.emplace_back(std::move(corpseEq));
        }

        lootItem.corpseEquip = std::move(newCorpseEquip);
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
    static std::chrono::steady_clock::time_point lootBackoffUntil{};

    try
    {
        if (!mem.IsDmaOperational())
            return;

        if (!Utils::valid_pointer(mainGame.localPlayerPtr))
            return;

        if (!radarGlobals::drawLoot && !espGlobals::drawLoot)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (now < lootBackoffUntil)
            return;

        if (!buildPointers())
        {
            lootFailStreak++;
            if (lootFailStreak == 1 || lootFailStreak % 10 == 0)
                LOGS.logError("[LOOT] Pointer Build Error");

            if (lootFailStreak >= 5)
                lootBackoffUntil = now + std::chrono::seconds(20);

            return;
        }

        lootFailStreak = 0;

        if (!get_lootCount())
        {
            lootCountFailStreak++;
            if (lootCountFailStreak >= 3)
            {
                lootListP = 0;
                lootListPtr = 0;
            }
            if (lootCountFailStreak == 1 || lootCountFailStreak % 10 == 0)
                LOGS.logError("[LOOT] Count Error");

            if (lootCountFailStreak >= 5)
                lootBackoffUntil = now + std::chrono::seconds(20);

            return;
        }

        lootCountFailStreak = 0;

        if (!buildLootBuffer())
        {
            lootBufferFailStreak++;
            if (lootBufferFailStreak == 1 || lootBufferFailStreak % 10 == 0)
                LOGS.logError("[LOOT] Loot Buffer Error");

            if (lootBufferFailStreak >= 5)
                lootListPtr = 0;

            if (lootBufferFailStreak >= 3)
                lootBackoffUntil = now + std::chrono::seconds(15);

            return;
        }

        lootBufferFailStreak = 0;

        std::unordered_set<uint64_t> livePointers;
        livePointers.reserve(loot_buffer.size());

        for (const uint64_t pointer : loot_buffer)
        {
            if (Utils::valid_pointer(pointer))
                livePointers.insert(pointer);
        }

        std::vector<LootList> workingCache;

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
            std::vector<LootList> resolveResults;

            if (!buildNewLootItemsScatter(
                pointersToResolve,
                resolveResults))
            {
                // failed attempt results so every pointer still follows
                resolveResults.clear();
                resolveResults.reserve(pointersToResolve.size());

                for (const uint64_t pointer : pointersToResolve)
                {
                    LootList failedAttempt{};
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

        updateCorpseRequirements(workingCache);
        updateExistingLootItems(workingCache);
        cleanupMissingLoot(workingCache, livePointers);

        {
            std::unique_lock lock(lootMutex);
            lootList = std::move(workingCache);
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError(
            "Exception caught in lootThread: " +
            std::string(e.what()) +
            ". Clearing cache..."
        );

        clearCache();
    }
    catch (...)
    {
        LOGS.logError(
            "Unknown exception caught in lootThread. Clearing cache..."
        );

        clearCache();
    }
}

