#include "headers/questManager.h"

#include "../memory/Memory.h"
#include "headers/sdk.h"
#include "headers/unityHelper.h"
#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/tarkovdevquery.h"

QuestManager questManager;
std::vector<QuestData> questDataActive;
std::vector<std::string> masterItems;
std::vector<QuestLocation> masterLocations;

std::mutex g_questCacheMutex;

static bool IsObjectiveCompleted(
    const TarkovObjective& obj,
    const std::vector<std::string>& completedConditions)
{
    // Pick best needle to search for in completedConditions
    const std::string* needle = nullptr;

    if (!obj.id.empty())               needle = &obj.id;          // best
    else if (!obj.questItemId.empty()) needle = &obj.questItemId; // next best
    else if (!obj.itemId.empty())      needle = &obj.itemId;      // fallback

    if (!needle || needle->empty())
        return false; // type-only objectives can't be reliably matched here

    for (const auto& cond : completedConditions)
    {
        if (Utils::Text::containsIgnoreCase(cond, *needle))
            return true;
    }

    return false;
}

static bool IsSkippedType(const std::string& type)
{
    for (const auto& skip : kSkipObjectiveTypes)
    {
        if (Utils::Text::containsIgnoreCase(type, skip))
            return true;
    }
    return false;
}

static bool IsCompletedById(const std::vector<std::string>& completedConditions, const std::string& objectiveId)
{
    if (objectiveId.empty())
        return false;

    for (const auto& cond : completedConditions)
    {
        if (Utils::Text::containsIgnoreCase(cond, objectiveId))
            return true;
    }
    return false;
}

static bool IsLocationObjectiveType(const std::string& type)
{
    return Utils::Text::containsIgnoreCase(type, "visit") ||
        Utils::Text::containsIgnoreCase(type, "mark") ||
        Utils::Text::containsIgnoreCase(type, "plantItem") ||
        Utils::Text::containsIgnoreCase(type, "findQuestItem");
}

static bool IsItemObjectiveType(const std::string& type)
{
    return Utils::Text::containsIgnoreCase(type, "findItem") ||
        Utils::Text::containsIgnoreCase(type, "findQuestItem");
}

static bool IsSupportedObjectiveType(const std::string& type)
{
    return IsLocationObjectiveType(type) || IsItemObjectiveType(type);
}

static bool IsConditionMatch(
    const std::vector<std::string>& completedConditions,
    const std::string& needle)
{
    const std::string want = TrimEFT(needle);

    if (want.empty())
        return false;

    for (const auto& cond : completedConditions)
    {
        if (TrimEFT(cond) == want)
            return true;
    }

    return false;
}

static void FilterConditions(
    const TarkovDevTasks& task,
    const QuestData& active,
    std::vector<ActiveObjective>& outObjectives,
    std::vector<std::string>& outMasterItems,
    std::vector<QuestLocation>& outMasterLocations)
{
    outObjectives.clear();

    for (const auto& obj : task.objectives)
    {
        // Only keep objective types we care about
        if (!IsSupportedObjectiveType(obj.type))
            continue;

        // Completed objective pruning
        // Important: item objectives may complete by obj.id, questItemId, or itemId
        if (IsObjectiveCompleted(obj, active.completedConditions))
            continue;

        // Keep active objective
        ActiveObjective ao{};
        ao.objectiveId = obj.id;
        ao.type = obj.type;
        ao.itemId = obj.itemId;
        ao.questItemId = obj.questItemId;
        ao.completed = false;
        ao.maps = obj.maps;

        ao.zones.reserve(obj.zones.size());

        for (const auto& z : obj.zones)
        {
            ActiveZone az{};
            az.mapNameId = z.mapNameId;
            az.position = z.position;
            ao.zones.emplace_back(std::move(az));
        }

        outObjectives.emplace_back(std::move(ao));

        
        // findItem / findQuestItem
        if (Utils::Text::containsIgnoreCase(obj.type, "findItem"))
        {
            if (!obj.itemId.empty())
                outMasterItems.emplace_back(obj.itemId);
        }

        if (Utils::Text::containsIgnoreCase(obj.type, "findQuestItem"))
        {
            if (!obj.questItemId.empty())
                outMasterItems.emplace_back(obj.questItemId);
        }

        
        // visit / mark / plantItem / findQuestItem
        if (!IsLocationObjectiveType(obj.type))
            continue;

        // Objective-level fallback map
        std::string fallbackMap;
        if (!obj.maps.empty())
            fallbackMap = TrimEFT(obj.maps[0]);

        // If zones exist, use them first
        if (!obj.zones.empty())
        {
            for (const auto& z : obj.zones)
            {
                std::string mapId = TrimEFT(z.mapNameId);

                if (mapId.empty())
                    mapId = fallbackMap;

                if (mapId.empty())
                    continue;

                QuestLocation loc{};
                loc.pos = z.position;
                loc.mapNameId = std::move(mapId);
                loc.questName = active.questName;
                loc.objectiveType = obj.type;
                loc.questId = active.questId;
                loc.objectiveId = obj.id;

                outMasterLocations.emplace_back(std::move(loc));
            }

            continue;
        }

        // No zones
        // plantItem / findQuestItem
        if (!fallbackMap.empty())
        {
            QuestLocation loc{};
            loc.pos = {};
            loc.mapNameId = std::move(fallbackMap);
            loc.questName = active.questName;
            loc.objectiveType = obj.type;
            loc.questId = active.questId;
            loc.objectiveId = obj.id;

            outMasterLocations.emplace_back(std::move(loc));
        }
    }
}

