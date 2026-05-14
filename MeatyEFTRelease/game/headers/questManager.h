#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <chrono>
#include <stop_token>
#include <optional>

#include <string_view>
#include <glm/glm.hpp>

enum class QuestObjectiveType
{
    Unknown = 0,        // for unrecognized / missing types
    FindQuestItem,      // "findQuestItem"
    GiveQuestItem,      // "giveQuestItem"
    PlantItem,          // "plantItem"
    PlantQuestItem,     // "plantQuestItem"
    BuildWeapon,        // "buildWeapon"
    FindItem,           // "findItem"
    GiveItem,           // "giveItem"
    Visit,              // "visit"
    Shoot,              // "shoot"
    Skill,              // "skill"
    Extract,            // "extract"
    Mark,               // "mark"
    Experience,         // "experience"
    UseItem,            // "useItem"
    SellItem,           // "sellItem"
    TraderLevel,        // "traderLevel"
    TaskStatus,         // "taskStatus"
    TraderStanding      // "traderStanding"
};

enum class QuestObjectiveStatus : uint8_t
{
    Unknown = 0,
    Active,
    Completed,
    Failed
};

enum class QuestStatus : uint8_t
{
    Unknown = 0,
    Locked,
    Available,
    Started,
    Completed,
    Failed
};

static const std::vector<std::string> kSkipObjectiveTypes = {
    "shoot",
    "skill",
    "experience",
    "extract",
    "traderLevel",
    "traderStanding",
    "taskStatus",
    "buildWeapon"
};

struct ActiveZone
{
    std::string mapNameId;
    glm::vec3 position{};
};

struct QuestLocation
{
    std::string mapNameId;
    glm::vec3 pos{};

    std::string questName;
    std::string objectiveType;   // "visit" / "mark" / "plantItem"

    std::string questId;
    std::string objectiveId;
};

struct QuestObjectiveData
{
    std::string objectiveId;          // if available from memory or matched later
    QuestObjectiveType objectiveType{ QuestObjectiveType::Unknown };
    std::string objectiveTypeRaw;     // e.g. "findQuestItem"

    int current{ 0 };
    int target{ 0 };
    QuestObjectiveStatus status{ QuestObjectiveStatus::Unknown };

    std::string mapNameId;

    std::string itemId;               // for FindItem/GiveItem/etc.
    std::string questItemId;          // for FindQuestItem/GiveQuestItem/etc.
};

struct ActiveObjective
{
    std::string objectiveId;
    std::string type;
    bool completed{ false }; // will be false for everything we keep (we skip completed)

    std::string itemId;
    std::string questItemId;

    std::vector<std::string> maps;   // for quest item objectives
    std::vector<ActiveZone> zones;   // for visit/mark/plantItem objectives
};

struct QuestData
{
    uint64_t questPtr{};
    std::string questId;
    std::string questName;
    QuestStatus status{ QuestStatus::Unknown };

    std::vector<std::string> completedConditions;

    std::vector<ActiveObjective> objectives;
};

struct ActiveQuestState
{
    std::vector<QuestData> activeQuests;
};

class QuestManager
{
    public:
        void initQuestManager();

        void updateAndPruneActiveQuests();

        uint64_t findLiveQuestPtrById(const std::string& wantedQuestId);

    private:
   
};

extern QuestManager questManager;
extern std::vector<QuestData> questDataActive;
extern std::vector<std::string> masterItems;
extern std::vector<QuestLocation> masterLocations;

extern std::mutex g_questCacheMutex;

extern std::vector<QuestData> questDataActive;
extern std::vector<std::string> masterItems;
extern std::vector<QuestLocation> masterLocations;

inline void ClearPublishedQuestState()
{
    std::lock_guard<std::mutex> lock(g_questCacheMutex);

    questDataActive.clear();
    masterItems.clear();
    masterLocations.clear();
}

inline std::vector<QuestLocation> GetMasterLocationsSnapshot()
{
    std::lock_guard<std::mutex> lock(g_questCacheMutex);
    return masterLocations;
}

inline std::vector<std::string> GetMasterItemsSnapshot()
{
    std::lock_guard<std::mutex> lock(g_questCacheMutex);
    return masterItems;
}

inline std::vector<QuestData> GetQuestDataActiveSnapshot()
{
    std::lock_guard<std::mutex> lock(g_questCacheMutex);
    return questDataActive;
}

