#pragma once
#include "../Tarkov/GameWorld/QuestManager.h"
#include "text.h"
#include "menuLayout.h"
#include "../Core/Utilities.h"
#include "../Tarkov/GameWorld/RegisteredPlayers.h"
#include "../Tarkov/GameWorld/Explosives/ExplosiveManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace MapNames
{
    inline const std::unordered_map<std::string_view, std::string_view> idToNameId =
    {
        { "55f2d3fd4bdc2d5f408b4567", "factory4_day" },
        { "56f40101d2720b2a4d8b45d6", "bigmap" },
        { "5704e3c2d2720bac5b8b4567", "Woods" },
        { "5704e4dad2720bb55b8b4567", "Lighthouse" },
        { "5704e554d2720bac5b8b456e", "Shoreline" },
        { "5704e5fad2720bc05b8b4567", "RezervBase" },
        { "5714dbc024597771384a510d", "Interchange" },
        { "5714dc692459777137212e12", "TarkovStreets" },
        { "59fc81d786f774390775787e", "factory4_night" },
        { "5b0fc42d86f7744a585f9105", "laboratory" },
        { "653e6760052c01c1c805532f", "Sandbox" },
        { "65b8d6f5cdde2479cb2a3125", "Sandbox_high" },
        { "65cc8f81a9aac3e77d0cfd3e", "Terminal" },
        { "6733700029c367a3d40b02af", "Labyrinth" },
        { "68236e8153654e8c1200798a", "Sandbox_start" },
        { "69af492a4819ea4ba10a69c5", "Icebreaker" },
        { "6a294a5b5eb5f9a1700417b7", "laboratory_dark" }
    };

    inline std::string_view GetNameFromId(std::string_view id)
    {
        const auto it = idToNameId.find(id);

        if (it == idToNameId.end())
            return "Unknown";

        return it->second;
    }
}

void DrawRadarMainText(int x, int y, ImVec4 color, const char* str)
{
    const float fontSize = 36.0f;

    ImFont* font = ImGui::GetFont();
    ImVec2 text_size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 pos(
        x - text_size.x * 0.5f,
        y - text_size.y * 0.5f
    );

    draw_list->AddText(
        font,
        fontSize,
        pos,
        ImColor(color),
        str
    );
}

void DrawRadarSubText(int x, int y, ImVec4 color, const char* str)
{
    const float fontSize = 20.0f;

    ImFont* font = ImGui::GetFont();
    ImVec2 text_size = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, str);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    ImVec2 pos(
        x - text_size.x * 0.5f,
        y - text_size.y * 0.5f
    );

    draw_list->AddText(
        font,
        fontSize,
        pos,
        ImColor(color),
        str
    );
}

