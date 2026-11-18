#include "QuestWidget.h"

#include "../includes.h"
#include "../config.h"
#include "../globals.h"
#include "../menuLayout.h"
#include "../../Tarkov/GameWorld/QuestManager.h"
#include "../../Web/TarkovDev/TarkovDevClient.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

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

void renderQuestsWindow() {
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | menuLayout::WidgetMovementFlags(globals::questsWindow);
    static int questFilter = 0; // 0 = All, 1 = Active, 2 = Selected
    static char questSearch[192] = "";

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    menuLayout::PrepareRightToolbarWindow(globals::questsWindow, "Quest Manager", ImVec2(900.0f, 680.0f), globals::appWindowAlpha);
    ImGui::SetNextWindowSize(ImVec2(900.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(680.0f, 480.0f), ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f));

    if (!ImGui::Begin("Quest Manager", &appMenu::appQuests, flags)) {
        ImGui::End();
        return;
    }

    if (menuLayout::RenderWidgetTitleBarControls(globals::questsWindow))
        configManager.SaveConfig();

    const std::vector<QuestData> activeQuests = GetQuestDataActiveSnapshot();
    const std::vector<std::string> selectedQuestIds = GetSelectedQuestIdsSnapshot();
    const std::unordered_set<std::string> selectedById(selectedQuestIds.begin(), selectedQuestIds.end());

    std::unordered_map<std::string, const QuestData*> activeById;
    activeById.reserve(activeQuests.size());

    for (const auto& quest : activeQuests) {
        if (!quest.questId.empty())
            activeById.emplace(quest.questId, &quest);
    }

    std::unordered_map<std::string, const TarkovDevTasks*> staticById;
    staticById.reserve(tarkovDevTasksData.size());

    for (const auto& quest : tarkovDevTasksData) {
        if (!quest.qID.empty())
            staticById.emplace(quest.qID, &quest);
    }

    const auto findStaticObjective = [&](const std::string& questId, const std::string& objectiveId) -> const TarkovObjective* {
        const auto questIt = staticById.find(questId);
        if (questIt == staticById.end() || !questIt->second)
            return nullptr;

        for (const auto& objective : questIt->second->objectives) {
            if (objective.id == objectiveId)
                return &objective;
        }

        return nullptr;
        };

    const auto itemDisplayName = [](const std::string& itemId) {
        const std::string name = BSGidToName(itemId);
        return name == "NoData" || name == "ERR" ? std::string{} : name;
        };

    const auto countObjectiveItems = [](const TarkovObjective& objective) {
        std::size_t count = objective.itemIds.empty() ? (objective.itemId.empty() ? 0u : 1u) : objective.itemIds.size();

        if (!objective.questItemId.empty())
            ++count;

        return count;
        };

    const std::string lowerSearch = toLower(TrimEFT(std::string(questSearch)));

    const auto staticQuestMatches = [&](const TarkovDevTasks& quest) -> bool {
        if (lowerSearch.empty() || QuestTextMatches(quest.qName, lowerSearch) || QuestTextMatches(quest.qID, lowerSearch))
            return true;

        for (const auto& objective : quest.objectives) {
            if (QuestTextMatches(objective.description, lowerSearch) ||
                QuestTextMatches(objective.type, lowerSearch) ||
                QuestTextMatches(objective.id, lowerSearch) ||
                QuestTextMatches(objective.itemId, lowerSearch) ||
                QuestTextMatches(objective.questItemId, lowerSearch)) {
                return true;
            }

            for (const auto& itemId : objective.itemIds) {
                if (QuestTextMatches(itemId, lowerSearch))
                    return true;
            }

            for (const auto& map : objective.maps) {
                if (QuestTextMatches(map, lowerSearch))
                    return true;
            }

            for (const auto& zone : objective.zones) {
                if (QuestTextMatches(zone.mapNameId, lowerSearch))
                    return true;
            }
        }

        return false;
        };

    const auto activeQuestMatches = [&](const QuestData& quest) -> bool {
        if (lowerSearch.empty() || QuestTextMatches(quest.questName, lowerSearch) || QuestTextMatches(quest.questId, lowerSearch))
            return true;

        for (const auto& objective : quest.objectives) {
            if (QuestTextMatches(objective.description, lowerSearch) ||
                QuestTextMatches(objective.type, lowerSearch) ||
                QuestTextMatches(objective.objectiveId, lowerSearch) ||
                QuestTextMatches(objective.itemId, lowerSearch) ||
                QuestTextMatches(objective.questItemId, lowerSearch)) {
                return true;
            }

            const TarkovObjective* staticObjective = findStaticObjective(quest.questId, objective.objectiveId);

            if (staticObjective) {
                for (const auto& itemId : staticObjective->itemIds) {
                    if (QuestTextMatches(itemId, lowerSearch))
                        return true;
                }
            }

            for (const auto& map : objective.maps) {
                if (QuestTextMatches(map, lowerSearch))
                    return true;
            }

            for (const auto& zone : objective.zones) {
                if (QuestTextMatches(zone.mapNameId, lowerSearch))
                    return true;
            }
        }

        for (const auto& completed : quest.completedConditions) {
            if (QuestTextMatches(completed, lowerSearch))
                return true;
        }

        return false;
        };

    std::vector<const TarkovDevTasks*> visibleAllQuests;
    std::vector<const QuestData*> visibleActiveQuests;
    std::vector<const TarkovDevTasks*> visibleSelectedQuests;

    if (questFilter == 0) {
        visibleAllQuests.reserve(tarkovDevTasksData.size());

        for (const auto& quest : tarkovDevTasksData) {
            if (staticQuestMatches(quest))
                visibleAllQuests.emplace_back(&quest);
        }

        std::stable_sort(visibleAllQuests.begin(), visibleAllQuests.end(), [&](const TarkovDevTasks* left, const TarkovDevTasks* right) {
            const bool leftActive = activeById.find(left->qID) != activeById.end();
            const bool rightActive = activeById.find(right->qID) != activeById.end();

            if (leftActive != rightActive)
                return leftActive;

            const bool leftSelected = selectedById.contains(left->qID);
            const bool rightSelected = selectedById.contains(right->qID);
            if (leftSelected != rightSelected)
                return leftSelected;

            return left->qName < right->qName;
            });
    }
    else if (questFilter == 1) {
        visibleActiveQuests.reserve(activeQuests.size());

        for (const auto& quest : activeQuests) {
            if (activeQuestMatches(quest))
                visibleActiveQuests.emplace_back(&quest);
        }

        std::sort(visibleActiveQuests.begin(), visibleActiveQuests.end(), [](const QuestData* left, const QuestData* right) {
            return left->questName < right->questName;
            });
    }
    else {
        visibleSelectedQuests.reserve(selectedQuestIds.size());

        for (const auto& quest : tarkovDevTasksData) {
            if (selectedById.contains(quest.qID) && !activeById.contains(quest.qID) && staticQuestMatches(quest))
                visibleSelectedQuests.emplace_back(&quest);
        }

        std::sort(visibleSelectedQuests.begin(), visibleSelectedQuests.end(), [](const TarkovDevTasks* left, const TarkovDevTasks* right) {
            return left->qName < right->qName;
            });
    }

    ImGui::TextUnformatted("Quest database");
    ImGui::SameLine();
    const std::size_t inactiveSelectedCount = std::count_if(selectedQuestIds.begin(), selectedQuestIds.end(), [&](const std::string& questId) {
        return !activeById.contains(questId);
        });
    ImGui::TextDisabled("%zu total  |  %zu active  |  %zu selected", tarkovDevTasksData.size(), activeQuests.size(), inactiveSelectedCount);

    ImGui::Spacing();

    ImGui::TextDisabled("Show");
    ImGui::SameLine();
    ImGui::RadioButton("All", &questFilter, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Active", &questFilter, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Selected", &questFilter, 2);
    ImGui::SameLine(0.0f, 24.0f);

    const bool hasSearch = questSearch[0] != '\0';
    const float clearButtonWidth = hasSearch ? 54.0f : 0.0f;
    const float searchSpacing = hasSearch ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
    ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - clearButtonWidth - searchSpacing));
    ImGui::InputTextWithHint("##QuestSearch", "Search quests, objectives, items or maps...", questSearch, IM_ARRAYSIZE(questSearch));

    if (hasSearch) {
        ImGui::SameLine();

        if (ImGui::Button("Clear", ImVec2(clearButtonWidth, 0.0f)))
            questSearch[0] = '\0';
    }

    if (questFilter == 1) {
        bool memoryQuestsEnabled = questManager.areMemoryQuestsEnabled();

        if (ImGui::Checkbox("Enable memory-read quests", &memoryQuestsEnabled)) {
            questManager.setMemoryQuestsEnabled(memoryQuestsEnabled);
            configManager.SaveConfig();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Turn off to use only quests enabled manually from All.");
    }

    const std::size_t sourceCount = questFilter == 0 ? tarkovDevTasksData.size() : questFilter == 1 ? activeQuests.size() : inactiveSelectedCount;
    const std::size_t visibleCount = questFilter == 0 ? visibleAllQuests.size() : questFilter == 1 ? visibleActiveQuests.size() : visibleSelectedQuests.size();

    ImGui::TextDisabled("Showing %zu of %zu %s", visibleCount, sourceCount, questFilter == 0 ? "quests" : questFilter == 1 ? "active quests" : "selected quests");
    ImGui::Separator();

    if (visibleCount == 0) {
        ImGui::Spacing();

        if (hasSearch)
            ImGui::TextDisabled("No quests match \"%s\".", questSearch);
        else if (questFilter == 1)
            ImGui::TextDisabled("No active quests are currently available.");
        else if (questFilter == 2)
            ImGui::TextDisabled("No inactive quests are selected. Add quests from All.");
        else
            ImGui::TextDisabled("The quest database is currently empty.");

        ImGui::End();
        return;
    }

    const auto mapDisplayName = [](const std::string& mapId) -> const char* {
        if (mapId == "bigmap") return "Customs";
        if (mapId == "factory4_day" || mapId == "factory4_night" || mapId == "factory_event") return "Factory";
        if (mapId == "laboratory" || mapId == "laboratory_dark" || mapId == "laboratory_event") return "The Lab";
        if (mapId == "RezervBase") return "Reserve";
        if (mapId == "TarkovStreets" || mapId == "tarkovstreets") return "Streets of Tarkov";
        if (mapId == "Sandbox" || mapId == "Sandbox_high") return "Ground Zero";
        return mapId.empty() ? "Unknown map" : mapId.c_str();
        };

    const auto renderMaps = [&](const std::vector<std::string>& maps) {
        if (maps.empty())
            return;

        ImGui::TextDisabled(maps.size() == 1 ? "Map" : "Maps");
        ImGui::Indent();

        for (const auto& map : maps)
            ImGui::BulletText("%s", mapDisplayName(map));

        ImGui::Unindent();
        };

    const auto renderItem = [&](const std::string& itemId) {
        if (itemId.empty())
            return;

        const std::string name = itemDisplayName(itemId);

        ImGui::BulletText("%s", name.empty() ? "Unknown item" : name.c_str());
        };

    const auto renderAcceptedItems = [&](const TarkovObjective& objective) {
        if (!objective.itemIds.empty()) {
            ImGui::TextDisabled(objective.itemIds.size() == 1 ? "Item" : "Accepted items (%zu)", objective.itemIds.size());
            ImGui::Indent();

            for (const auto& itemId : objective.itemIds)
                renderItem(itemId);

            ImGui::Unindent();
        }
        else if (!objective.itemId.empty()) {
            ImGui::TextDisabled("Item");
            ImGui::Indent();
            renderItem(objective.itemId);
            ImGui::Unindent();
        }

        if (!objective.questItemId.empty()) {
            ImGui::TextDisabled("Quest item");
            ImGui::Indent();
            renderItem(objective.questItemId);
            ImGui::Unindent();
        }
        };

    const auto renderObjectiveMeta = [](const TarkovObjective& objective) {
        std::string details;
        if (objective.count > 1)
            details = "Required: " + std::to_string(objective.count);
        if (objective.foundInRaid)
            details += (details.empty() ? "" : "  |  ") + std::string("Found in raid");
        if (objective.optional)
            details += (details.empty() ? "" : "  |  ") + std::string("Optional");
        if (!details.empty())
            ImGui::TextDisabled("%s", details.c_str());
        };

    const auto renderStaticObjectives = [&](const TarkovDevTasks& quest) {
        for (std::size_t index = 0; index < quest.objectives.size(); ++index) {
            const auto& objective = quest.objectives[index];
            ImGui::PushID(static_cast<int>(index));

            if (!objective.description.empty()) {
                ImGui::TextWrapped("%zu. %s", index + 1, objective.description.c_str());
                ImGui::TextColored(ImVec4(0.45f, 0.72f, 0.95f, 1.0f), "%s", QuestObjectiveTypeLabel(objective.type));
            }
            else {
                ImGui::Text("%zu. %s", index + 1, QuestObjectiveTypeLabel(objective.type));
            }

            ImGui::Indent();

            renderObjectiveMeta(objective);
            renderAcceptedItems(objective);
            renderMaps(objective.maps);

            if (!objective.zones.empty()) {
                ImGui::TextDisabled("Locations (%zu)", objective.zones.size());
                ImGui::Indent();

                for (const auto& zone : objective.zones) {
                    ImGui::BulletText("%s", mapDisplayName(zone.mapNameId));
                }

                ImGui::Unindent();
            }

            ImGui::Unindent();

            if (index + 1 < quest.objectives.size())
                ImGui::Separator();

            ImGui::PopID();
        }
        };

    const auto renderActiveObjectives = [&](const QuestData& quest) {
        for (std::size_t index = 0; index < quest.objectives.size(); ++index) {
            const auto& objective = quest.objectives[index];
            const TarkovObjective* staticObjective = findStaticObjective(quest.questId, objective.objectiveId);
            ImGui::PushID(static_cast<int>(index));

            if (!objective.description.empty()) {
                ImGui::TextWrapped("%zu. %s", index + 1, objective.description.c_str());
                ImGui::TextColored(ImVec4(0.45f, 0.72f, 0.95f, 1.0f), "%s", QuestObjectiveTypeLabel(objective.type));
            }
            else {
                ImGui::Text("%zu. %s", index + 1, QuestObjectiveTypeLabel(objective.type));
            }

            ImGui::Indent();

            if (staticObjective) {
                renderObjectiveMeta(*staticObjective);
                renderAcceptedItems(*staticObjective);
            }
            else {
                if (!objective.itemId.empty()) {
                    ImGui::TextDisabled("Item");
                    ImGui::Indent();
                    renderItem(objective.itemId);
                    ImGui::Unindent();
                }

                if (!objective.questItemId.empty()) {
                    ImGui::TextDisabled("Quest item");
                    ImGui::Indent();
                    renderItem(objective.questItemId);
                    ImGui::Unindent();
                }
            }

            renderMaps(objective.maps);

            if (!objective.zones.empty()) {
                ImGui::TextDisabled("Locations (%zu)", objective.zones.size());
                ImGui::Indent();

                for (const auto& zone : objective.zones) {
                    ImGui::BulletText("%s", mapDisplayName(zone.mapNameId));
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

    if (ImGui::BeginTable("##QuestDatabase", 3, tableFlags, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Quest", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Objectives", ImGuiTableColumnFlags_WidthStretch, 0.70f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int rowId = 0;

        const auto renderStaticQuestRow = [&](const TarkovDevTasks& quest) {
            const bool isActive = activeById.contains(quest.qID);
            const bool isSelected = selectedById.contains(quest.qID);
            std::size_t locationCount = 0;
            std::size_t itemCount = 0;

            for (const auto& objective : quest.objectives) {
                locationCount += objective.zones.size();
                if (objective.zones.empty())
                    locationCount += objective.maps.size();
                itemCount += countObjectiveItems(objective);
            }

            ImGui::PushID(rowId++);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(quest.qName.empty() ? "Unnamed quest" : quest.qName.c_str());
            ImGui::PopTextWrapPos();

            ImGui::TableSetColumnIndex(1);
            const std::string summary = std::to_string(quest.objectives.size()) + " objectives  |  " +
                std::to_string(locationCount) + " locations  |  " + std::to_string(itemCount) + " items";

            if (ImGui::TreeNodeEx("##QuestDetails", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", summary.c_str())) {
                ImGui::Spacing();
                renderStaticObjectives(quest);
                ImGui::TreePop();
            }

            ImGui::TableSetColumnIndex(2);
            if (isActive)
                ImGui::TextColored(ImVec4(0.32f, 0.85f, 0.48f, 1.0f), "Active");

            if (isSelected) {
                if (ImGui::SmallButton("Remove"))
                    questManager.setQuestSelected(quest.qID, false);
            }
            else if (ImGui::SmallButton("Enable")) {
                questManager.setQuestSelected(quest.qID, true);
            }

            ImGui::PopID();
            };

        if (questFilter == 0) {
            for (const TarkovDevTasks* quest : visibleAllQuests)
                renderStaticQuestRow(*quest);
        }
        else if (questFilter == 1) {
            for (const QuestData* quest : visibleActiveQuests) {
                std::size_t locationCount = 0;
                std::size_t itemCount = 0;

                for (const auto& objective : quest->objectives) {
                    locationCount += objective.zones.size();

                    if (objective.zones.empty())
                        locationCount += objective.maps.size();

                    const TarkovObjective* staticObjective = findStaticObjective(quest->questId, objective.objectiveId);

                    if (staticObjective)
                        itemCount += countObjectiveItems(*staticObjective);
                    else {
                        if (!objective.itemId.empty())
                            ++itemCount;

                        if (!objective.questItemId.empty())
                            ++itemCount;
                    }
                }

                ImGui::PushID(rowId++);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(quest->questName.empty() ? "Unnamed quest" : quest->questName.c_str());
                ImGui::PopTextWrapPos();

                ImGui::TableSetColumnIndex(1);
                const std::string summary = std::to_string(quest->objectives.size()) + " remaining  |  " +
                    std::to_string(locationCount) + " locations  |  " + std::to_string(itemCount) + " items";

                if (ImGui::TreeNodeEx("##ActiveQuestDetails", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", summary.c_str())) {
                    ImGui::Spacing();
                    renderActiveObjectives(*quest);
                    ImGui::TreePop();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(ImVec4(0.32f, 0.85f, 0.48f, 1.0f), "Active");

                ImGui::PopID();
            }
        }
        else {
            for (const TarkovDevTasks* quest : visibleSelectedQuests)
                renderStaticQuestRow(*quest);
        }

        ImGui::EndTable();
    }

    ImGui::End();
}



}
