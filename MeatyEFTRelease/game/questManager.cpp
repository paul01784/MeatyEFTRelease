#include "headers/questManager.h"

#include "../memory/Memory.h"
#include "headers/sdk.h"
#include "headers/unityHelper.h"
#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/tarkovdevquery.h"

#include <array>
#include <limits>
#include <unordered_map>

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
        Utils::Text::containsIgnoreCase(type, "plantQuestItem") ||
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
        ao.description = obj.description;
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

        
        // visit / mark / plantItem / plantQuestItem / findQuestItem
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
        // plantItem / plantQuestItem / findQuestItem
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

namespace
{
    constexpr std::int32_t kMaxCompletedConditionEntries = 2048;
    constexpr int kMaxConditionIdChars = 128;

    using CompletedConditionEntry = UnityHashSet<MongoID>::MemHashEntry;

    class QuestScatterBatch
    {
    public:
        QuestScatterBatch()
            : handle_(mem.CreateScatterHandle(false))
        {
        }

        ~QuestScatterBatch()
        {
            if (handle_)
                mem.CloseScatterHandle(handle_);
        }

        QuestScatterBatch(const QuestScatterBatch&) = delete;
        QuestScatterBatch& operator=(const QuestScatterBatch&) = delete;

        bool valid() const
        {
            return handle_ != nullptr;
        }

        template <typename T>
        bool Queue(std::uint64_t address, T& destination)
        {
            static_assert(
                std::is_trivially_copyable_v<T>,
                "Quest scatter destinations must be trivially copyable"
            );

            return QueueBytes(address, &destination, sizeof(T));
        }

        bool QueueBytes(
            std::uint64_t address,
            void* destination,
            std::size_t size)
        {
            if (!handle_ || !Utils::valid_pointer(address) ||
                !destination || size == 0)
            {
                return false;
            }

            if (!mem.AddScatterReadRequest(
                handle_,
                address,
                destination,
                size))
            {
                return false;
            }

            ++queuedReads_;
            return true;
        }

        bool Execute(const char* label)
        {
            if (!handle_)
                return false;

            if (queuedReads_ == 0)
                return true;

            const bool ok =
                mem.ExecuteReadScatter(handle_, false, label);

            queuedReads_ = 0;
            return ok;
        }

    private:
        VMMDLL_SCATTER_HANDLE handle_ = nullptr;
        std::size_t queuedReads_ = 0;
    };

    struct LiveQuestRead
    {
        std::size_t snapshotIndex = 0;
        std::uint64_t questPtr = 0;
        int status = 0;
        std::uint64_t completedPtr = 0;
        std::vector<std::string> completedConditions;
    };

    struct CompletedSetRead
    {
        std::size_t liveQuestIndex = 0;
        std::int32_t count = 0;
        std::uint64_t entriesArray = 0;
        std::vector<CompletedConditionEntry> entries;
    };

    struct ConditionStringRead
    {
        std::size_t liveQuestIndex = 0;
        std::uint64_t stringPtr = 0;
        int charCount = 0;
        std::array<wchar_t, kMaxConditionIdChars + 1> chars{};
    };

