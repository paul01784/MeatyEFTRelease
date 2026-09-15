#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

enum class HideoutScanState
{
    Idle,
    Queued,
    Scanning,
    Complete,
    Failed,
    Unavailable
};

enum class HideoutRequirementKind
{
    Area,
    Item,
    TraderUnlock,
    TraderLoyalty,
    Skill,
    Resource,
    Tool,
    QuestComplete,
    Health,
    BodyPartBuff,
    GameVersion,
    Unknown
};

struct HideoutRequirementInfo
{
    HideoutRequirementKind kind{ HideoutRequirementKind::Unknown };
    bool fulfilled{};
    std::string runtimeClass;
    std::string itemTemplateId;
    std::string itemName;
    std::int32_t currentCount{};
    std::int32_t requiredCount{};
    std::int32_t requiredArea{ -1 };
    std::int32_t requiredLevel{};
    std::string skillName;
    std::int32_t skillLevel{};
    std::string traderId;
    std::int32_t loyaltyLevel{};
    std::uint64_t address{};
};

struct HideoutZoneInfo
{
    std::int32_t areaType{ -1 };
    std::int32_t currentLevel{};
    std::int32_t status{};
    bool maxLevel{};
    bool nextStageValid{};
    std::uint64_t area{};
    std::uint64_t data{};
    std::uint64_t levels{};
    std::uint64_t nextLevel{};
    std::uint64_t stage{};
    std::vector<HideoutRequirementInfo> requirements;
};

struct HideoutNeededItem
{
    std::string itemTemplateId;
    std::string itemName;
    std::int32_t currentCount{};
    std::int32_t requiredCount{};
    std::int32_t stillNeeded{};
    std::int32_t marketPrice{};
    std::vector<std::string> zones;
};

struct HideoutOverviewSnapshot
{
    HideoutScanState state{ HideoutScanState::Idle };
    std::string message{ "Press Scan Hideout while inside the Hideout." };
    std::uint64_t scanSerial{};
    std::uint64_t gameObjectManager{};
    std::uint64_t hideoutArea{};
    std::uint64_t hideoutController{};
    std::uint64_t areasDictionary{};
    std::int32_t objectsScanned{};
    std::int32_t dictionaryCount{};
    std::int32_t fulfilledRequirements{};
    std::int32_t totalRequirements{};
    std::int32_t maxLevelZones{};
    std::int32_t readyZones{};
    std::int32_t missingItemCount{};
    std::int64_t estimatedMissingValue{};
    double scanMilliseconds{};
    std::vector<HideoutZoneInfo> zones;
    std::vector<HideoutNeededItem> neededItems;
};

class HideoutOverview
{
public:
    HideoutOverview() = default;
    ~HideoutOverview();

    HideoutOverview(const HideoutOverview&) = delete;
    HideoutOverview& operator=(const HideoutOverview&) = delete;

    void Configure(bool inRaid, bool dmaReady);
    void RequestScan();
    void Stop();

    [[nodiscard]] HideoutOverviewSnapshot GetSnapshot() const;
    [[nodiscard]] std::vector<std::string> GetNeededItemIds() const;

private:
    void WorkerLoop(std::stop_token stopToken);
    void RunScan(std::uint64_t request, std::uint64_t context);
    void Publish(HideoutOverviewSnapshot snapshot, std::uint64_t request);
    [[nodiscard]] bool CanRead(std::uint64_t request, std::uint64_t context) const;
    [[nodiscard]] std::string ReadBehaviourClassName(std::uint64_t behaviour) const;

    mutable std::mutex snapshotMutex_;
    HideoutOverviewSnapshot snapshot_{};

    std::mutex workerMutex_;
    std::jthread worker_;
    std::atomic_bool inRaid_{ false };
    std::atomic_bool dmaReady_{ false };
    std::atomic_uint64_t requestSerial_{ 0 };
    std::atomic_uint64_t contextSerial_{ 0 };
};

[[nodiscard]] const char* HideoutAreaName(std::int32_t areaType);
[[nodiscard]] const char* HideoutAreaStatusName(std::int32_t status);
[[nodiscard]] const char* HideoutRequirementKindName(HideoutRequirementKind kind);

extern HideoutOverview HIDEOUT_OVERVIEW;
