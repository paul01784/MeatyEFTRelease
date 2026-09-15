#include "HideoutOverview.h"

#include "../../SDK/EftOffsets.h"
#include "../../Unity/UnityOffsets.h"
#include "../../../Core/Utilities.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

HideoutOverview HIDEOUT_OVERVIEW;

namespace
{
    constexpr DmaCacheMode kReadMode = DmaCacheMode::Uncached;
    constexpr std::int32_t kMaxGameObjects = 100000;
    constexpr std::int32_t kMaxAreas = 128;
    constexpr std::int32_t kMaxLevels = 32;
    constexpr std::int32_t kMaxRequirements = 128;
    constexpr std::uint64_t kEftComponentObjectClassOffset = 0x20;

#pragma pack(push, 8)
    struct ObjectListNode
    {
        std::uint64_t previous{}; // +0x00
        std::uint64_t next{};     // +0x08
        std::uint64_t object{};   // +0x10
    };

    struct DictionaryEntry
    {
        std::int32_t hashCode{};
        std::int32_t next{};
        std::int32_t key{};
        std::int32_t padding{};
        std::uint64_t value{};
    };
#pragma pack(pop)

    struct ComponentArray
    {
        std::uint64_t entries{};
        std::uint64_t memoryLabel{};
        std::uint64_t size{};
        std::uint64_t capacity{};
    };

    struct ComponentEntry
    {
        std::uint64_t unknown0{};
        std::uint64_t component{};
    };

    static_assert(offsetof(ObjectListNode, previous) == 0x00);
    static_assert(offsetof(ObjectListNode, next) == 0x08);
    static_assert(offsetof(ObjectListNode, object) == 0x10);
    static_assert(offsetof(ComponentArray, size) == UnityOffsets::ComponentArray_SizeOffset);
    static_assert(offsetof(ComponentArray, capacity) == UnityOffsets::ComponentArray_CapacityOffset);
    static_assert(offsetof(ComponentEntry, component) == 0x8);
    static_assert(sizeof(ComponentEntry) == 0x10);

    static_assert(sizeof(DictionaryEntry) == 0x18);

