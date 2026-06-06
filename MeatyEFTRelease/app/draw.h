#pragma once
#include "../game/headers/questManager.h"
#include "text.h"
#include "../game/headers/utils.h"
#include "../game/headers/players.h"
#include "../game/headers/explosives.h"



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

    std::vector<PlayerCache> cache = players.getCacheSnapshot();
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

            DrawRadarPlayerMarkers(position.x, position.y, mapControl.zoomLevel, player);

            if (!player.isBTR)
            {
                int aimLineLen = 100;
                if (player.groupId != mainGame.localGroupId)
                    aimLineLen = radarGlobals::enemyAimLine;
                else
                    aimLineLen = radarGlobals::friendAimLine;

                if (mainGame.localGroupId == "")
                    aimLineLen = radarGlobals::enemyAimLine;

                drawAimLine(
                    glm::vec2(position.x - 5.f, position.y - 5.f),
                    player.rotation,
                    aimLineLen,
                    player.colour
                );



                drawGroupLine(position, player);

                
            }
            
        }


    }

}

void drawLocalPlayer()
{
    //localPlayer position on map
    glm::vec3 position = mapControl.getMapPosition(mainGame.localLocation, currentMap::configX, currentMap::configY, currentMap::configScale);



    drawAimLine(
        glm::vec2(position.x - 4.f, position.y - 4.f),
        mainGame.localRotation,
        radarGlobals::localAimLine,
        coloursGlobals::playerLocal
    );

    DrawCircleFilled(position.x - 4.f, position.y - 4.f, 8, ImColor(coloursGlobals::playerLocal.x, coloursGlobals::playerLocal.y, coloursGlobals::playerLocal.z, coloursGlobals::playerLocal.w));

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
    if (!appMenu::widgetTopLoot)
        return;

    const std::string windowNameMain = "Top Loot";
    static ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;

    std::vector<LootList> lootCache = Loot.getCacheLoot();

    struct AggregatedLoot
    {
        std::string shortName;
        std::string longName;
        int avgMarketPrice = 0;
        int traderPrice = 0;
        int qty = 0;
        int nearestDistance = INT_MAX;
        bool wanted = false;

        std::vector<LootList*> items;

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

        if (!loot.isItem)
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
                    LootList* bestLoot = nullptr;
                    float bestDist = FLT_MAX;

                    for (LootList* item : loot.items)
                    {
                        if (!item)
                            continue;

                        glm::vec3 d = item->worldLocation - mainGame.localLocation;
                        float distSq = d.x * d.x + d.y * d.y + d.z * d.z;

                        if (distSq < bestDist)
                        {
                            bestDist = distSq;
                            bestLoot = item;
                        }
                    }

                    if (bestLoot)
                    {
                        mapGlobals::followLocal = false;
                        mapGlobals::focusPoint = bestLoot->worldLocation;

                        for (LootList* item : loot.items)
                        {
                            if (!item)
                                continue;

                            item->wanted = true;
                            item->forceWanted = true;
                            item->color = coloursGlobals::valueLootColour;
                        }
                    }
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Focus nearest and mark wanted: %s", loot.shortName.c_str());
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
        ImGuiWindowFlags_NoResize;

    std::vector<PlayerCache> playerSnapshot = players.getCacheSnapshot();

    // Count visible rows from snapshot
    int visibleRows = 0;

    for (const auto& cache : playerSnapshot)
    {
        if (cache.isAi && !cache.isBoss)
            continue;

        if (cache.isDead || cache.hasExfiled)
            continue;

        if (!Utils::valid_pointer(cache.instance))
            continue;

        visibleRows++;
    }

    const float fixedWidth = 750.0f;
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float headerHeight = ImGui::GetFrameHeight() + 8.0f;
    const float padding = ImGui::GetStyle().WindowPadding.y * 2.0f + 20.0f;

    float dynamicHeight = padding + headerHeight + (visibleRows * rowHeight);
    dynamicHeight = ImClamp(dynamicHeight, 120.0f, 600.0f);

    ImGui::SetNextWindowSize(ImVec2(fixedWidth, dynamicHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetPlayers, flags))
    {
        // Persistent edit buffers per player instance
        static std::unordered_map<uint64_t, std::array<char, 16>> groupEditBuffers;

        // Edits to apply after rendering
        std::vector<std::pair<uint64_t, std::string>> pendingGroupEdits;

        if (ImGui::BeginTable("##players", 10, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Side", ImGuiTableColumnFlags_WidthFixed, 35.0f);
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, 175.0f);
            ImGui::TableSetupColumn("LvL", ImGuiTableColumnFlags_WidthFixed, 35.0f);
            ImGui::TableSetupColumn("KD(PKD)", ImGuiTableColumnFlags_WidthFixed, 45.0f);
            ImGui::TableSetupColumn("Hours", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Container", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableHeadersRow();

            for (const auto& cache : playerSnapshot)
            {
                if (cache.isAi && !cache.isBoss)
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

                // Side
                ImGui::TableSetColumnIndex(0);

                if (!cache.side.empty())
                    ImGui::Text("%c", cache.side[0]);
                else
                    ImGui::Text("?");

                // Editable Group ID
                ImGui::TableSetColumnIndex(1);

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

                // Name
                ImGui::TableSetColumnIndex(2);

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

                // Item
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted((cache.observedHandsInfo.itemName + " (" + std::string(cache.observedHandsInfo.ammoName) + "/"+ std::to_string(cache.observedHandsInfo.magazineCount) + ")").c_str());

                // LvL
                ImGui::TableSetColumnIndex(4);

                if (cache.isPlayer && !cache.isPlayerScav)
                    ImGui::Text("%d", cache.DT_lvl);
                else
                    ImGui::TextUnformatted("-");

                // KD
                ImGui::TableSetColumnIndex(5);

                if (cache.isPlayer && !cache.isPlayerScav)
                    ImGui::Text("%d(%.2f)", cache.kd, cache.pkd);
                else
                    ImGui::TextUnformatted("-");

                // Hours
                ImGui::TableSetColumnIndex(6);

                if (cache.isPlayer && !cache.isPlayerScav)
                    ImGui::Text("%d", cache.hours);
                else
                    ImGui::TextUnformatted("-");

                // Value
                ImGui::TableSetColumnIndex(7);

                {
                    char valueText[16]{};
                    formatValue(cache.playerValue, valueText);
                    ImGui::Text("%s", valueText);
                }

                // Container
                ImGui::TableSetColumnIndex(8);

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

                // Distance
                ImGui::TableSetColumnIndex(9);
                ImGui::Text("%dm", cache.distance);
            }

            ImGui::EndTable();
        }

        if (!pendingGroupEdits.empty())
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& liveCache = players.getCache();

            for (const auto& [instance, newGroupId] : pendingGroupEdits)
            {
                auto it = std::find_if(
                    liveCache.begin(),
                    liveCache.end(),
                    [&](const PlayerCache& player)
                    {
                        return player.instance == instance;
                    });

                if (it == liveCache.end())
                    continue;

                if (it->isDead || it->hasExfiled)
                    continue;

                it->groupId = newGroupId;

                // If editing local player's group, sync mainGame.localGroupId.
                if (it->isLocal || it->instance == mainGame.localPlayerPtr)
                {
                    if (newGroupId.empty() || newGroupId == "0")
                        mainGame.localGroupId.clear();
                    else
                        mainGame.localGroupId = newGroupId;
                }

                std::ostringstream ss;
                ss << "[PLAYERS][GROUP EDIT] "
                    << it->name
                    << " instance: 0x"
                    << std::hex << it->instance
                    << " groupId: "
                    << (newGroupId.empty() ? "none" : newGroupId);

                LOGS.logInfo(ss.str());
            }
        }
    }

    ImGui::End();
}

void drawQuests()
{
    if (radarGlobals::drawQuestHelper == FALSE)
        return;

    if (masterLocations.empty())
        return;

    const std::string currentMapId = TrimEFT(mainGame.selectedLocation);

    for (auto& loc : masterLocations)
    {
        // Map filter
        if (!(Utils::Text::containsIgnoreCase(loc.mapNameId, currentMapId) ||
            Utils::Text::containsIgnoreCase(currentMapId, loc.mapNameId)))
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

    if (radarGlobals::drawExfils == FALSE)
        return;

    std::vector<exfilsMemory>& cache = exfil.getCacheExfil();

    if (appMenu::widgetExfil_Scav == FALSE) // show pmc
    {
        for (auto& exfil : cache)
        {
            glm::vec3 location = mapControl.getMapPosition(exfil.locationWorld, currentMap::configX, currentMap::configY, currentMap::configScale);

            DrawExfil(location.x, location.y, mapControl.zoomLevel, exfil);
        }
    }

}

void drawLoot()
{
    if (!radarGlobals::drawLoot)
        return;

    std::vector<LootList> cacheLoot = Loot.getCacheLoot();

    if (cacheLoot.size() == 0)
        return;

    for (auto& itemLoot : cacheLoot)
    {

        if (itemLoot.pendingResolve || itemLoot.failed)
            continue;

        if (!itemLoot.hasValidPosition)
            continue;

        if (itemLoot.isContainer)
        {
            if (!itemLoot.wanted)
                continue;

            glm::vec3 location = mapControl.getMapPosition(itemLoot.worldLocation, currentMap::configX, currentMap::configY, currentMap::configScale);
            DrawLootContainerMarker(location.x, location.y, itemLoot.color, mapControl.zoomLevel, itemLoot);

        }


        if (itemLoot.isItem)
        {
            if (!itemLoot.wanted && !itemLoot.forceWanted)
                continue;

            glm::vec3 location = mapControl.getMapPosition(itemLoot.worldLocation, currentMap::configX, currentMap::configY, currentMap::configScale);
            DrawLootItemMarker(location.x, location.y, itemLoot.color, mapControl.zoomLevel, itemLoot);
        }

        if (itemLoot.isQuestItem && radarGlobals::drawQuestHelper)
        {
            if (!itemLoot.wanted && !itemLoot.forceWanted)
                continue;

                glm::vec3 location =
                    mapControl.getMapPosition(
                        itemLoot.worldLocation,
                        currentMap::configX,
                        currentMap::configY,
                        currentMap::configScale);

                DrawLootItemMarker(
                    location.x,
                    location.y,
                    coloursGlobals::questColour,
                    mapControl.zoomLevel,
                    itemLoot);

              
        }

        if (itemLoot.isCorpse)
        {
            if (!itemLoot.wanted)
                continue;

            glm::vec3 location = mapControl.getMapPosition(itemLoot.worldLocation, currentMap::configX, currentMap::configY, currentMap::configScale);
            DrawRadarPlayerCorpseMarkers(location.x, location.y, mapControl.zoomLevel, itemLoot);
        }

    }
}

void drawWidgetExfils()
{

    if (!appMenu::widgetExfil)
        return;

    std::string windowNameMain = "Raid Extracts";
    static ImGuiWindowFlags flagss = NULL;// ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    std::vector<exfilsMemory>& exfils = exfil.getCacheExfil();

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetExfil, flagss))
    {



        if (ImGui::BeginTable("##exfils", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 75.f);
            ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();


            for (auto& cache : exfils)
            {


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
    std::vector<GrenadeList> cacheGrenades = explosiveManager.getGrenades();

    if (cacheGrenades.size() == 0)
        return;

    for (auto& grenades : cacheGrenades)
    {
        glm::vec3 locationMap = mapControl.getMapPosition(grenades.worldLocation, currentMap::configX, currentMap::configY, currentMap::configScale);

        DrawGrenade(locationMap.x, locationMap.y, mapControl.zoomLevel, grenades);

    }
}