static const char* QuestStatusToStr(int s)
{
    switch (s)
    {
    case 0: return "Locked/Unknown(0)";
    case 1: return "Available(1)";
    case 2: return "Started(2)";
    case 3: return "AvailableForFinish(3)";
    case 4: return "Success(4)";
    case 5: return "Fail(5)";
    default: return "Unknown(?)";
    }
}

static std::string Hex64(uint64_t v)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << v << std::dec;
    return oss.str();
}

static bool ContainsExact(const std::vector<std::string>& v, const std::string& needle)
{
    for (const auto& s : v)
        if (s == needle) return true;
    return false;
}

void QuestManager::initQuestManager()
{
    ClearPublishedQuestState();

    std::vector<QuestData> newQuestDataActive;
    std::vector<std::string> newMasterItems;
    std::vector<QuestLocation> newMasterLocations;

    try
    {
        if (!Utils::valid_pointer(mainGame.localplayerProfile))
            return;

        const uint64_t questData = mem.Read<uint64_t>(
            mainGame.localplayerProfile + sdk::Profile::QuestsData
        );

        if (!Utils::valid_pointer(questData))
            return;

        MonoList<uint64_t> questDataList(questData);

        if (questDataList.count < 1 || questDataList.count > 512)
            return;

        for (int i = 0; i < questDataList.count; ++i)
        {
            const uint64_t qDataEntry = questDataList[i];
            if (!Utils::valid_pointer(qDataEntry))
                continue;

            const int qStatus = mem.Read<int>(
                qDataEntry + sdk::QuestsData::Status
            );

            if (qStatus != 2) // started
                continue;

            const uint64_t qIdStrPtr = mem.Read<uint64_t>(
                qDataEntry + sdk::QuestsData::Id
            );

            if (!Utils::valid_pointer(qIdStrPtr))
                continue;

            int qIdLen = mem.Read<int>(qIdStrPtr + 0x10);

            if (qIdLen <= 0 || qIdLen > 256)
                continue;

            std::string qID = mem.readUnicodeString(qIdStrPtr + 0x14, qIdLen);
            qID = TrimEFT(std::move(qID));

            if (qID.empty())
                continue;

            const TarkovDevTasks* task = nullptr;

            for (const auto& t : tarkovDevTasksData)
            {
                if (t.qID == qID)
                {
                    task = &t;
                    break;
                }
            }

            if (!task)
                continue;

            std::vector<std::string> completedConditions;

            const uint64_t completedPtr = mem.Read<uint64_t>(
                qDataEntry + sdk::QuestsData::CompletedConditions
            );

            if (Utils::valid_pointer(completedPtr))
            {
                auto completedHS = UnityHashSet<MongoID>::Create(completedPtr, mem);

                const size_t reserveCount = std::min<size_t>(
                    static_cast<size_t>(completedHS.size()),
                    512
                );

                completedConditions.reserve(reserveCount);

                for (const auto& e : completedHS.entries)
                {
                    if (e.hashCode < 0)
                        continue;

                    std::string cond = e.value.ReadString(mem);
                    cond = TrimEFT(std::move(cond));

                    if (!cond.empty())
                        completedConditions.emplace_back(std::move(cond));
                }
            }

            QuestData q{};
            q.questPtr = qDataEntry;
            q.questId = qID;
            q.questName = task->qName;
            q.status = QuestStatus::Started;
            q.completedConditions = std::move(completedConditions);

            FilterConditions(
                *task,
                q,
                q.objectives,
                newMasterItems,
                newMasterLocations
            );

            newQuestDataActive.emplace_back(std::move(q));
        }

        {
            std::lock_guard<std::mutex> lock(g_questCacheMutex);

            questDataActive = std::move(newQuestDataActive);
            masterItems = std::move(newMasterItems);
            masterLocations = std::move(newMasterLocations);
        }

        std::cout << "\n[Quest Manager] Active quests: " << questDataActive.size() << "\n";
        std::cout << "[Quest Manager] MasterItems: " << masterItems.size()
            << " | MasterLocations: " << masterLocations.size() << "\n";
    }
    catch (const std::exception& e)
    {
        LOGS.logError("Exception caught in initQuestManager: " + std::string(e.what()));
        ClearPublishedQuestState();
    }
    catch (...)
    {
        LOGS.logError("Unknown exception caught in initQuestManager");
        ClearPublishedQuestState();
    }
}