    bool ContainsInsensitive(const std::string& value, const char* token)
    {
        std::string lowerValue = value;
        std::string lowerToken = token;
        std::transform(lowerValue.begin(), lowerValue.end(), lowerValue.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowerValue.find(lowerToken) != std::string::npos;
    }

    HideoutRequirementKind ClassifyRequirement(const std::string& className)
    {
        if (ContainsInsensitive(className, "Tool")) return HideoutRequirementKind::Tool;
        if (ContainsInsensitive(className, "Item")) return HideoutRequirementKind::Item;
        if (ContainsInsensitive(className, "Area")) return HideoutRequirementKind::Area;
        if (ContainsInsensitive(className, "Skill")) return HideoutRequirementKind::Skill;
        if (ContainsInsensitive(className, "Loyalty")) return HideoutRequirementKind::TraderLoyalty;
        if (ContainsInsensitive(className, "Trader")) return HideoutRequirementKind::TraderUnlock;
        if (ContainsInsensitive(className, "Quest")) return HideoutRequirementKind::QuestComplete;
        if (ContainsInsensitive(className, "Health")) return HideoutRequirementKind::Health;
        if (ContainsInsensitive(className, "BodyPart")) return HideoutRequirementKind::BodyPartBuff;
        if (ContainsInsensitive(className, "Version")) return HideoutRequirementKind::GameVersion;
        if (ContainsInsensitive(className, "Resource")) return HideoutRequirementKind::Resource;
        return HideoutRequirementKind::Unknown;
    }

    std::string ReadManagedString(std::uint64_t fieldAddress)
    {
        std::uint64_t stringObject = 0;
        if (!mem.TryRead(fieldAddress, stringObject, kReadMode) || !Utils::valid_pointer(stringObject))
            return {};
        return mem.readUnityString(stringObject, 128, kReadMode);
    }

    bool IsReadyStatus(std::int32_t status)
    {
        return status == 2 || status == 4 || status == 6 || status == 8;
    }
}

const char* HideoutAreaName(std::int32_t areaType)
{
    static const char* const names[] = {
        "Vents", "Security", "Lavatory", "Stash", "Generator", "Heating", "Water Collector", "Medstation", "Nutrition Unit", "Rest Space",
        "Workbench", "Intelligence Center", "Shooting Range", "Library", "Scav Case", "Illumination", "Hall of Fame", "Air Filtering Unit",
        "Solar Power", "Booze Generator", "Bitcoin Farm", "Christmas Tree", "Defective Wall", "Gym", "Weapon Rack", "Secondary Weapon Rack",
        "Gear Rack", "Cultist Circle"
    };
    if (areaType < 0 || areaType >= static_cast<std::int32_t>(std::size(names)))
        return "Unknown Zone";
    return names[areaType];
}

const char* HideoutAreaStatusName(std::int32_t status)
{
    static const char* const names[] = {
        "Not Set", "Locked to Construct", "Ready to Construct", "Constructing", "Ready to Install", "Locked to Upgrade", "Ready to Upgrade",
        "Upgrading", "Ready to Install", "Max Level", "Auto Upgrading"
    };
    if (status < 0 || status >= static_cast<std::int32_t>(std::size(names)))
        return "Unknown";
    return names[status];
}

const char* HideoutRequirementKindName(HideoutRequirementKind kind)
{
    switch (kind)
    {
    case HideoutRequirementKind::Area: return "Zone";
    case HideoutRequirementKind::Item: return "Item";
    case HideoutRequirementKind::TraderUnlock: return "Trader";
    case HideoutRequirementKind::TraderLoyalty: return "Trader loyalty";
    case HideoutRequirementKind::Skill: return "Skill";
    case HideoutRequirementKind::Resource: return "Resource";
    case HideoutRequirementKind::Tool: return "Tool";
    case HideoutRequirementKind::QuestComplete: return "Quest";
    case HideoutRequirementKind::Health: return "Health";
    case HideoutRequirementKind::BodyPartBuff: return "Body part";
    case HideoutRequirementKind::GameVersion: return "Game version";
    default: return "Unknown";
    }
}

HideoutOverview::~HideoutOverview()
{
    Stop();
}

void HideoutOverview::Configure(bool inRaid, bool dmaReady)
{
    const bool oldInRaid = inRaid_.exchange(inRaid, std::memory_order_acq_rel);
    const bool oldDmaReady = dmaReady_.exchange(dmaReady, std::memory_order_acq_rel);
    if (oldInRaid != inRaid || oldDmaReady != dmaReady)
    {
        contextSerial_.fetch_add(1, std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        if (snapshot_.state == HideoutScanState::Queued || snapshot_.state == HideoutScanState::Scanning)
        {
            snapshot_.state = HideoutScanState::Unavailable;
            snapshot_.message = inRaid ? "Scan cancelled because a raid became active." : !dmaReady ? "Scan cancelled because DMA became unavailable." : "Scan context changed; press Scan Hideout to retry.";
        }
    }
}

void HideoutOverview::RequestScan()
{
    const std::uint64_t request = requestSerial_.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_.state = HideoutScanState::Queued;
        snapshot_.message = "Hideout scan queued...";
        snapshot_.scanSerial = request;
    }

    std::lock_guard<std::mutex> workerLock(workerMutex_);
    if (!worker_.joinable())
        worker_ = std::jthread([this](std::stop_token stopToken) { WorkerLoop(stopToken); });
}

void HideoutOverview::Stop()
{
    std::jthread worker;
    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        if (!worker_.joinable())
            return;
        worker = std::move(worker_);
    }
    worker.request_stop();
}

HideoutOverviewSnapshot HideoutOverview::GetSnapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

std::vector<std::string> HideoutOverview::GetNeededItemIds() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    std::vector<std::string> ids;
    ids.reserve(snapshot_.neededItems.size());
    for (const HideoutNeededItem& item : snapshot_.neededItems)
    {
        if (!item.itemTemplateId.empty() && item.stillNeeded > 0)
            ids.push_back(item.itemTemplateId);
    }
    return ids;
}