void drawPlayers()
{
    const PlayerSnapshot cacheSnapshot = registeredPlayers.getCacheSnapshot();
    const PlayerCollection& cache = *cacheSnapshot;

    BeginRadarPlayerPanelFrame();

    constexpr float kBtrPassengerRadius = 4.0f;
    constexpr float kBtrPassengerRadiusSquared =
        kBtrPassengerRadius * kBtrPassengerRadius;
    constexpr size_t kBtrPassengerCapacity = 4;

    struct BtrPassengerStack
    {
        const Player* btr{};
        std::array<const Player*, kBtrPassengerCapacity> passengers{};
        size_t count{};
    };

    std::vector<BtrPassengerStack> btrStacks;
    btrStacks.reserve(1);

    for (const Player& player : cache)
    {
        if (player.isBTR &&
            !player.isDead &&
            !player.hasExfiled &&
            Utils::valid_pointer(player.instance))
        {
            btrStacks.push_back({ &player });
        }
    }

    for (const Player& player : cache)
    {
        if (!player.isInBTR ||
            player.isBTR ||
            player.isDead ||
            player.hasExfiled ||
            !Utils::valid_pointer(player.instance))
        {
            continue;
        }

        BtrPassengerStack* nearestBtr = nullptr;
        float nearestDistanceSquared = kBtrPassengerRadiusSquared;

        for (BtrPassengerStack& stack : btrStacks)
        {
            const glm::vec3 delta = player.location - stack.btr->location;
            const float distanceSquared =
                (delta.x * delta.x) +
                (delta.y * delta.y) +
                (delta.z * delta.z);

            if (distanceSquared <= nearestDistanceSquared)
            {
                nearestDistanceSquared = distanceSquared;
                nearestBtr = &stack;
            }
        }

        if (nearestBtr && nearestBtr->count < kBtrPassengerCapacity)
            nearestBtr->passengers[nearestBtr->count++] = &player;
    }

    for (BtrPassengerStack& stack : btrStacks)
    {
        std::sort(
            stack.passengers.begin(),
            stack.passengers.begin() + stack.count,
            [](const Player* first, const Player* second)
            {
                return first->instance < second->instance;
            }
        );
    }

    struct BtrRadarHeadingState
    {
        glm::vec3 previousWorldPosition{};
        glm::vec2 rotation{};
        bool initialized = false;
    };

    static std::unordered_map<uint64_t, BtrRadarHeadingState> btrHeadingStates;
    const auto getTrackedBtrRotation = [&](const Player& btr, const glm::vec3& mapPosition)
        {
            BtrRadarHeadingState& state = btrHeadingStates[btr.instance];
            if (!state.initialized)
            {
                state.previousWorldPosition = btr.location;
                state.initialized = true;
                return state.rotation;
            }

            const glm::vec3 worldMovement = btr.location - state.previousWorldPosition;
            const float worldMovementSquared = (worldMovement.x * worldMovement.x) + (worldMovement.z * worldMovement.z);

            if (worldMovementSquared > 0.0025f)
            {
                const glm::vec3 previousMapPosition = mapControl.getMapPosition(state.previousWorldPosition, currentMap::configX, currentMap::configY, currentMap::configScale);
                const float mapMovementX = mapPosition.x - previousMapPosition.x;
                const float mapMovementY = mapPosition.y - previousMapPosition.y;

                if ((mapMovementX * mapMovementX) + (mapMovementY * mapMovementY) > 0.01f)
                {
                    state.rotation.x = static_cast<float>(std::atan2(mapMovementY, mapMovementX) * (180.0 / PI));
                }

                state.previousWorldPosition = btr.location;
            }

            return state.rotation;
        };

    for (const auto& player : cache)
    {
        if (!Utils::valid_pointer(player.instance))
            continue;

        if (player.isLocal)
            continue;
        if (player.hasExfiled)
            continue;
        if (!player.isDead) {


            if (player.location.x == 0.f && player.location.y == 0.f && player.location.z == 0.f)
                continue;

            glm::vec3 position = mapControl.getMapPosition(player.location, currentMap::configX, currentMap::configY, currentMap::configScale);

            if (player.isInBTR)
            {
                const bool representedByBtr = std::any_of(
                    btrStacks.begin(),
                    btrStacks.end(),
                    [&player](const BtrPassengerStack& stack)
                    {
                        return std::find(
                            stack.passengers.begin(),
                            stack.passengers.begin() + stack.count,
                            &player) != stack.passengers.begin() + stack.count;
                    }
                );

                if (representedByBtr)
                    continue;
            }

            if (player.isBTR)
            {
                std::vector<glm::vec4> passengerColours;
                const auto stack = std::find_if(
                    btrStacks.begin(),
                    btrStacks.end(),
                    [&player](const BtrPassengerStack& candidate)
                    {
                        return candidate.btr == &player;
                    });

                if (stack != btrStacks.end())
                {
                    passengerColours.reserve(stack->count);
                    for (size_t index = 0; index < stack->count; ++index)
                        passengerColours.push_back(GetRadarPlayerMarkerColour(*stack->passengers[index]));
                }

                DrawRadarBtrMarker(position.x, position.y, getTrackedBtrRotation(player, position), GetRadarPlayerMarkerColour(player), passengerColours, mapControl.zoomLevel);
                continue;
            }

            if (!player.isInBTR)
            {
                int aimLineLen = 100;
                if (player.isFriend)
                    aimLineLen = radarGlobals::friendAimLine;
                else if (player.groupId != mainGame.localGroupId)
                    aimLineLen = radarGlobals::enemyAimLine;
                else
                    aimLineLen = radarGlobals::friendAimLine;

                if (mainGame.localGroupId == "" && !player.isFriend)
                    aimLineLen = radarGlobals::enemyAimLine;

                if (radarGlobals::drawAimLineTargets &&
                    player.aimLineTargetConfirmed)
                {
                    const glm::vec3 targetPosition = mapControl.getMapPosition(player.aimLineTargetLocation, currentMap::configX, currentMap::configY, currentMap::configScale);
					const glm::vec2 lineStart = GetRadarFacingPoint(glm::vec2(position.x, position.y), player.rotation, kRadarPlayerTriangleRadius);

					DrawLine(lineStart.x, lineStart.y, targetPosition.x, targetPosition.y, GetRadarPlayerMarkerColour(player), 3);
                }
                else
                {
					drawAimLine(glm::vec2(position.x, position.y), player.rotation, aimLineLen, GetRadarPlayerMarkerColour(player), kRadarPlayerTriangleRadius);
                }

                if (!radarGlobals::minimalView)
                    drawGroupLine(position, player);
            }

            DrawRadarPlayerMarkers(position.x, position.y, mapControl.zoomLevel, player);

        }


    }

    DrawRadarPlayerLoadoutPanel(cache);

}

