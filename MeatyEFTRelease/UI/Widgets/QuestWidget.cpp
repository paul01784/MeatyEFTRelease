#include "QuestWidget.h"

#include "../includes.h"
#include "../config.h"
#include "../globals.h"
#include "../../Tarkov/GameWorld/QuestManager.h"
#include "../../Web/TarkovDev/TarkovDevClient.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace uiWidgets
{
static std::string toLower(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return result;
}

static const char* QuestStatusLabel(QuestStatus status)
{
    switch (status)
    {
    case QuestStatus::Locked: return "Locked";
    case QuestStatus::Available: return "Available";
    case QuestStatus::Started: return "Started";
    case QuestStatus::Completed: return "Completed";
    case QuestStatus::Failed: return "Failed";
    default: return "Unknown";
    }
}

static const char* QuestObjectiveTypeLabel(const std::string& type)
{
    if (type == "findQuestItem") return "Find quest item";
    if (type == "giveQuestItem") return "Hand over quest item";
    if (type == "plantItem") return "Plant item";
    if (type == "plantQuestItem") return "Plant quest item";
    if (type == "findItem") return "Find item";
    if (type == "giveItem") return "Hand over item";
    if (type == "visit") return "Visit location";
    if (type == "mark") return "Mark location";
    if (type == "extract") return "Extract";
    if (type == "shoot") return "Eliminate target";
    if (type == "buildWeapon") return "Build weapon";
    if (type == "experience") return "Gain experience";
    if (type == "skill") return "Reach skill level";
    if (type == "useItem") return "Use item";
    if (type == "sellItem") return "Sell item";
    if (type == "traderLevel") return "Reach trader level";
    if (type == "traderStanding") return "Reach trader standing";
    if (type == "taskStatus") return "Complete task";

    return type.empty() ? "Unknown objective" : type.c_str();
}

static bool QuestTextMatches(
    const std::string& value,
    const std::string& lowerSearch)
{
    return lowerSearch.empty() ||
        toLower(value).find(lowerSearch) != std::string::npos;
}

void renderQuestsWindow()
{
    static ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    static int questFilter = 0; // 0 = All, 1 = Active
    static char questSearch[192] = "";

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + 30.0f, viewport->Pos.y + 30.0f),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(ImVec2(900.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(680.0f, 480.0f),
        ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f)
    );
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (!ImGui::Begin("Quest Manager", &appMenu::appQuests, flags))
    {
        ImGui::End();
        return;
    }

    const std::vector<QuestData> activeQuests =
        GetQuestDataActiveSnapshot();

    std::unordered_map<std::string, const QuestData*> activeById;
    activeById.reserve(activeQuests.size());

    for (const auto& quest : activeQuests)
    {
        if (!quest.questId.empty())
            activeById.emplace(quest.questId, &quest);
    }

    const std::string lowerSearch = toLower(TrimEFT(std::string(questSearch)));

    const auto staticQuestMatches = [&](const TarkovDevTasks& quest) -> bool
        {
            if (lowerSearch.empty() ||
                QuestTextMatches(quest.qName, lowerSearch) ||
                QuestTextMatches(quest.qID, lowerSearch))
            {
                return true;
            }

            for (const auto& objective : quest.objectives)
            {
                if (QuestTextMatches(objective.description, lowerSearch) ||
                    QuestTextMatches(objective.type, lowerSearch) ||
                    QuestTextMatches(objective.id, lowerSearch) ||
                    QuestTextMatches(objective.itemId, lowerSearch) ||
                    QuestTextMatches(objective.questItemId, lowerSearch))
                {
                    return true;
                }

                for (const auto& map : objective.maps)
                {
                    if (QuestTextMatches(map, lowerSearch))
                        return true;
                }

                for (const auto& zone : objective.zones)
                {
                    if (QuestTextMatches(zone.mapNameId, lowerSearch))
                        return true;
                }
            }

            return false;
        };

    const auto activeQuestMatches = [&](const QuestData& quest) -> bool
        {
            if (lowerSearch.empty() ||
                QuestTextMatches(quest.questName, lowerSearch) ||
                QuestTextMatches(quest.questId, lowerSearch))
            {
                return true;
            }

            for (const auto& objective : quest.objectives)
            {
                if (QuestTextMatches(objective.description, lowerSearch) ||
                    QuestTextMatches(objective.type, lowerSearch) ||
                    QuestTextMatches(objective.objectiveId, lowerSearch) ||
                    QuestTextMatches(objective.itemId, lowerSearch) ||
                    QuestTextMatches(objective.questItemId, lowerSearch))
                {
                    return true;
                }

                for (const auto& map : objective.maps)
                {
                    if (QuestTextMatches(map, lowerSearch))
                        return true;
                }

                for (const auto& zone : objective.zones)
                {
                    if (QuestTextMatches(zone.mapNameId, lowerSearch))
                        return true;
                }
            }

            for (const auto& completed : quest.completedConditions)
            {
                if (QuestTextMatches(completed, lowerSearch))
                    return true;
            }

            return false;
        };

    std::vector<const TarkovDevTasks*> visibleAllQuests;
    std::vector<const QuestData*> visibleActiveQuests;

    if (questFilter == 0)
    {
        visibleAllQuests.reserve(tarkovDevTasksData.size());

        for (const auto& quest : tarkovDevTasksData)
        {
            if (staticQuestMatches(quest))
                visibleAllQuests.emplace_back(&quest);
        }

        std::stable_sort(
            visibleAllQuests.begin(),
            visibleAllQuests.end(),
            [&](const TarkovDevTasks* left, const TarkovDevTasks* right)
            {
                const bool leftActive =
                    activeById.find(left->qID) != activeById.end();
                const bool rightActive =
                    activeById.find(right->qID) != activeById.end();

                if (leftActive != rightActive)
                    return leftActive;

                return left->qName < right->qName;
            });
    }
    else
    {
        visibleActiveQuests.reserve(activeQuests.size());

        for (const auto& quest : activeQuests)
        {
            if (activeQuestMatches(quest))
                visibleActiveQuests.emplace_back(&quest);
        }

        std::sort(
            visibleActiveQuests.begin(),
            visibleActiveQuests.end(),
            [](const QuestData* left, const QuestData* right)
            {
                return left->questName < right->questName;
            });
    }

    ImGui::TextUnformatted("Quest database");
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%zu total  |  %zu active",
        tarkovDevTasksData.size(),
        activeQuests.size());

    ImGui::Spacing();

    ImGui::TextDisabled("Show");
    ImGui::SameLine();
    ImGui::RadioButton("All", &questFilter, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Active", &questFilter, 1);
    ImGui::SameLine(0.0f, 24.0f);

    const bool hasSearch = questSearch[0] != '\0';
    const float clearButtonWidth = hasSearch ? 54.0f : 0.0f;
    const float searchSpacing = hasSearch
        ? ImGui::GetStyle().ItemSpacing.x
        : 0.0f;
    ImGui::SetNextItemWidth(std::max(
        180.0f,
        ImGui::GetContentRegionAvail().x -
            clearButtonWidth -
            searchSpacing));
    ImGui::InputTextWithHint(
        "##QuestSearch",
        "Search quests, objectives, items, maps or IDs...",
        questSearch,
        IM_ARRAYSIZE(questSearch));

    if (hasSearch)
    {
        ImGui::SameLine();

        if (ImGui::Button("Clear", ImVec2(clearButtonWidth, 0.0f)))
            questSearch[0] = '\0';
    }

    const std::size_t sourceCount = questFilter == 0
        ? tarkovDevTasksData.size()
        : activeQuests.size();
    const std::size_t visibleCount = questFilter == 0
        ? visibleAllQuests.size()
        : visibleActiveQuests.size();

    ImGui::TextDisabled(
        "Showing %zu of %zu %s",
        visibleCount,
        sourceCount,
        questFilter == 0 ? "quests" : "active quests");
    ImGui::Separator();

    if (visibleCount == 0)
    {
        ImGui::Spacing();

        if (hasSearch)
        {
            ImGui::TextDisabled(
                "No quests match \"%s\".",
                questSearch);
        }
        else if (questFilter == 1)
        {
            ImGui::TextDisabled(
                "No active quests are currently available.");
        }
        else
        {
            ImGui::TextDisabled(
                "The quest database is currently empty.");
        }

        ImGui::End();
        return;
    }

    const auto renderMaps = [](const std::vector<std::string>& maps)
        {
            if (maps.empty())
                return;

            ImGui::TextDisabled("Maps");
            ImGui::Indent();

            for (const auto& map : maps)
                ImGui::BulletText("%s", map.c_str());

            ImGui::Unindent();
        };

    const auto renderStaticObjectives = [&](const TarkovDevTasks& quest)
        {
            for (std::size_t index = 0;
                index < quest.objectives.size();
                ++index)
            {
                const auto& objective = quest.objectives[index];
                ImGui::PushID(static_cast<int>(index));

                if (!objective.description.empty())
                {
                    ImGui::TextWrapped(
                        "%zu. %s",
                        index + 1,
                        objective.description.c_str());
                    ImGui::TextDisabled(
                        "%s  |  %s",
                        QuestObjectiveTypeLabel(objective.type),
                        objective.type.c_str());
                }
                else
                {
                    ImGui::Text(
                        "%zu. %s",
                        index + 1,
                        QuestObjectiveTypeLabel(objective.type));
                }

                ImGui::Indent();

                if (!objective.id.empty())
                    ImGui::TextDisabled(
                        "Objective ID: %s",
                        objective.id.c_str());

                if (!objective.itemId.empty())
                    ImGui::TextDisabled(
                        "Item ID: %s",
                        objective.itemId.c_str());

                if (!objective.questItemId.empty())
                    ImGui::TextDisabled(
                        "Quest item ID: %s",
                        objective.questItemId.c_str());

                renderMaps(objective.maps);

                if (!objective.zones.empty())
                {
                    ImGui::TextDisabled(
                        "Locations (%zu)",
                        objective.zones.size());
                    ImGui::Indent();

                    for (const auto& zone : objective.zones)
                    {
                        const char* mapName = zone.mapNameId.empty()
                            ? "Unknown map"
                            : zone.mapNameId.c_str();
                        ImGui::BulletText(
                            "%s  (%.1f, %.1f, %.1f)",
                            mapName,
                            zone.position.x,
                            zone.position.y,
                            zone.position.z);
                    }

                    ImGui::Unindent();
                }

                ImGui::Unindent();

                if (index + 1 < quest.objectives.size())
                    ImGui::Separator();

                ImGui::PopID();
            }
        };

    const auto renderActiveObjectives =
        [&](const QuestData& quest)
        {
            for (std::size_t index = 0;
                index < quest.objectives.size();
                ++index)
            {
                const auto& objective = quest.objectives[index];
                ImGui::PushID(static_cast<int>(index));

                if (!objective.description.empty())
                {
                    ImGui::TextWrapped(
                        "%zu. %s",
                        index + 1,
                        objective.description.c_str());
                    ImGui::TextDisabled(
                        "%s  |  %s",
                        QuestObjectiveTypeLabel(objective.type),
                        objective.type.c_str());
                }
                else
                {
                    ImGui::Text(
                        "%zu. %s",
                        index + 1,
                        QuestObjectiveTypeLabel(objective.type));
                }

                ImGui::Indent();

                if (!objective.objectiveId.empty())
                    ImGui::TextDisabled(
                        "Objective ID: %s",
                        objective.objectiveId.c_str());

                if (!objective.itemId.empty())
                    ImGui::TextDisabled(
                        "Item ID: %s",
                        objective.itemId.c_str());

                if (!objective.questItemId.empty())
                    ImGui::TextDisabled(
                        "Quest item ID: %s",
                        objective.questItemId.c_str());

                renderMaps(objective.maps);

                if (!objective.zones.empty())
                {
                    ImGui::TextDisabled(
                        "Locations (%zu)",
                        objective.zones.size());
                    ImGui::Indent();

                    for (const auto& zone : objective.zones)
                    {
                        const char* mapName = zone.mapNameId.empty()
                            ? "Unknown map"
                            : zone.mapNameId.c_str();
                        ImGui::BulletText(
                            "%s  (%.1f, %.1f, %.1f)",
                            mapName,
                            zone.position.x,
                            zone.position.y,
                            zone.position.z);
                    }

                    ImGui::Unindent();
                }

                ImGui::Unindent();

                if (index + 1 < quest.objectives.size())
                    ImGui::Separator();

                ImGui::PopID();
            }
        };

    const ImGuiTableFlags tableFlags =
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable(
        "##QuestDatabase",
        3,
        tableFlags,
        ImGui::GetContentRegionAvail()))
    {
        ImGui::TableSetupColumn(
            "Quest",
            ImGuiTableColumnFlags_WidthStretch,
            0.34f);
        ImGui::TableSetupColumn(
            "State",
            ImGuiTableColumnFlags_WidthFixed,
            88.0f);
        ImGui::TableSetupColumn(
            "Objectives",
            ImGuiTableColumnFlags_WidthStretch,
            0.66f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int rowId = 0;

        if (questFilter == 0)
        {
            for (const TarkovDevTasks* quest : visibleAllQuests)
            {
                const auto activeIt = activeById.find(quest->qID);
                const bool isActive = activeIt != activeById.end();
                const QuestData* activeQuest = isActive
                    ? activeIt->second
                    : nullptr;

                std::size_t locationCount = 0;
                std::size_t itemCount = 0;

                for (const auto& objective : quest->objectives)
                {
                    locationCount += objective.zones.size();

                    if (objective.zones.empty())
                        locationCount += objective.maps.size();

                    if (!objective.itemId.empty() ||
                        !objective.questItemId.empty())
                    {
                        ++itemCount;
                    }
                }

                ImGui::PushID(rowId++);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(
                    quest->qName.empty()
                        ? quest->qID.c_str()
                        : quest->qName.c_str());
                ImGui::PopTextWrapPos();

                if (!quest->qID.empty())
                    ImGui::TextDisabled("%s", quest->qID.c_str());

                ImGui::TableSetColumnIndex(1);

                if (isActive)
                {
                    ImGui::TextColored(
                        ImVec4(0.32f, 0.85f, 0.48f, 1.0f),
                        "ACTIVE");

                    if (activeQuest)
                    {
                        ImGui::TextDisabled(
                            "%zu remaining",
                            activeQuest->objectives.size());
                    }
                }
                else
                {
                    ImGui::TextDisabled("Not active");
                }

                ImGui::TableSetColumnIndex(2);

                const std::string summary =
                    std::to_string(quest->objectives.size()) +
                    " objectives  |  " +
                    std::to_string(locationCount) +
                    " locations  |  " +
                    std::to_string(itemCount) +
                    " items";

                if (ImGui::TreeNodeEx(
                    "##QuestDetails",
                    ImGuiTreeNodeFlags_SpanAvailWidth,
                    "%s",
                    summary.c_str()))
                {
                    ImGui::Spacing();
                    renderStaticObjectives(*quest);
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }
        else
        {
            for (const QuestData* quest : visibleActiveQuests)
            {
                std::size_t locationCount = 0;
                std::size_t itemCount = 0;

                for (const auto& objective : quest->objectives)
                {
                    locationCount += objective.zones.size();

                    if (objective.zones.empty())
                        locationCount += objective.maps.size();

                    if (!objective.itemId.empty() ||
                        !objective.questItemId.empty())
                    {
                        ++itemCount;
                    }
                }

                ImGui::PushID(rowId++);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(
                    quest->questName.empty()
                        ? quest->questId.c_str()
                        : quest->questName.c_str());
                ImGui::PopTextWrapPos();

                if (!quest->questId.empty())
                    ImGui::TextDisabled("%s", quest->questId.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(
                    ImVec4(0.32f, 0.85f, 0.48f, 1.0f),
                    "%s",
                    QuestStatusLabel(quest->status));
                ImGui::TextDisabled(
                    "%zu complete",
                    quest->completedConditions.size());

                ImGui::TableSetColumnIndex(2);

                const std::string summary =
                    std::to_string(quest->objectives.size()) +
                    " remaining  |  " +
                    std::to_string(locationCount) +
                    " locations  |  " +
                    std::to_string(itemCount) +
                    " items";

                if (ImGui::TreeNodeEx(
                    "##ActiveQuestDetails",
                    ImGuiTreeNodeFlags_SpanAvailWidth,
                    "%s",
                    summary.c_str()))
                {
                    ImGui::Spacing();
                    renderActiveObjectives(*quest);
                    ImGui::TreePop();
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    ImGui::End();
}


}