void QuestManager::updateAndPruneActiveQuests()
{
    try
    {
        std::vector<QuestData> activeSnapshot;

        {
            std::lock_guard<std::mutex> lock(g_questCacheMutex);

            if (questDataActive.empty())
                return;

            activeSnapshot = questDataActive;
        }

        if (!Utils::valid_pointer(mainGame.localplayerProfile))
        {
            ClearPublishedQuestState();
            return;
        }

        std::vector<QuestData> updated;
        updated.reserve(activeSnapshot.size());

        std::vector<std::string> newMasterItems;
        std::vector<QuestLocation> newMasterLocations;

        for (auto& q : activeSnapshot)
        {
            if (q.questId.empty())
                continue;

            const uint64_t liveQuestPtr = findLiveQuestPtrById(q.questId);

            if (!Utils::valid_pointer(liveQuestPtr))
                continue;

            const int qStatus = mem.Read<int>(
                liveQuestPtr + sdk::QuestsData::Status
            );

            if (qStatus != 2)
                continue;

            std::vector<std::string> completedConditions;

            const uint64_t completedPtr = mem.Read<uint64_t>(
                liveQuestPtr + sdk::QuestsData::CompletedConditions
            );

            if (Utils::valid_pointer(completedPtr))
            {
                auto completedHS = UnityHashSet<MongoID>::Create(completedPtr, mem);

                const size_t reserveCount = std::min<size_t>(
                    static_cast<size_t>(completedHS.size()),
                    512
                );

                completedConditions.reserve(reserveCount);

                for (const auto& e : completedHS.entries)
                {
                    if (e.hashCode < 0)
                        continue;

                    std::string cond = e.value.ReadString(mem);
                    cond = TrimEFT(std::move(cond));

                    if (!cond.empty())
                        completedConditions.emplace_back(std::move(cond));
                }
            }

            const TarkovDevTasks* task = nullptr;

            for (const auto& t : tarkovDevTasksData)
            {
                if (t.qID == q.questId)
                {
                    task = &t;
                    break;
                }
            }

            if (!task)
                continue;

            QuestData fresh = q;
            fresh.questPtr = liveQuestPtr;
            fresh.completedConditions = std::move(completedConditions);
            fresh.status = QuestStatus::Started;

            std::vector<ActiveObjective> rebuiltObjectives;

            FilterConditions(
                *task,
                fresh,
                rebuiltObjectives,
                newMasterItems,
                newMasterLocations
            );

            if (rebuiltObjectives.empty())
                continue;

            fresh.objectives = std::move(rebuiltObjectives);
            updated.emplace_back(std::move(fresh));
        }

        {
            std::lock_guard<std::mutex> lock(g_questCacheMutex);

            questDataActive = std::move(updated);
            masterItems = std::move(newMasterItems);
            masterLocations = std::move(newMasterLocations);
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError("Exception caught in updateAndPruneActiveQuests: " + std::string(e.what()) + ". Retrying...");
        return;
    }
    catch (...)
    {
        LOGS.logError("Unknown exception caught in updateAndPruneActiveQuests. Retrying...");
        return;
    }
}

uint64_t QuestManager::findLiveQuestPtrById(const std::string& wantedQuestId)
{
    if (!Utils::valid_pointer(mainGame.localplayerProfile))
        return 0;

    const uint64_t questData = mem.Read<uint64_t>(mainGame.localplayerProfile + sdk::Profile::QuestsData);
    if (!Utils::valid_pointer(questData))
        return 0;

    MonoList<uint64_t> questDataList(questData);
    if (questDataList.count < 1)
        return 0;

    for (int i = 0; i < questDataList.count; ++i)
    {
        const uint64_t qDataEntry = questDataList[i];
        if (!Utils::valid_pointer(qDataEntry))
            continue;

        const uint64_t qIdStrPtr = mem.Read<uint64_t>(qDataEntry + sdk::QuestsData::Id);
        if (!Utils::valid_pointer(qIdStrPtr))
            continue;

        int qIdLen = mem.Read<int>(qIdStrPtr + 0x10);
        if (qIdLen <= 0)
            continue;

        if (qIdLen > 256)
            qIdLen = 256;

        std::string qID = mem.readUnicodeString(qIdStrPtr + 0x14, qIdLen);
        qID = TrimEFT(std::move(qID));

        if (qID == wantedQuestId)
            return qDataEntry;
    }

    return 0;
}

template <typename TObjective>
static bool IsObjectiveCompleted(
    const TObjective& obj,
    const std::vector<std::string>& completedConditions)
{
    // Normal objective id
    if (IsConditionMatch(completedConditions, obj.id))
        return true;

    // Item objectives can complete using different IDs
    if (IsItemObjectiveType(obj.type))
    {
        if (IsConditionMatch(completedConditions, obj.questItemId))
            return true;

        if (IsConditionMatch(completedConditions, obj.itemId))
            return true;
    }

    return false;
}