void drawLocalPlayer()
{
    if (!Utils::valid_pointer(mainGame.localPlayerHands) ||
        (mainGame.localLocation.x == 0.0f &&
            mainGame.localLocation.y == 0.0f &&
            mainGame.localLocation.z == 0.0f))
    {
        return;
    }

    const PlayerSnapshot cacheSnapshot = registeredPlayers.getCacheSnapshot();

    for (const Player& player : *cacheSnapshot)
    {
        if (player.isLocal && Utils::valid_pointer(player.P_CorpseClass))
            return;

        if (player.isLocal &&
            (!Utils::valid_pointer(player.P_HandsController) ||
                (player.location.x == 0.0f &&
                    player.location.y == 0.0f &&
                    player.location.z == 0.0f)))
        {
            return;
        }
    }

    //localPlayer position on map
    glm::vec3 position = mapControl.getMapPosition(mainGame.localLocation, currentMap::configX, currentMap::configY, currentMap::configScale);



    drawAimLine(glm::vec2(position.x, position.y), mainGame.localRotation, radarGlobals::localAimLine, coloursGlobals::playerLocal, kRadarPlayerTriangleRadius
    );

    DrawRadarDirectionalTriangle(position.x, position.y, mainGame.localRotation,
        ImColor(
            coloursGlobals::playerLocal.x,
            coloursGlobals::playerLocal.y,
            coloursGlobals::playerLocal.z,
            coloursGlobals::playerLocal.w));

}

void formatValue(int value, char* out)
{
    if (value >= 1000000)
        snprintf(out, 16, "%.1fm", value / 1000000.0f);
    else if (value >= 1000)
        snprintf(out, 16, "%dk", (value + 500) / 1000);
    else
        snprintf(out, 16, "%d", value);
}