void HideoutOverview::WorkerLoop(std::stop_token stopToken)
{
    std::uint64_t handledRequest = 0;
    while (!stopToken.stop_requested())
    {
        const std::uint64_t request = requestSerial_.load(std::memory_order_acquire);
        if (request != 0 && request != handledRequest)
        {
            const std::uint64_t context = contextSerial_.load(std::memory_order_acquire);
            RunScan(request, context);
            handledRequest = request;
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool HideoutOverview::CanRead(std::uint64_t request, std::uint64_t context) const
{
    return requestSerial_.load(std::memory_order_acquire) == request && contextSerial_.load(std::memory_order_acquire) == context &&
           !inRaid_.load(std::memory_order_acquire) && dmaReady_.load(std::memory_order_acquire);
}

void HideoutOverview::Publish(HideoutOverviewSnapshot snapshot, std::uint64_t request)
{
    if (requestSerial_.load(std::memory_order_acquire) != request)
        return;
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    if (requestSerial_.load(std::memory_order_acquire) == request)
        snapshot_ = std::move(snapshot);
}

std::string HideoutOverview::ReadBehaviourClassName(std::uint64_t behaviour) const
{
    if (!Utils::valid_pointer(behaviour))
        return {};
    std::uint64_t klass = 0;
    std::uint64_t name = 0;
    if (!mem.TryRead(behaviour, klass, kReadMode) || !Utils::valid_pointer(klass) ||
        !mem.TryRead(klass + 0x10, name, kReadMode) || !Utils::valid_pointer(name))
    {
        return {};
    }
    return mem.readString(name, 96, kReadMode);
}

void HideoutOverview::RunScan(std::uint64_t request, std::uint64_t context)
{
    HideoutOverviewSnapshot result{};
    result.state = HideoutScanState::Scanning;
    result.message = "Resolving Hideout objects...";
    result.scanSerial = request;
    Publish(result, request);

    if (inRaid_.load(std::memory_order_acquire))
    {
        result.state = HideoutScanState::Unavailable;
        result.message = "Hideout scanning is disabled while a raid is active.";
        Publish(std::move(result), request);
        return;
    }
    if (!dmaReady_.load(std::memory_order_acquire))
    {
        result.state = HideoutScanState::Unavailable;
        result.message = "DMA or the game process is not ready.";
        Publish(std::move(result), request);
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    const TarkovPointerSnapshot pointers = mem.GetTarkovPointerSnapshot();
    std::uint64_t gom = pointers.gameObjectManager;
    if (!Utils::valid_pointer(gom) && Utils::valid_pointer(pointers.unityPlayerBase))
        (void)mem.TryRead(pointers.unityPlayerBase + UnityOffsets::GameObjectManager, gom, kReadMode);
    result.gameObjectManager = gom;

    if (!Utils::valid_pointer(gom) || !CanRead(request, context))
    {
        result.state = HideoutScanState::Failed;
        result.message = "GameObjectManager was not available.";
        Publish(std::move(result), request);
        return;
    }

    std::uint64_t activeNode = 0;
    std::uint64_t lastNode = 0;
    if (!mem.TryRead(gom + UnityOffsets::GameObjectManager_ActiveNodesOffset, activeNode, kReadMode) ||
        !mem.TryRead(gom + UnityOffsets::GameObjectManager_LastActiveNodeOffset, lastNode, kReadMode) ||
        !Utils::valid_pointer(activeNode) || !Utils::valid_pointer(lastNode))
    {
        result.state = HideoutScanState::Failed;
        result.message = "The active Unity object list was not available.";
        Publish(std::move(result), request);
        return;
    }

    std::vector<std::uint64_t> nodeAddresses;
    nodeAddresses.reserve(2048);
    std::unordered_set<std::uint64_t> visited;
    visited.reserve(2048);
    bool reachedActiveNode = false;
    std::uint64_t current = lastNode;
    while (CanRead(request, context) && Utils::valid_pointer(current) && nodeAddresses.size() < kMaxGameObjects && visited.emplace(current).second)
    {
        nodeAddresses.push_back(current);
        if (current == activeNode)
        {
            reachedActiveNode = true;
            break;
        }
        std::uint64_t previous = 0;
        if (!mem.TryRead(current + offsetof(ObjectListNode, previous), previous, kReadMode))
            break;
        current = previous;
    }

    if (!reachedActiveNode)
    {
        current = activeNode;
        while (CanRead(request, context) && Utils::valid_pointer(current) && nodeAddresses.size() < kMaxGameObjects && visited.emplace(current).second)
        {
            nodeAddresses.push_back(current);
            if (current == lastNode)
                break;
            std::uint64_t next = 0;
            if (!mem.TryRead(current + offsetof(ObjectListNode, next), next, kReadMode))
                break;
            current = next;
        }
    }

    result.objectsScanned = static_cast<std::int32_t>(nodeAddresses.size());
    result.message = "Inspecting Hideout components...";
    Publish(result, request);

    if (nodeAddresses.empty() || !CanRead(request, context))
        return;

    std::vector<ObjectListNode> nodes(nodeAddresses.size());
    ScatterReadBatch nodeBatch(mem, kReadMode, "Hideout overview nodes");
    if (!nodeBatch.Valid())
    {
        result.state = HideoutScanState::Failed;
        result.message = "Could not create the Unity object read batch.";
        Publish(std::move(result), request);
        return;
    }
    for (std::size_t i = 0; i < nodeAddresses.size(); ++i)
        nodeBatch.AddBytes(nodeAddresses[i], &nodes[i], sizeof(ObjectListNode));
    if (!CanRead(request, context) || !nodeBatch.Execute())
        return;

    std::unordered_map<std::uint64_t, std::string> classNames;
    for (const ObjectListNode& node : nodes)
    {
        if (!CanRead(request, context))
            return;
        if (!Utils::valid_pointer(node.object))
            continue;

        ComponentArray components{};
        if (!mem.TryRead(node.object + UnityOffsets::GameObject_ComponentsOffset, components, kReadMode) ||
            !Utils::valid_pointer(components.entries) || components.size == 0 || components.size > 0x400)
        {
            continue;
        }

        std::vector<ComponentEntry> entries(static_cast<std::size_t>(components.size));
        if (!mem.Read(components.entries, entries.data(), entries.size() * sizeof(ComponentEntry), kReadMode, "Hideout overview components"))
            continue;

        for (const ComponentEntry& entry : entries)
        {
            if (!Utils::valid_pointer(entry.component))
                continue;
            std::uint64_t behaviour = 0;
            std::uint64_t klass = 0;
            if (!mem.TryRead(entry.component + kEftComponentObjectClassOffset, behaviour, kReadMode) || !Utils::valid_pointer(behaviour) ||
                !mem.TryRead(behaviour, klass, kReadMode) || !Utils::valid_pointer(klass))
            {
                continue;
            }

            auto nameIt = classNames.find(klass);
            if (nameIt == classNames.end())
                nameIt = classNames.emplace(klass, ReadBehaviourClassName(behaviour)).first;
            if (_stricmp(nameIt->second.c_str(), "HideoutArea") == 0 && !result.hideoutArea)
                result.hideoutArea = behaviour;
            else if (_stricmp(nameIt->second.c_str(), "HideoutController") == 0 && !result.hideoutController)
                result.hideoutController = behaviour;
            if (Utils::valid_pointer(result.hideoutArea) && Utils::valid_pointer(result.hideoutController))
                break;
        }
        if (Utils::valid_pointer(result.hideoutArea) && Utils::valid_pointer(result.hideoutController))
            break;
    }

    if (!Utils::valid_pointer(result.hideoutController))
    {
        result.state = HideoutScanState::Failed;
        result.message = "HideoutController was not found. Enter the Hideout, wait for it to load, then scan again.";
        Publish(std::move(result), request);
        return;
    }

    std::uint64_t dictionary = 0;
    std::int32_t dictionaryCount = 0;
    std::uint64_t entriesArray = 0;
    (void)mem.TryRead(result.hideoutController + sdk::HideoutController::Areas, dictionary, kReadMode);
    if (!Utils::valid_pointer(dictionary) || !mem.TryRead(dictionary + 0x20, dictionaryCount, kReadMode) || dictionaryCount <= 0 || dictionaryCount > kMaxAreas ||
        !mem.TryRead(dictionary + 0x18, entriesArray, kReadMode) || !Utils::valid_pointer(entriesArray))
    {
        result.state = HideoutScanState::Failed;
        result.message = "HideoutController was found, but its area dictionary was not valid.";
        Publish(std::move(result), request);
        return;
    }

    result.areasDictionary = dictionary;
    result.dictionaryCount = dictionaryCount;
    result.message = "Reading zones and next-level requirements...";
    Publish(result, request);

    std::vector<DictionaryEntry> areaEntries(static_cast<std::size_t>(dictionaryCount));
    if (!mem.Read(entriesArray + 0x20, areaEntries.data(), areaEntries.size() * sizeof(DictionaryEntry), kReadMode, "Hideout area dictionary"))
    {
        result.state = HideoutScanState::Failed;
        result.message = "Could not read the Hideout area dictionary entries.";
        Publish(std::move(result), request);
        return;
    }

    std::unordered_set<std::int32_t> seenAreas;
    for (const DictionaryEntry& entry : areaEntries)
    {
        if (!CanRead(request, context))
            return;
        if (entry.hashCode < 0 || entry.key < 0 || entry.key > 127 || !Utils::valid_pointer(entry.value) || !seenAreas.emplace(entry.key).second)
            continue;

        HideoutZoneInfo zone{};
        zone.areaType = entry.key;
        zone.area = entry.value;
        (void)mem.TryRead(zone.area + sdk::HideoutArea::Data, zone.data, kReadMode);
        (void)mem.TryRead(zone.area + sdk::HideoutArea::Levels, zone.levels, kReadMode);
        if (!Utils::valid_pointer(zone.data))
            continue;
        (void)mem.TryRead(zone.data + sdk::HideoutAreaData::CurrentLevel, zone.currentLevel, kReadMode);
        (void)mem.TryRead(zone.data + sdk::HideoutAreaData::Status, zone.status, kReadMode);
        zone.maxLevel = zone.status == 9;
        if (zone.maxLevel)
            ++result.maxLevelZones;
        if (IsReadyStatus(zone.status))
            ++result.readyZones;

        std::int32_t levelCount = 0;
        if (!zone.maxLevel && Utils::valid_pointer(zone.levels) && mem.TryRead(zone.levels + 0x18, levelCount, kReadMode) &&
            levelCount > 0 && levelCount <= kMaxLevels && zone.currentLevel >= 0 && zone.currentLevel + 1 < levelCount)
        {
            const std::uint64_t nextAddress = zone.levels + 0x20 + static_cast<std::uint64_t>(zone.currentLevel + 1) * sizeof(std::uint64_t);
            (void)mem.TryRead(nextAddress, zone.nextLevel, kReadMode);
            if (Utils::valid_pointer(zone.nextLevel))
                (void)mem.TryRead(zone.nextLevel + sdk::HideoutAreaLevel::Stage, zone.stage, kReadMode);
            zone.nextStageValid = Utils::valid_pointer(zone.stage);
        }

        if (zone.nextStageValid)
        {
            std::uint64_t relatedRequirements = 0;
            std::uint64_t requirementList = 0;
            std::uint64_t requirementArray = 0;
            std::int32_t requirementCount = 0;
            (void)mem.TryRead(zone.stage + sdk::HideoutStage::Requirements, relatedRequirements, kReadMode);
            if (Utils::valid_pointer(relatedRequirements))
                (void)mem.TryRead(relatedRequirements + sdk::HideoutRelatedRequirements::Data, requirementList, kReadMode);
            if (Utils::valid_pointer(requirementList))
            {
                (void)mem.TryRead(requirementList + 0x10, requirementArray, kReadMode);
                (void)mem.TryRead(requirementList + 0x18, requirementCount, kReadMode);
            }

            if (Utils::valid_pointer(requirementArray) && requirementCount >= 0 && requirementCount <= kMaxRequirements)
            {
                std::vector<std::uint64_t> requirementPointers(static_cast<std::size_t>(requirementCount));
                if (requirementCount == 0 || mem.Read(requirementArray + 0x20, requirementPointers.data(), requirementPointers.size() * sizeof(std::uint64_t),
                                                      kReadMode, "Hideout requirements"))
                {
                    for (const std::uint64_t requirementAddress : requirementPointers)
                    {
                        if (!Utils::valid_pointer(requirementAddress) || !CanRead(request, context))
                            continue;
                        HideoutRequirementInfo requirement{};
                        requirement.address = requirementAddress;
                        requirement.runtimeClass = ReadBehaviourClassName(requirementAddress);
                        requirement.kind = ClassifyRequirement(requirement.runtimeClass);
                        (void)mem.TryRead(requirementAddress + sdk::HideoutRequirement::Fulfilled, requirement.fulfilled, kReadMode);

                        if (requirement.kind == HideoutRequirementKind::Item || requirement.kind == HideoutRequirementKind::Tool)
                        {
                            requirement.itemTemplateId = ReadManagedString(requirementAddress + sdk::HideoutItemRequirement::TemplateId);
                            (void)mem.TryRead(requirementAddress + sdk::HideoutItemRequirement::UserItemsCount, requirement.currentCount, kReadMode);
                            (void)mem.TryRead(requirementAddress + sdk::HideoutItemRequirement::RequiredCount, requirement.requiredCount, kReadMode);
                            requirement.currentCount = (std::max)(0, requirement.currentCount);
                            requirement.requiredCount = (std::max)(0, requirement.requiredCount);
                            requirement.itemName = BSGidToName(requirement.itemTemplateId);
                            if (requirement.itemName.empty() || requirement.itemName == "NoData" || requirement.itemName == "ERR")
                                requirement.itemName = requirement.itemTemplateId.empty() ? "Unknown item" : requirement.itemTemplateId;
                        }
                        else if (requirement.kind == HideoutRequirementKind::Area)
                        {
                            (void)mem.TryRead(requirementAddress + sdk::HideoutAreaRequirement::AreaType, requirement.requiredArea, kReadMode);
                            (void)mem.TryRead(requirementAddress + sdk::HideoutAreaRequirement::RequiredLevel, requirement.requiredLevel, kReadMode);
                        }
                        else if (requirement.kind == HideoutRequirementKind::Skill)
                        {
                            requirement.skillName = ReadManagedString(requirementAddress + sdk::HideoutSkillRequirement::SkillName);
                            (void)mem.TryRead(requirementAddress + sdk::HideoutSkillRequirement::SkillLevel, requirement.skillLevel, kReadMode);
                        }
                        else if (requirement.kind == HideoutRequirementKind::TraderLoyalty)
                        {
                            requirement.traderId = ReadManagedString(requirementAddress + sdk::HideoutTraderLoyaltyRequirement::TraderId);
                            (void)mem.TryRead(requirementAddress + sdk::HideoutTraderLoyaltyRequirement::LoyaltyLevel, requirement.loyaltyLevel, kReadMode);
                        }
                        else if (requirement.kind == HideoutRequirementKind::TraderUnlock)
                        {
                            requirement.traderId = ReadManagedString(requirementAddress + sdk::HideoutTraderUnlockRequirement::TraderId);
                        }

                        ++result.totalRequirements;
                        if (requirement.fulfilled)
                            ++result.fulfilledRequirements;
                        zone.requirements.push_back(std::move(requirement));
                    }
                }
            }
        }

        result.zones.push_back(std::move(zone));
    }

    std::sort(result.zones.begin(), result.zones.end(), [](const HideoutZoneInfo& left, const HideoutZoneInfo& right) { return left.areaType < right.areaType; });

    std::map<std::string, HideoutNeededItem> neededById;
    for (const HideoutZoneInfo& zone : result.zones)
    {
        for (const HideoutRequirementInfo& requirement : zone.requirements)
        {
            if ((requirement.kind != HideoutRequirementKind::Item && requirement.kind != HideoutRequirementKind::Tool) || requirement.itemTemplateId.empty())
                continue;
            const std::int32_t stillNeeded = (std::max)(0, requirement.requiredCount - requirement.currentCount);
            if (stillNeeded <= 0)
                continue;
            HideoutNeededItem& item = neededById[requirement.itemTemplateId];
            item.itemTemplateId = requirement.itemTemplateId;
            item.itemName = requirement.itemName;
            item.currentCount = (std::max)(item.currentCount, requirement.currentCount);
            item.requiredCount = (std::max)(item.requiredCount, requirement.requiredCount);
            item.stillNeeded = (std::max)(item.stillNeeded, stillNeeded);
            const std::string zoneName = HideoutAreaName(zone.areaType);
            if (std::find(item.zones.begin(), item.zones.end(), zoneName) == item.zones.end())
                item.zones.push_back(zoneName);
        }
    }

    for (auto& [id, item] : neededById)
    {
        item.marketPrice = (std::max)(0, Marketprice(id));
        result.missingItemCount += item.stillNeeded;
        result.estimatedMissingValue += static_cast<std::int64_t>(item.stillNeeded) * item.marketPrice;
        result.neededItems.push_back(std::move(item));
    }
    std::sort(result.neededItems.begin(), result.neededItems.end(), [](const HideoutNeededItem& left, const HideoutNeededItem& right)
    {
        const std::int64_t leftValue = static_cast<std::int64_t>(left.stillNeeded) * left.marketPrice;
        const std::int64_t rightValue = static_cast<std::int64_t>(right.stillNeeded) * right.marketPrice;
        if (leftValue != rightValue)
            return leftValue > rightValue;
        return left.itemName < right.itemName;
    });

    result.scanMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    result.state = result.zones.empty() ? HideoutScanState::Failed : HideoutScanState::Complete;
    result.message = result.zones.empty() ? "HideoutController was found, but no valid zones were read." : "Hideout scan complete.";
    Publish(std::move(result), request);
}