    std::string WideCharsToUtf8(
        const wchar_t* chars,
        std::size_t maxChars)
    {
        if (!chars || maxChars == 0)
            return {};

        std::size_t actualChars = 0;

        while (actualChars < maxChars && chars[actualChars] != L'\0')
            ++actualChars;

        if (actualChars == 0 ||
            actualChars > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return {};
        }

        const int sourceChars = static_cast<int>(actualChars);
        const int utf8Size = WideCharToMultiByte(
            CP_UTF8,
            0,
            chars,
            sourceChars,
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (utf8Size <= 0)
            return {};

        std::string result(static_cast<std::size_t>(utf8Size), '\0');

        WideCharToMultiByte(
            CP_UTF8,
            0,
            chars,
            sourceChars,
            result.data(),
            utf8Size,
            nullptr,
            nullptr
        );

        return result;
    }

    bool ReadCompletedConditions(
        QuestScatterBatch& scatter,
        std::vector<LiveQuestRead>& liveQuests)
    {
        std::vector<CompletedSetRead> sets;
        sets.reserve(liveQuests.size());

        for (std::size_t i = 0; i < liveQuests.size(); ++i)
        {
            const auto& live = liveQuests[i];

            if (live.status != 2 ||
                !Utils::valid_pointer(live.completedPtr))
            {
                continue;
            }

            CompletedSetRead set{};
            set.liveQuestIndex = i;
            sets.emplace_back(std::move(set));
        }

        for (auto& set : sets)
        {
            const std::uint64_t completedPtr =
                liveQuests[set.liveQuestIndex].completedPtr;

            if (!scatter.Queue(
                completedPtr + UnityHashSet<MongoID>::CountOffset,
                set.count))
            {
                return false;
            }

            if (!scatter.Queue(
                completedPtr + UnityHashSet<MongoID>::ArrOffset,
                set.entriesArray))
            {
                return false;
            }
        }

        if (!scatter.Execute("Quest completed headers"))
            return false;

        std::size_t totalEntryCount = 0;

        for (auto& set : sets)
        {
            if (set.count < 0 ||
                set.count > kMaxCompletedConditionEntries)
            {
                return false;
            }

            if (set.count == 0)
                continue;

            if (!Utils::valid_pointer(set.entriesArray))
                return false;

            const std::size_t entryCount =
                static_cast<std::size_t>(set.count);

            set.entries.resize(entryCount);
            totalEntryCount += entryCount;

            if (!scatter.QueueBytes(
                set.entriesArray + UnityHashSet<MongoID>::ArrStartOffset,
                set.entries.data(),
                entryCount * sizeof(CompletedConditionEntry)))
            {
                return false;
            }
        }

        if (!scatter.Execute("Quest completed entries"))
            return false;

        std::vector<ConditionStringRead> conditionStrings;
        conditionStrings.reserve(totalEntryCount);

        for (const auto& set : sets)
        {
            for (const auto& entry : set.entries)
            {
                if (entry.hashCode < 0 ||
                    !Utils::valid_pointer(entry.value._stringId))
                {
                    continue;
                }

                ConditionStringRead read{};
                read.liveQuestIndex = set.liveQuestIndex;
                read.stringPtr = entry.value._stringId;
                conditionStrings.emplace_back(std::move(read));
            }
        }

        for (auto& read : conditionStrings)
        {
            if (!scatter.Queue(read.stringPtr + 0x10, read.charCount))
                return false;
        }

        if (!scatter.Execute("Quest condition lengths"))
            return false;

        for (auto& read : conditionStrings)
        {
            if (read.charCount <= 0)
                continue;

            const int charsToRead =
                (std::min)(read.charCount, kMaxConditionIdChars);

            if (!scatter.QueueBytes(
                read.stringPtr + 0x14,
                read.chars.data(),
                static_cast<std::size_t>(charsToRead) * sizeof(wchar_t)))
            {
                return false;
            }
        }

        if (!scatter.Execute("Quest condition strings"))
            return false;

        for (const auto& read : conditionStrings)
        {
            if (read.charCount <= 0)
                continue;

            const std::size_t charsRead = static_cast<std::size_t>(
                (std::min)(read.charCount, kMaxConditionIdChars)
            );

            std::string condition =
                TrimEFT(WideCharsToUtf8(read.chars.data(), charsRead));

            if (!condition.empty())
            {
                liveQuests[read.liveQuestIndex]
                    .completedConditions
                    .emplace_back(std::move(condition));
            }
        }

        return true;
    }
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
        if (!espGlobals::drawQuestHelper && !radarGlobals::drawQuestHelper)
            return;

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

        QuestScatterBatch scatter;

        if (!scatter.valid())
            return;

        std::vector<LiveQuestRead> liveQuests;
        liveQuests.reserve(activeSnapshot.size());

        for (std::size_t i = 0; i < activeSnapshot.size(); ++i)
        {
            const auto& quest = activeSnapshot[i];

            if (quest.questId.empty() ||
                !Utils::valid_pointer(quest.questPtr))
            {
                continue;
            }

            LiveQuestRead live{};
            live.snapshotIndex = i;
            live.questPtr = quest.questPtr;
            liveQuests.emplace_back(std::move(live));
        }

        for (auto& live : liveQuests)
        {
            if (!scatter.Queue(
                live.questPtr + sdk::QuestsData::Status,
                live.status))
            {
                return;
            }

            if (!scatter.Queue(
                live.questPtr + sdk::QuestsData::CompletedConditions,
                live.completedPtr))
            {
                return;
            }
        }

        if (!scatter.Execute("Quest live state"))
            return;

        if (!ReadCompletedConditions(scatter, liveQuests))
            return;

        std::unordered_map<std::string, const TarkovDevTasks*> tasksById;
        tasksById.reserve(tarkovDevTasksData.size());

        for (const auto& task : tarkovDevTasksData)
            tasksById.emplace(task.qID, &task);

        for (auto& live : liveQuests)
        {
            if (live.status != 2)
                continue;

            QuestData fresh = activeSnapshot[live.snapshotIndex];

            const auto taskIt = tasksById.find(fresh.questId);

            if (taskIt == tasksById.end())
                continue;

            fresh.questPtr = live.questPtr;
            fresh.completedConditions =
                std::move(live.completedConditions);
            fresh.status = QuestStatus::Started;

            std::vector<ActiveObjective> rebuiltObjectives;

            FilterConditions(
                *taskIt->second,
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