float DistSq(const glm::vec3& a, const glm::vec3& b)
{
    glm::vec3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

void drawWidgetTopLoot()
{
    if ((radarGlobals::minimalView && appGlobals::runRadar.load()) || !appMenu::widgetTopLoot)
        return;

    const std::string windowNameMain = "Top Loot";
    static ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;

    std::vector<LootEntity> lootCache = Loot.getCacheLoot();
    
    struct AggregatedLoot
    {
        std::string shortName;
        std::string longName;
        int avgMarketPrice = 0;
        int traderPrice = 0;
        int qty = 0;
        int nearestDistance = INT_MAX;
        bool wanted = false;

        std::vector<LootEntity*> items;

        int bestPrice() const
        {
            return (avgMarketPrice > 0) ? avgMarketPrice : traderPrice;
        }
    };

    // Use a compact key built from fields we know exist.
    std::unordered_map<std::string, AggregatedLoot> groupedLoot;
    groupedLoot.reserve(lootCache.size());

    for (auto& loot : lootCache)
    {
        if (loot.pendingResolve || loot.failed)
            continue;

        if (!loot.hasValidPosition)
            continue;

        if (!loot.isItem())
            continue;

        const std::string shortName = TrimEFT(loot.shortName);
        const std::string longName = TrimEFT(loot.longName);

        if (shortName.empty() && longName.empty())
            continue;

        std::string key;
        key.reserve(shortName.size() + 32);
        key += shortName;
        key += '|';
        key += std::to_string(loot.avgMarketPrice);
        key += '|';
        key += std::to_string(loot.traderPrice);

        auto it = groupedLoot.find(key);
        if (it == groupedLoot.end())
        {
            AggregatedLoot entry;
            entry.shortName = shortName.empty() ? longName : shortName;
            entry.longName = longName.empty() ? shortName : longName;
            entry.avgMarketPrice = loot.avgMarketPrice;
            entry.traderPrice = loot.traderPrice;
            entry.qty = 1;
            entry.nearestDistance = loot.distance;
            entry.wanted = loot.wanted;
            entry.items.push_back(&loot); 

            groupedLoot.emplace(std::move(key), std::move(entry));
        }
        else
        {
            AggregatedLoot& entry = it->second;
            entry.qty++;
            entry.items.push_back(&loot); 

            if (loot.distance < entry.nearestDistance)
                entry.nearestDistance = loot.distance;

            if (loot.wanted)
                entry.wanted = true;
        }
    }
    
    std::vector<AggregatedLoot> topLoot;
    topLoot.reserve(groupedLoot.size());

    for (auto& kv : groupedLoot)
        topLoot.push_back(std::move(kv.second));

    std::sort(topLoot.begin(), topLoot.end(),
        [](const AggregatedLoot& a, const AggregatedLoot& b)
        {
            return a.bestPrice() > b.bestPrice();
        });

    if (topLoot.size() > 6)
        topLoot.resize(6);
    
    const int visibleRows = static_cast<int>(topLoot.size());

    const float fixedWidth = 700.0f;
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float headerHeight = ImGui::GetFrameHeight() + 8.0f;
    const float padding = ImGui::GetStyle().WindowPadding.y * 2.0f + 20.0f;

    float dynamicHeight = padding + headerHeight + (visibleRows * rowHeight);
    dynamicHeight = ImClamp(dynamicHeight, 120.0f, 350.0f);

    ImGui::SetNextWindowPos(menuLayout::TopLeftWidgetPosition(), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(fixedWidth, dynamicHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetTopLoot, flags))
    {
        if (ImGui::BeginTable("##toploot", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Trader", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Market", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (int i = 0; i < (int)topLoot.size(); i++)
            {
                auto& loot = topLoot[i];

                ImGui::TableNextRow();

                // SHOW ICON
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);

                if (ImGui::SmallButton(ICON_FK_SEARCH))
                {
                    LootEntity* bestLoot = nullptr;
                    float bestDistSq = FLT_MAX;

                    for (LootEntity* item : loot.items)
                    {
                        if (!item)
                            continue;

                        const glm::vec3 difference =
                            item->worldLocation - mainGame.localLocation;

                        const float distSq =
                            difference.x * difference.x +
                            difference.y * difference.y +
                            difference.z * difference.z;

                        if (distSq < bestDistSq)
                        {
                            bestDistSq = distSq;
                            bestLoot = item;
                        }
                    }

                    if (bestLoot)
                    {
                        const auto focusLocation = Loot.focusClosestLootItem(
                            bestLoot->instance,
                            bestLoot->bsgId,
                            coloursGlobals::valueLootColour
                        );

                        if (focusLocation)
                        {
                            mapGlobals::followLocal = false;
                            mapGlobals::focusPoint = *focusLocation;
                            mapGlobals::startLootFocusRipple(*focusLocation);
                        }
                    }
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "Focus the closest instance and force it only when needed: %s",
                        loot.shortName.c_str());
                }

                ImGui::PopID();

                // QTY
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", loot.qty);

                // NAME
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(loot.shortName.c_str());

                if (ImGui::IsItemHovered() && !loot.longName.empty())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(loot.longName.c_str());
                    ImGui::EndTooltip();
                }

                // TRADER
                ImGui::TableSetColumnIndex(3);
                {
                    char traderText[16]{};
                    formatValue(loot.traderPrice, traderText);
                    ImGui::Text("%s", traderText);
                }

                // MARKET
                ImGui::TableSetColumnIndex(4);
                {
                    char marketText[16]{};
                    formatValue(loot.avgMarketPrice, marketText);
                    ImGui::Text("%s", marketText);
                }

                // DISTANCE
                ImGui::TableSetColumnIndex(5);
                if (loot.nearestDistance != INT_MAX)
                    ImGui::Text("%dm", loot.nearestDistance);
                else
                    ImGui::Text("-");
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void drawWidgetPlayers()
{
    if (!appMenu::widgetPlayers)
        return;

    const std::string windowNameMain = "Active Players";
    static ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;

    const PlayerSnapshot playerSnapshotHandle =
        registeredPlayers.getCacheSnapshot();
    const PlayerCollection& playerSnapshot = *playerSnapshotHandle;

    const auto isHumanPlayer = [](const Player& player)
    {
        return !player.isAi &&
            (player.isLocal || player.isPlayer || player.isPlayerScav);
    };

    // Count visible rows from snapshot
    int visibleRows = 0;

    for (const auto& cache : playerSnapshot)
    {
        if (!isHumanPlayer(cache))
            continue;

        if (cache.isDead || cache.hasExfiled)
            continue;

        if (!Utils::valid_pointer(cache.instance))
            continue;

        visibleRows++;
    }

    constexpr int maxVisibleRows = 12;
    const int tableRows = std::clamp(visibleRows, 1, maxVisibleRows);
    const float fixedWidth = 750.0f;
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float tableHeight =
        ImGui::GetFrameHeightWithSpacing() +
        (tableRows * rowHeight) +
        6.0f;
    const float dynamicHeight =
        ImGui::GetFrameHeight() +
        (ImGui::GetStyle().WindowPadding.y * 2.0f) +
        ImGui::GetStyle().ItemSpacing.y +
        tableHeight;

    ImGui::SetNextWindowPos(menuLayout::TopLeftWidgetPosition(), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(fixedWidth, dynamicHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetPlayers, flags))
    {
        // Persistent edit buffers per player instance
        static std::unordered_map<uint64_t, std::array<char, 16>> groupEditBuffers;

        // Edits to apply after rendering
        std::vector<std::pair<uint64_t, std::string>> pendingGroupEdits;

        if (ImGui::BeginTable(
            "##players",
            9,
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, tableHeight)))
        {
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 125.0f);
            ImGui::TableSetupColumn("LvL", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("KD(PKD)", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Hours", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Container", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (const auto& cache : playerSnapshot)
            {
                if (!isHumanPlayer(cache))
                    continue;

                if (cache.isDead || cache.hasExfiled)
                    continue;

                if (!Utils::valid_pointer(cache.instance))
                    continue;

                ImGui::TableNextRow();

                if (cache.location.x == 0.f ||
                    cache.location.y == 0.f ||
                    cache.location.z == 0.f)
                {
                    ImGui::TableSetBgColor(
                        ImGuiTableBgTarget_RowBg0,
                        IM_COL32(255, 0, 0, 255)
                    );
                }

                ImGui::TableSetColumnIndex(0);

                auto& groupBuffer = groupEditBuffers[cache.instance];

                const std::string currentGroup = cache.groupId;

                const std::string inputId =
                    "##group_" + std::to_string(cache.instance);

                const ImGuiID imguiInputId = ImGui::GetID(inputId.c_str());

                const bool inputActive =
                    ImGui::GetActiveID() == imguiInputId;

                const std::string bufferText = groupBuffer.data();

                // Keep the input box synced with the actual group unless editing.
                if (!inputActive && bufferText != currentGroup)
                {
                    std::snprintf(
                        groupBuffer.data(),
                        groupBuffer.size(),
                        "%s",
                        currentGroup.c_str()
                    );
                }

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
                ImGui::SetNextItemWidth(-FLT_MIN);

                const bool enterPressed = ImGui::InputText(
                    inputId.c_str(),
                    groupBuffer.data(),
                    groupBuffer.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue |
                    ImGuiInputTextFlags_AutoSelectAll
                );

                const bool finishedEdit = ImGui::IsItemDeactivatedAfterEdit();

                ImGui::PopStyleVar();

                if (enterPressed || finishedEdit)
                {
                    std::string newGroup = TrimEFT(groupBuffer.data());

                    // Allow "-" to clear group
                    if (newGroup == "-")
                        newGroup.clear();

                    if (newGroup != cache.groupId)
                    {
                        pendingGroupEdits.emplace_back(cache.instance, newGroup);
                    }
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted("Edit group ID");
                    ImGui::TextUnformatted("Blank or '-' clears group");
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(1);

                if (cache.isLocal)
                    ImGui::TextUnformatted("LocalPlayer");
                else
                    ImGui::TextUnformatted(cache.name.c_str());

                if (ImGui::IsItemHovered() && !cache.profileId.empty())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Memory ProfileID: %s", cache.profileId.c_str());
                    ImGui::EndTooltip();
                }

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted((cache.observedHandsInfo.itemName + " (" + std::string(cache.observedHandsInfo.ammoName) + "/"+ std::to_string(cache.observedHandsInfo.magazineCount) + ")").c_str());

                ImGui::TableSetColumnIndex(3);

                if (cache.isPlayer && !cache.isPlayerScav)
                    ImGui::Text("%d", cache.DT_lvl);
                else
                    ImGui::TextUnformatted("-");

                ImGui::TableSetColumnIndex(4);

                if (cache.isPlayer && !cache.isPlayerScav)
                    ImGui::Text("%d(%.2f)", cache.kd, cache.pkd);
                else
                    ImGui::TextUnformatted("-");

                ImGui::TableSetColumnIndex(5);

                if (cache.isPlayer && !cache.isPlayerScav)
                    ImGui::Text("%d", cache.hours);
                else
                    ImGui::TextUnformatted("-");

                ImGui::TableSetColumnIndex(6);

                {
                    char valueText[16]{};
                    formatValue(cache.playerValue, valueText);
                    ImGui::Text("%s", valueText);
                }

                ImGui::TableSetColumnIndex(7);

                bool foundContainer = false;

                for (const auto& slot : cache._slots)
                {
                    std::string slotn = TrimEFT(slot.name);

                    if (slotn == "SecuredContainer")
                    {
                        ImGui::TextUnformatted(slot.equipName.c_str());
                        foundContainer = true;
                        break;
                    }
                }

                if (!foundContainer)
                    ImGui::TextUnformatted("-");

                ImGui::TableSetColumnIndex(8);
                ImGui::Text("%dm", cache.distance);
            }

            ImGui::EndTable();
        }

        if (!pendingGroupEdits.empty())
            registeredPlayers.applyGroupEdits(pendingGroupEdits);
    }

    ImGui::End();
}

void drawQuests()
{
    if (radarGlobals::minimalView ||
        radarGlobals::drawQuestHelper == FALSE)
        return;

    const QuestPublishedSnapshot questSnapshot =
        GetQuestPublishedSnapshot();
    const std::vector<QuestLocation>& locations =
        questSnapshot->masterLocations;

    if (locations.empty())
        return;

    const std::string currentMapId = TrimEFT(mainGame.selectedLocation);

    for (const auto& loc : locations)
    {
        // Convert id to name
        std::string mapName(MapNames::GetNameFromId(loc.mapNameId));

        // Map filter
        if (!(Utils::Text::containsIgnoreCase(mapName, currentMapId) ||
            Utils::Text::containsIgnoreCase(currentMapId, mapName)))
            continue;

        if (loc.objectiveType == "findQuestItem")
            continue;

        // Convert world to map coords
        glm::vec3 mapPos =
            mapControl.getMapPosition(loc.pos, currentMap::configX, currentMap::configY, currentMap::configScale);

        
        DrawQuest(mapPos.x, mapPos.y, mapControl.zoomLevel, loc);
    }
}

void drawExfils() {

    if (radarGlobals::minimalView ||
        radarGlobals::drawExfils == FALSE)
        return;

    const ExfilCacheSnapshot cacheSnapshot =
        exfil.getCacheExfilSnapshot();
    const ExfilCacheCollection& cache = *cacheSnapshot;

    if (appMenu::widgetExfil_Scav == FALSE) // show pmc
    {
        for (const auto& exfil : cache)
        {
            if ((exfil.type == ExfilType::Secret && !radarGlobals::drawSecretExfils) ||
                (exfil.type == ExfilType::Transit && !radarGlobals::drawTransitExfils))
            {
                continue;
            }

            glm::vec3 location = mapControl.getMapPosition(exfil.locationWorld, currentMap::configX, currentMap::configY, currentMap::configScale);

            DrawExfil(location.x, location.y, mapControl.zoomLevel, exfil);
        }
    }

}

void drawLoot()
{
    if (radarGlobals::minimalView)
    {
        DrawRadarLootClusterPanel({}, {});
        DrawRadarCorpseHoverPanel({}, {});
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (now < mapGlobals::lootFocusRippleUntil)
    {
        const float remainingSeconds =
            std::chrono::duration<float>(mapGlobals::lootFocusRippleUntil - now).count();
        const float elapsedSeconds = 5.0f - remainingSeconds;
        float ripplePhase = elapsedSeconds / 0.8f;
        ripplePhase -= static_cast<int>(ripplePhase);

        const glm::vec3 rippleLocation = mapControl.getMapPosition(
            mapGlobals::lootFocusRippleLocation,
            currentMap::configX,
            currentMap::configY,
            currentMap::configScale);
        DrawLootFocusRipple(
            rippleLocation.x,
            rippleLocation.y,
            ripplePhase,
            coloursGlobals::valueLootColour);
    }

    if (!radarGlobals::drawLoot)
    {
        DrawRadarLootClusterPanel({}, {});
        DrawRadarCorpseHoverPanel({}, {});
        return;
    }

    const LootCacheSnapshot cacheSnapshot = Loot.getCacheSnapshot();
    const LootCacheCollection& cacheLoot = *cacheSnapshot;

    struct VisibleRadarLoot
    {
        const LootEntity* loot = nullptr;
        glm::vec3 screenPosition{};
    };

    std::vector<VisibleRadarLoot> visibleLoot;
    visibleLoot.reserve(cacheLoot.size());

    for (const LootEntity& itemLoot : cacheLoot)
    {
        if (itemLoot.pendingResolve || itemLoot.failed || !itemLoot.hasValidPosition)
            continue;

        bool visible = false;
        if (itemLoot.isContainer())
        {
            visible = itemLoot.wanted &&
                itemLoot.distance <= lootGlobals::containerDistance;
        }
        else if (itemLoot.isItem())
        {
            visible = itemLoot.wanted || itemLoot.forceWanted;
        }
        else if (itemLoot.isQuestItem())
        {
            visible = radarGlobals::drawQuestHelper &&
                (itemLoot.wanted || itemLoot.forceWanted);
        }
        else if (itemLoot.isCorpse())
        {
            visible = itemLoot.wanted;
        }

        if (!visible)
            continue;

        visibleLoot.push_back({
            &itemLoot,
            mapControl.getMapPosition(
                itemLoot.worldLocation,
                currentMap::configX,
                currentMap::configY,
                currentMap::configScale)
            });
    }

    constexpr float kLootClusterRadiusMetres = 2.0f;
    constexpr float kLootClusterRadiusSquared =
        kLootClusterRadiusMetres * kLootClusterRadiusMetres;

    std::vector<size_t> parents(visibleLoot.size());
    std::iota(parents.begin(), parents.end(), 0);

    auto findRoot = [&parents](size_t index)
        {
            size_t root = index;
            while (parents[root] != root)
                root = parents[root];

            while (parents[index] != index)
            {
                const size_t parent = parents[index];
                parents[index] = root;
                index = parent;
            }

            return root;
        };

    auto makeSpatialCellKey = [](const int x, const int z)
        {
            return
                (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
                static_cast<uint32_t>(z);
        };

    std::unordered_map<uint64_t, std::vector<size_t>> spatialCells;
    spatialCells.reserve(visibleLoot.size());

    for (size_t index = 0; index < visibleLoot.size(); ++index)
    {
        // Quest loot keeps its dedicated marker and never joins the compact
        // nearby-loot cluster
        if (visibleLoot[index].loot->isQuestItem())
            continue;

        const glm::vec3& position = visibleLoot[index].loot->worldLocation;
        const int cellX = static_cast<int>(std::floor(position.x / kLootClusterRadiusMetres));
        const int cellZ = static_cast<int>(std::floor(position.z / kLootClusterRadiusMetres));

        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            for (int offsetZ = -1; offsetZ <= 1; ++offsetZ)
            {
                const auto nearbyCell = spatialCells.find(
                    makeSpatialCellKey(cellX + offsetX, cellZ + offsetZ));
                if (nearbyCell == spatialCells.end())
                    continue;

                for (const size_t nearbyIndex : nearbyCell->second)
                {
                    const glm::vec3& nearbyPosition = visibleLoot[nearbyIndex].loot->worldLocation;
                    const float deltaX = position.x - nearbyPosition.x;
                    const float deltaZ = position.z - nearbyPosition.z;
                    const float horizontalDistanceSquared =
                        (deltaX * deltaX) +
                        (deltaZ * deltaZ);

                    if (horizontalDistanceSquared > kLootClusterRadiusSquared)
                        continue;

                    const size_t indexRoot = findRoot(index);
                    const size_t nearbyRoot = findRoot(nearbyIndex);
                    if (indexRoot != nearbyRoot)
                        parents[indexRoot] = nearbyRoot;
                }
            }
        }

        spatialCells[makeSpatialCellKey(cellX, cellZ)].push_back(index);
    }

    std::vector<size_t> componentRoots;
    std::vector<std::vector<size_t>> componentMembers;
    componentRoots.reserve(visibleLoot.size());
    componentMembers.reserve(visibleLoot.size());

    for (size_t index = 0; index < visibleLoot.size(); ++index)
    {
        const size_t root = findRoot(index);
        const auto existingRoot = std::find(componentRoots.begin(), componentRoots.end(), root);
        if (existingRoot == componentRoots.end())
        {
            componentRoots.push_back(root);
            componentMembers.push_back({ index });
        }
        else
        {
            const size_t componentIndex = static_cast<size_t>(existingRoot - componentRoots.begin());
            componentMembers[componentIndex].push_back(index);
        }
    }

    // Split chain-shaped connected components so every pair of entries in a displayed group is genuinely within the configured 2 metre radius
    std::vector<std::vector<size_t>> strictComponentMembers;
    strictComponentMembers.reserve(componentMembers.size());

    for (const std::vector<size_t>& component : componentMembers)
    {
        std::vector<std::vector<size_t>> componentGroups;

        for (const size_t candidateIndex : component)
        {
            const glm::vec3& candidatePosition = visibleLoot[candidateIndex].loot->worldLocation;
            bool addedToGroup = false;

            for (std::vector<size_t>& group : componentGroups)
            {
                const bool isWithinEveryMember = std::all_of(
                    group.begin(),
                    group.end(),
                    [&](const size_t memberIndex)
                    {
                        const glm::vec3& memberPosition = visibleLoot[memberIndex].loot->worldLocation;
                        const float deltaX = candidatePosition.x - memberPosition.x;
                        const float deltaZ = candidatePosition.z - memberPosition.z;
                        return (deltaX * deltaX) + (deltaZ * deltaZ) <=
                            kLootClusterRadiusSquared;
                    });

                if (!isWithinEveryMember)
                    continue;

                group.push_back(candidateIndex);
                addedToGroup = true;
                break;
            }

            if (!addedToGroup)
                componentGroups.push_back({ candidateIndex });
        }

        for (std::vector<size_t>& group : componentGroups)
            strictComponentMembers.push_back(std::move(group));
    }

    componentMembers = std::move(strictComponentMembers);

    const PlayerSnapshot playerSnapshot = registeredPlayers.getCacheSnapshot();
    const PlayerCollection& players = *playerSnapshot;

    std::vector<RadarLootCluster> lootClusters;
    std::vector<size_t> singleLootIndices;
    lootClusters.reserve(componentMembers.size());
    singleLootIndices.reserve(componentMembers.size());

    for (const std::vector<size_t>& members : componentMembers)
    {
        if (members.size() < 2)
        {
            singleLootIndices.push_back(members.front());
            continue;
        }

        RadarLootCluster cluster{};
        cluster.entries.reserve(members.size());
        uint64_t stableId = 0;

        for (const size_t memberIndex : members)
        {
            const VisibleRadarLoot& member = visibleLoot[memberIndex];
            cluster.entries.push_back(member.loot);
            cluster.worldCenter += member.loot->worldLocation;
            cluster.screenPosition.x += member.screenPosition.x;
            cluster.screenPosition.y += member.screenPosition.y;

            const uint64_t entryId = GetRadarLootEntityUiId(*member.loot);
            if (entryId != 0 && (stableId == 0 || entryId < stableId))
                stableId = entryId;
        }

        const float inverseCount = 1.0f / static_cast<float>(members.size());
        cluster.worldCenter *= inverseCount;
        cluster.screenPosition.x *= inverseCount;
        cluster.screenPosition.y *= inverseCount;
        cluster.id = stableId;

        for (const Player& player : players)
        {
            if (player.isLocal ||
                player.isDead ||
                player.hasExfiled ||
                player.isBTR ||
                !Utils::valid_pointer(player.instance))
            {
                continue;
            }

            for (const LootEntity* entry : cluster.entries)
            {
                const float deltaX = player.location.x - entry->worldLocation.x;
                const float deltaZ = player.location.z - entry->worldLocation.z;
                const float horizontalDistanceSquared =
                    (deltaX * deltaX) +
                    (deltaZ * deltaZ);

                if (horizontalDistanceSquared <= kLootClusterRadiusSquared)
                {
                    cluster.popupSuppressed = true;
                    break;
                }
            }

            if (cluster.popupSuppressed)
                break;
        }

        lootClusters.push_back(std::move(cluster));
    }

    std::vector<RadarCorpseHoverCandidate> hoveredCorpses;
    std::vector<const LootEntity*> visibleCorpses;
    std::vector<RadarLootClusterHoverCandidate> hoveredClusters;

    auto drawSingleLoot = [&](const size_t visibleIndex)
        {
            const VisibleRadarLoot& visibleEntry = visibleLoot[visibleIndex];
            const LootEntity& itemLoot = *visibleEntry.loot;
            const glm::vec3& location = visibleEntry.screenPosition;

            if (itemLoot.isContainer())
            {
                DrawLootContainerMarker(
                    location.x,
                    location.y,
                    itemLoot.color,
                    mapControl.zoomLevel,
                    itemLoot);
            }
            else if (itemLoot.isItem())
            {
                DrawLootItemMarker(
                    location.x,
                    location.y,
                    itemLoot.color,
                    mapControl.zoomLevel,
                    itemLoot);
            }
            else if (itemLoot.isQuestItem())
            {
                DrawLootItemMarker(
                    location.x,
                    location.y,
                    coloursGlobals::questColour,
                    mapControl.zoomLevel,
                    itemLoot);
            }
            else if (itemLoot.isCorpse())
            {
                visibleCorpses.push_back(&itemLoot);
                const float hoverDistanceSquared = DrawRadarPlayerCorpseMarkers(
                    static_cast<int>(std::round(location.x)),
                    static_cast<int>(std::round(location.y)),
                    mapControl.zoomLevel,
                    itemLoot);

                if (hoverDistanceSquared < FLT_MAX)
                    hoveredCorpses.push_back({ &itemLoot, hoverDistanceSquared });
            }
        };

    for (const size_t visibleIndex : singleLootIndices)
        drawSingleLoot(visibleIndex);

    for (const RadarLootCluster& cluster : lootClusters)
    {
        const float hoverDistanceSquared = DrawRadarLootClusterMarker(
            cluster,
            mapControl.zoomLevel);
        if (hoverDistanceSquared < FLT_MAX)
            hoveredClusters.push_back({ &cluster, hoverDistanceSquared });
    }

    DrawRadarLootClusterPanel(hoveredClusters, lootClusters);
    DrawRadarCorpseHoverPanel(hoveredCorpses, visibleCorpses);
}

void drawWidgetExfils()
{

    if ((radarGlobals::minimalView && appGlobals::runRadar.load()) || !appMenu::widgetExfil)
        return;

    std::string windowNameMain = "Raid Extracts";
    static ImGuiWindowFlags flagss =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize;

    ImGui::SetNextWindowPos(menuLayout::TopLeftWidgetPosition(), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    const ExfilCacheSnapshot exfilSnapshot =
        exfil.getCacheExfilSnapshot();
    const ExfilCacheCollection& exfils = *exfilSnapshot;

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetExfil, flagss))
    {



        if (ImGui::BeginTable("##exfils", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 75.f);
            ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();


            for (const auto& cache : exfils)
            {
                if ((cache.type == ExfilType::Secret && !radarGlobals::drawSecretExfils) ||
                    (cache.type == ExfilType::Transit && !radarGlobals::drawTransitExfils))
                {
                    continue;
                }


                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", cache.extractName.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", cache.status.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%sm", std::to_string(cache.distance).c_str());



            }


        }
        ImGui::EndTable();
    }

    ImGui::End();

}

void drawGrenades()
{
    if (!radarGlobals::drawGrenades)
        return;

    const GrenadeCacheSnapshot grenadeSnapshot =
        explosiveManager.getGrenadesSnapshot();
    const GrenadeCacheCollection& cacheGrenades = *grenadeSnapshot;

    if (cacheGrenades.empty())
        return;

    for (const GrenadeList& grenade : cacheGrenades)
    {
        if (grenade.type != ExplosiveType::Grenade)
            continue;

        glm::vec3 locationMap = mapControl.getMapPosition(grenade.worldLocation, currentMap::configX, currentMap::configY, currentMap::configScale);

        DrawGrenade(locationMap.x, locationMap.y, mapControl.zoomLevel, grenade);

    }
}

void drawTripwires()
{
    if (!radarGlobals::drawTripwires)
        return;

    const GrenadeCacheSnapshot explosiveSnapshot = explosiveManager.getGrenadesSnapshot();
    const GrenadeCacheCollection& explosives = *explosiveSnapshot;

    for (const GrenadeList& tripwire : explosives)
    {
        if (tripwire.type != ExplosiveType::Tripwire ||
            !tripwire.isActive ||
            !Utils::isGoodVec3(tripwire.worldLocation) ||
            !Utils::isGoodVec3(tripwire.fromWorldLocation))
        {
            continue;
        }

        const glm::vec3 toMap = mapControl.getMapPosition(
            tripwire.worldLocation,
            currentMap::configX,
            currentMap::configY,
            currentMap::configScale);
        const glm::vec3 fromMap = mapControl.getMapPosition(
            tripwire.fromWorldLocation,
            currentMap::configX,
            currentMap::configY,
            currentMap::configScale);

        if (radarGlobals::drawTripwireLine)
        {
            DrawLine(
                fromMap.x,
                fromMap.y,
                toMap.x,
                toMap.y,
                coloursGlobals::tripwires,
                2.0f);
        }

        DrawTripWire(
            static_cast<int>(toMap.x),
            static_cast<int>(toMap.y),
            coloursGlobals::tripwires,
            mapControl.zoomLevel);
    }
}
