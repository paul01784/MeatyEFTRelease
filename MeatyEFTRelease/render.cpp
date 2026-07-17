#include "app/includes.h"
#include "memory/Memory.h"
#include "app/debug.h"
#include "app/globals.h"
#include "app/perfMonitor.h"

#include "external/glm/glm.hpp"
#include "external/glm/gtc/matrix_access.hpp"
#include "app/maps.h"
#include "game/headers/maingame.h"
#include "game/headers/loot.h"
#include "app/market.h"
#include "game/headers/players.h"
#include "app/draw.h"
#include "app/config.h"
#include "app/DxRenderWindow.h"
#include "app/fuserRender.h"
#include "game/headers/exfil.h"
#include "game/headers/tarkovdevquery.h"
#include "game/headers/questManager.h"
#include "game/headers/wishlist.h"
#include "app/DogTagAPI.h"
#include "app/makcu.h"
#include "game/headers/watchList.h"


ConfigManager configManager("config.json", "lootFilters.json");

// select what window to not close on run
// settings,lootfilters,players,fuser
void closeSettingWindows(std::string dontClose)
{
    if (dontClose != "settings")
        appMenu::appSettings = false;
    if (dontClose != "lootfilters")
        appMenu::appLootFilters = false;
    if (dontClose != "quests")
        appMenu::appQuests = false;
    if (dontClose != "fuser")
        appMenu::appFuser = false;
    if (dontClose != "makcu")
        appMenu::appMakcu = false;
    if (dontClose != "watchlist")
        appMenu::appWatchList = false;
}

// Function to convert enum to string for display purposes
const char* WindowsKeyToString(WindowsKey key) {
    switch (key) {
    case WindowsKey::LeftControl: return "Left Control";
    case WindowsKey::LeftAlt: return "Left Alt";
    case WindowsKey::LeftShift: return "Left Shift";
    case WindowsKey::Mouse0: return "Mouse 0";
    case WindowsKey::Mouse1: return "Mouse 1";
    case WindowsKey::Mouse2: return "Mouse 2";
    case WindowsKey::Mouse3: return "Mouse 3";
    case WindowsKey::Mouse4: return "Mouse 4";
    case WindowsKey::Enter: return "Enter";
    case WindowsKey::F12: return "F12";
    default: return "Unknown";
    }
}



int WindowsKeyToIndex(WindowsKey key) {
    switch (key) {
    case WindowsKey::Mouse0: return 0;
    case WindowsKey::Mouse1: return 1;
    case WindowsKey::Mouse2: return 2;
    case WindowsKey::Mouse3: return 3;
    case WindowsKey::Mouse4: return 4;
    case WindowsKey::LeftControl: return 5;
    case WindowsKey::LeftAlt: return 6;
    case WindowsKey::LeftShift: return 7;
    case WindowsKey::Enter: return 8;
    case WindowsKey::F12: return 9;
    default: return -1;
    }
}

WindowsKey IndexToWindowsKey(int index) {
    switch (index) {
    case 0: return WindowsKey::Mouse0;
    case 1: return WindowsKey::Mouse1;
    case 2: return WindowsKey::Mouse2;
    case 3: return WindowsKey::Mouse3;
    case 4: return WindowsKey::Mouse4;
    case 5: return WindowsKey::LeftControl;
    case 6: return WindowsKey::LeftAlt;
    case 7: return WindowsKey::LeftShift;
    case 8: return WindowsKey::Enter;
    case 9: return WindowsKey::F12;
    default: return WindowsKey::LeftControl;
    }
}

std::vector<WindowsKey> GetAllWindowsKeys() {
    return {
        WindowsKey::Mouse0,
        WindowsKey::Mouse1,
        WindowsKey::Mouse2,
        WindowsKey::Mouse3,
        WindowsKey::Mouse4,
        WindowsKey::LeftControl,
        WindowsKey::LeftAlt,
        WindowsKey::LeftShift,
        WindowsKey::Enter,
        WindowsKey::F12,
    };
}

boneListIndexes IndexToBoneList(int index) {
    switch (index) {
    case 0: return boneListIndexes::Pelvis;
    case 1: return boneListIndexes::Head;
    case 2: return boneListIndexes::Neck;
    case 3: return boneListIndexes::Spine;
    case 4: return boneListIndexes::LForearm;
    case 5: return boneListIndexes::LPalm;
    case 6: return boneListIndexes::RForearm;
    case 7: return boneListIndexes::RPalm;
    case 8: return boneListIndexes::LThigh;
    case 9: return boneListIndexes::LFoot;
    case 10: return boneListIndexes::RThigh;
    case 11: return boneListIndexes::RFoot;
    }
}



bool showResSelectionBox()
{
    if (espGlobals::gameRes.x == 3440 &&
        espGlobals::gameRes.y == 1440)
    {
        espGlobals::gameResInt = RES_3440X1440;
    }
    else if (espGlobals::gameRes.x == 2560 &&
        espGlobals::gameRes.y == 1440)
    {
        espGlobals::gameResInt = RES_1440P;
    }
    else
    {
        espGlobals::gameResInt = RES_1080P;
    }

    // Resolution options
    const char* resolutionOptions[] = { "1920x1080", "2560x1440", "3440x1440" };

    if (ImGui::Combo(" Game Resolution", &espGlobals::gameResInt, resolutionOptions, IM_ARRAYSIZE(resolutionOptions))) {
        // Update resolution based on selection
        switch (espGlobals::gameResInt)
        {
        case RES_1080P:
            espGlobals::gameRes = { 1920, 1080 };
            break;

        case RES_1440P:
            espGlobals::gameRes = { 2560, 1440 };
            break;

        case RES_3440X1440:
            espGlobals::gameRes = { 3440, 1440 };
            break;

        default:
            espGlobals::gameResInt = RES_1080P;
            espGlobals::gameRes = { 1920, 1080 };
            break;
        }
        return true;
    }
    return false;
}

bool ShowKeySelectionBox(WindowsKey& aimKey, std::string selection_name) {
    static std::vector<WindowsKey> keys = GetAllWindowsKeys();
    static std::vector<const char*> items;

    if (items.empty()) {
        for (const auto& key : keys) {
            items.push_back(WindowsKeyToString(key));
        }
    }

    int currentItem = std::distance(keys.begin(), std::find(keys.begin(), keys.end(), aimKey));


    if (ImGui::BeginCombo(selection_name.c_str(), items[currentItem])) {
        for (int i = 0; i < items.size(); i++) {
            bool isSelected = (currentItem == i);
            if (ImGui::Selectable(items[i], isSelected)) {
                currentItem = i;
                aimKey = IndexToWindowsKey(i); // Map selection to enum
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return true;
}

bool LoadTextureFromFile(const char* filename, PDIRECT3DTEXTURE9* out_texture, int* out_width, int* out_height)
{
    // Load texture from disk
    PDIRECT3DTEXTURE9 texture;
    HRESULT hr = D3DXCreateTextureFromFile(g_pd3dDevice, filename, &texture);
    if (hr != S_OK)
        return false;

    // Retrieve description of the texture surface so we can access its size
    D3DSURFACE_DESC my_image_desc;
    texture->GetLevelDesc(0, &my_image_desc);
    *out_texture = texture;
    *out_width = (int)my_image_desc.Width;
    *out_height = (int)my_image_desc.Height;
    return true;
}

static void renderMapDetails()
{
    float map_orgW = 0.0f;
    float map_orgH = 0.0f;
    PDIRECT3DTEXTURE9 texture = NULL;

    const float height = mainGame.localLocation.y;

    if (mainGame.selectedLocation.empty())
        return;

    if (mainGame.selectedLocation == "bigmap") // Customs
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = customs_configX;
            currentMap::configY = customs_configY;
            currentMap::configScale = customs_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = customs_orgW;
        map_orgH = customs_orgH;
        texture = customs_texture;
    }
    else if (mainGame.selectedLocation == "factory4_day" || mainGame.selectedLocation == "factory4_night") // Factory
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = factory_configX;
            currentMap::configY = factory_configY;
            currentMap::configScale = factory_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = factory_orgW;
        map_orgH = factory_orgH;

        if (height < factory_texture0_MinHeight)
            texture = factory_textureBase;
        else
            texture = factory_texture0;
    }
    else if (mainGame.selectedLocation == "Interchange")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = interchange_configX;
            currentMap::configY = interchange_configY;
            currentMap::configScale = interchange_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = interchange_orgW;
        map_orgH = interchange_orgH;

        if (height < interchange_texture1_MinHeight)
            texture = interchange_texture0;
        else if (height < interchange_texture2_MinHeight)
            texture = interchange_texture1;
        else
            texture = interchange_texture2;
    }
    else if (mainGame.selectedLocation == "laboratory" || mainGame.selectedLocation == "laboratory_dark")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = labs_configX;
            currentMap::configY = labs_configY;
            currentMap::configScale = labs_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = labs_orgW;
        map_orgH = labs_orgH;

        if (height < labs_texture1_MinHeight)
            texture = labs_texture0;
        else if (height < labs_texture2_MinHeight)
            texture = labs_texture1;
        else
            texture = labs_texture2;
    }
    else if (mainGame.selectedLocation == "Lighthouse")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = lighthouse_configX;
            currentMap::configY = lighthouse_configY;
            currentMap::configScale = lighthouse_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = lighthouse_orgW;
        map_orgH = lighthouse_orgH;
        texture = lighthouse_texture;
    }
    else if (mainGame.selectedLocation == "RezervBase")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = reserve_configX;
            currentMap::configY = reserve_configY;
            currentMap::configScale = reserve_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = reserve_orgW;
        map_orgH = reserve_orgH;

        if (height < reserve_texture0_MinHeight)
            texture = reserve_texture_base;
        else
            texture = reserve_texture0;
    }
    else if (mainGame.selectedLocation == "Shoreline")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = shoreline_configX;
            currentMap::configY = shoreline_configY;
            currentMap::configScale = shoreline_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = shoreline_orgW;
        map_orgH = shoreline_orgH;
        texture = shoreline_texture0;
    }
    else if (mainGame.selectedLocation == "TarkovStreets")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = streets_configX;
            currentMap::configY = streets_configY;
            currentMap::configScale = streets_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = streets_orgW;
        map_orgH = streets_orgH;
        texture = streets_texture0;
    }
    else if (mainGame.selectedLocation == "Woods")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = woods_configX;
            currentMap::configY = woods_configY;
            currentMap::configScale = woods_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = woods_orgW;
        map_orgH = woods_orgH;
        texture = woods_texture0;
    }
    else if (mainGame.selectedLocation == "Sandbox_high" || mainGame.selectedLocation == "Sandbox")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = gz_configX;
            currentMap::configY = gz_configY;
            currentMap::configScale = gz_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = gz_orgW;
        map_orgH = gz_orgH;
        texture = gz_texture0;
    }
    else if (mainGame.selectedLocation == "Icebreaker")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = ib_configX;
            currentMap::configY = ib_configY;
            currentMap::configScale = ib_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = static_cast<float>(ib_orgW);
        map_orgH = static_cast<float>(ib_orgH);

        // Icebreaker floor selection by height
        if (height >= ib_texture11_MinHeight)
        {
            texture = ib_texture11;
            
        }
        else if (height >= ib_texture10_MinHeight)
        {
            texture = ib_texture10;
        }
        else if (height >= ib_texture9_MinHeight)
        {
            texture = ib_texture9;
        }
        else if (height >= ib_texture8_MinHeight)
        {
            texture = ib_texture8;
        }
        else if (height >= ib_texture7_MinHeight)
        {
            texture = ib_texture7;
        }
        else if (height >= ib_texture6_MinHeight)
        {
            texture = ib_texture6;
        }
        else if (height >= ib_texture5_MinHeight)
        {
            texture = ib_texture5;
        }
        else if (height >= ib_texture4_MinHeight)
        {
            texture = ib_texture4;
        }
        else if (height >= ib_texture3_MinHeight)
        {
            texture = ib_texture3;
        }
        else if (height >= ib_texture2_MinHeight)
        {
            texture = ib_texture2;
        }
        else
        {
            texture = ib_texture1;
        }
    }
    else
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = customs_configX;
            currentMap::configY = customs_configY;
            currentMap::configScale = customs_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = customs_orgW;
        map_orgH = customs_orgH;
        texture = NULL;
    }

    if (!texture || map_orgW <= 0.0f || map_orgH <= 0.0f)
        return;

    mapControl.Update(ImVec2(map_orgW, map_orgH));
    mapControl.RenderImage(texture, mapGlobals::focusPoint, mapGlobals::followLocal);
}

void CustomChildWindowWithTitle(const char* title, const ImVec2& size) {
    // Get current ImGui window and draw list
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Calculate positions and sizes
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float title_height = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;

    // Draw custom border around the child window
    ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Border);
    draw_list->AddRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + size.x, pos.y + size.y), border_color);

    // Draw the title background to cover the border behind the title
    ImVec2 title_pos = ImVec2(pos.x + ImGui::GetStyle().FramePadding.x, pos.y - title_height / 2);
    ImVec2 title_size = ImGui::CalcTextSize(title);
    draw_list->AddRectFilled(title_pos, ImVec2(title_pos.x + title_size.x, title_pos.y + title_height), ImGui::GetColorU32(ImGuiCol_WindowBg));

    // Draw the title background
    ImGui::SetCursorScreenPos(ImVec2(pos.x + 7, pos.y + 6 - title_height / 2));
    ImGui::Text("%s", title);

}
char filterName[128] = "";
void ShowAddFilterPopup(bool* open) {
    if (*open) {
        ImGui::OpenPopup("Add Filter");
        *open = false;
    }

    if (ImGui::BeginPopupModal("Add Filter", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {

        //default values
        static bool filterActive = false;
        static ImVec4 filterColour = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

        ImGui::InputText("Filter Name", filterName, IM_ARRAYSIZE(filterName));


        if (ImGui::Button("Add", ImVec2(90, 29))) {
            // Generate a new ID
            long newId = lootFilters.empty() ? 1 : lootFilters.back().id + 1;
            // Add new filter to the vector
            lootFilters.push_back({ newId, filterActive, filterName, glm::vec4(filterColour.x, filterColour.y, filterColour.z, filterColour.w) });
            //save updated json!
            configManager.SaveLootFilterConfig();

            // Close the popup
            filterName[0] = '\0';

            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 29))) {
            // Close the popup without adding
            filterName[0] = '\0';
            ImGui::CloseCurrentPopup();


        }

        ImGui::EndPopup();
    }
}

// Function to compare items for sorting
bool CompareGameItems(const gameItemList& a, const gameItemList& b, ImGuiTableSortSpecs* sortSpecs) {
    for (int n = 0; n < sortSpecs->SpecsCount; n++) {
        const ImGuiTableColumnSortSpecs* sortSpec = &sortSpecs->Specs[n];
        int delta = 0;
        switch (sortSpec->ColumnIndex) {
        case 0: delta = (a.name < b.name) ? -1 : (a.name > b.name) ? 1 : 0; break;
        case 1: delta = (a.traderPrice < b.traderPrice) ? -1 : (a.traderPrice > b.traderPrice) ? 1 : 0; break;
        case 2: delta = (a.marketPrice < b.marketPrice) ? -1 : (a.marketPrice > b.marketPrice) ? 1 : 0; break;
        }
        if (delta != 0)
            return (sortSpec->SortDirection == ImGuiSortDirection_Ascending) ? delta < 0 : delta > 0;
    }
    return false;
}

// Convert a string to lowercase
std::string ToLower(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

// Function to format the price
std::string FormatPrice(long price) {
    std::ostringstream oss;
    if (price >= 1000000) {
        oss << std::fixed << std::setprecision(2) << static_cast<float>(price) / 1000000 << "m";
    }
    else if (price >= 1000) {
        oss << price / 1000 << "k";
    }
    else {
        oss << price;
    }
    return oss.str();
}

bool IsItemInAnyLootFilters(const std::vector<LootFilters>& lootFilters, const std::string& bsgid) {
    for (const auto& filter : lootFilters) {
        if (std::any_of(filter.lootItems.begin(), filter.lootItems.end(),
            [&bsgid](const lootFilterItems& item) { return item.bsgid == bsgid; })) {
            return true;
        }
    }
    return false;
}

std::string searchQuery;
void ShowGameItemListTable(LootFilters& currentLootFilter) {
    ImVec2 tableSize = ImVec2(586.f, 600.0f); // Set the desired size for the table

    if (ImGui::BeginTable("GameItemListTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, tableSize)) {
        // Set up the table columns
        ImGui::TableSetupColumn("Item Name", ImGuiTableColumnFlags_WidthFixed, 370.f);
        ImGui::TableSetupColumn("Trader", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Market", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Add", ImGuiTableColumnFlags_WidthFixed, 50.f);
        ImGui::TableHeadersRow();

        // Handle sorting
        ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs();
        if (sortSpecs && sortSpecs->SpecsDirty) {
            std::sort(marketList.begin(), marketList.end(),
                [sortSpecs](const gameItemList& a, const gameItemList& b) { return CompareGameItems(a, b, sortSpecs); });
            sortSpecs->SpecsDirty = false;
        }


        // Iterate through the gameItems vector and display each entry
        std::string lowerSearchQuery = ToLower(searchQuery);
        for (size_t i = 0; i < marketList.size(); ++i) {
            std::string lowerItemName = ToLower(marketList[i].name);
            if (lowerSearchQuery.empty() || lowerItemName.find(lowerSearchQuery) != std::string::npos) {
                ImGui::TableNextRow();

                // Name column
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", marketList[i].name.c_str());

                // Trader Price column
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", FormatPrice(marketList[i].traderPrice).c_str());

                // Market Price column
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", FormatPrice(marketList[i].marketPrice).c_str());

                // Add button column
                ImGui::TableSetColumnIndex(3);

                if (!IsItemInAnyLootFilters(lootFilters, marketList[i].bsgid)) {
                    //if (ImGui::Button(("+##" + marketList[i].bsgid).c_str(),ImVec2(20,20))) {
                    if (ImGui::Selectable((" + ##" + marketList[i].bsgid).c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        // Create a new lootFilterItems entry and populate it with data from gameItems[i]
                        lootFilterItems newItem;
                        newItem.bsgid = marketList[i].bsgid;
                        newItem.name = marketList[i].name;
                        newItem.shortName = marketList[i].shortName;
                        newItem.traderPrice = marketList[i].traderPrice;
                        newItem.marketPrice = marketList[i].marketPrice;

                        // Add the new item to the currentLootFilter's lootItems vector
                        currentLootFilter.lootItems.push_back(newItem);
                        configManager.SaveLootFilterConfig();
                    }
                }


            }
        }

        ImGui::EndTable();
    }
}

// Function to show the add loot popup
char searchBuffer[128] = "";
void ShowAddLootPopup(bool* open, LootFilters& currentLootFilter) {
    if (*open) {
        ImGui::OpenPopup("Add Loot");
        *open = false;
    }

    // Set the next window size to a fixed size
    ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_Always);
    bool isOpen = true;

    if (ImGui::BeginPopupModal("Add Loot", &isOpen, ImGuiWindowFlags_NoResize)) {
        if (!isOpen) {
            searchBuffer[0] = '\0';
            searchQuery.clear();
            ImGui::CloseCurrentPopup();


        }
        // Text input for search

        if (ImGui::InputText("Search", searchBuffer, IM_ARRAYSIZE(searchBuffer))) {
            searchQuery = searchBuffer;
        }

        ImGui::SetCursorPos(ImVec2(5, 80));
        ShowGameItemListTable(currentLootFilter);

        ImGui::EndPopup();
    }

}

//helpers for render
static void DrawLootListInfoTooltip(const LootList& loot)
{
    ImGui::BeginTooltip();

    ImGui::Text("m_itemObject: 0x%llX", loot.m_itemObject);
    ImGui::Text("m_interactiveClass: 0x%llX", loot.m_interactiveClass);
    ImGui::Text("m_baseObject: 0x%llX", loot.m_baseObject);
    ImGui::Text("m_gameObject: 0x%llX", loot.m_gameObject);
    ImGui::Text("m_pGameObjectName: 0x%llX", loot.m_pGameObjectName);
    ImGui::Text("m_objectClassName: %s", loot.m_objectClassName.c_str());
    ImGui::Text("m_objectClass: 0x%llX", loot.m_objectClass);
    ImGui::Text("m_pointerToTransform1: 0x%llX", loot.m_pointerToTransform1);
    ImGui::Text("m_pointerToTransform2: 0x%llX", loot.m_pointerToTransform2);

    ImGui::Separator();

    ImGui::Text("worldLocation: %.2f, %.2f, %.2f",
        loot.worldLocation.x, loot.worldLocation.y, loot.worldLocation.z);

    ImGui::Text("gameObjectName: %s", loot.gameObjectName.c_str());
    ImGui::Text("bsgId: %s", loot.bsgId.c_str());
    ImGui::Text("longName: %s", loot.longName.c_str());
    ImGui::Text("shortName: %s", loot.shortName.c_str());

    ImGui::Separator();

    ImGui::Text("avgMarketPrice: %d", loot.avgMarketPrice);
    ImGui::Text("traderPrice: %d", loot.traderPrice);
    ImGui::Text("corpseValue: %d", loot.corpseValue);

    ImGui::Separator();

    ImGui::Text("isItem: %s", loot.isItem ? "true" : "false");
    ImGui::Text("isContainer: %s", loot.isContainer ? "true" : "false");
    ImGui::Text("isQuestItem: %s", loot.isQuestItem ? "true" : "false");
    ImGui::Text("isCorpse: %s", loot.isCorpse ? "true" : "false");
    ImGui::Text("wanted: %s", loot.wanted ? "true" : "false");
    ImGui::Text("forceWanted: %s", loot.forceWanted ? "true" : "false");

    ImGui::Text("color: %.2f, %.2f, %.2f, %.2f",
        loot.color.x, loot.color.y, loot.color.z, loot.color.w);

    ImGui::EndTooltip();
}

static void BuildLootListDebugRows(
    std::vector<LootList>& lootCache,
    std::vector<LootList*>& normalLootRows,
    std::vector<LootList*>& questLootRows,
    std::vector<LootList*>& wantedRows)
{
    normalLootRows.clear();
    questLootRows.clear();
    wantedRows.clear();

    normalLootRows.reserve(lootCache.size());
    questLootRows.reserve(lootCache.size());
    wantedRows.reserve(lootCache.size());

    for (auto& loot : lootCache)
    {
        if (loot.isContainer)
            continue;

        if (loot.isCorpse)
            continue;

        if (loot.isQuestItem)
        {
            questLootRows.push_back(&loot);

            if (loot.wanted || loot.forceWanted)
                wantedRows.push_back(&loot);

            continue;
        }

        if (loot.isItem)
        {
            normalLootRows.push_back(&loot);

            if (loot.wanted || loot.forceWanted)
                wantedRows.push_back(&loot);
        }
    }
}

static void DrawLootListDebugTable(std::vector<LootList*>& rows, const char* tableId)
{
    if (!ImGui::BeginTable(tableId, 4,
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable))
    {
        return;
    }

    ImGui::TableSetupColumn("Wanted", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Short Name", ImGuiTableColumnFlags_WidthStretch, 220.0f);
    ImGui::TableSetupColumn("BSG ID", ImGuiTableColumnFlags_WidthStretch, 260.0f);
    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthFixed, 30.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        LootList* loot = rows[i];
        if (!loot)
            continue;

        ImGui::TableNextRow();
        ImGui::PushID(loot);

        // Wanted toggle
        ImGui::TableSetColumnIndex(0);
        bool wantedTick = (loot->wanted || loot->forceWanted);
        if (ImGui::Checkbox("##wanted", &wantedTick))
        {
            loot->wanted = wantedTick;
            loot->forceWanted = wantedTick;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Toggle wanted and forceWanted");
        }

        // Short Name
        ImGui::TableSetColumnIndex(1);
        const std::string shortName = TrimEFT(loot->shortName);
        const std::string longName = TrimEFT(loot->longName);

        ImGui::TextUnformatted(shortName.c_str());

        if (ImGui::IsItemHovered() && !longName.empty())
        {
            ImGui::SetTooltip("%s", longName.c_str());
        }

        // BSG ID
        ImGui::TableSetColumnIndex(2);
        const std::string bsgId = TrimEFT(loot->bsgId);
        ImGui::TextUnformatted(bsgId.c_str());

        // Info
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted("(i)");
        if (ImGui::IsItemHovered())
        {
            DrawLootListInfoTooltip(*loot);
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

// Variable to hold the selected ID
//static LootFilters currentLootFilter;
static long selectedLootFilterID = -1;
LootFilters* currentLootFilter = nullptr;

static void renderLootFiltersMenu()
{



    std::string windowNameMain = "Loot Filters";

    static ImGuiWindowFlags flagss = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2((viewport->Pos.x + viewport->Size.x) - 810, viewport->Pos.y + 10));

    ImGui::SetNextWindowSize(ImVec2(750, viewport->Size.y - 50));
    //ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    static bool addFilterPopupOpen = false;
    static bool addLootPopupOpen = false;



    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::appLootFilters, flagss))
    {
        //left top
        {
            //set position
            ImGui::SetCursorPos(ImVec2(10, 45));

            // draw window frame
            CustomChildWindowWithTitle(" Loot Filter Settings ", ImVec2(250, 300));

            //draw inside window
            ImGui::SetCursorPos(ImVec2(20, 60));

            if (ImGui::Checkbox(" Show Quest Loot", &lootGlobals::enableQuestLoot)) configManager.SaveConfig();
            ImGui::SameLine(); ImGui::SetCursorPosX(200);
            if (ImGui::ColorEdit4("##questcolour", (float*)&coloursGlobals::questColour, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
            
            ImGui::SetCursorPosX(20);
            
            if (ImGui::Checkbox(" Show WishList Loot", &lootGlobals::enableWishListLoot)) configManager.SaveConfig();
            ImGui::SameLine(); ImGui::SetCursorPosX(200);
            if (ImGui::ColorEdit4("##wishlistcolour", (float*)&coloursGlobals::wishListColour, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();

            ImGui::SetCursorPosX(20);

            if (ImGui::Checkbox(" Show Value Loot", &lootGlobals::enableValueLoot)) configManager.SaveConfig();
            ImGui::SameLine(); ImGui::SetCursorPosX(200);
            if (ImGui::ColorEdit4("##valuelistcolour", (float*)&coloursGlobals::valueLootColour, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
            ImGui::SetCursorPosX(20);
            ImGui::PushItemWidth(150);
            if (ImGui::SliderInt("R (over)", &lootGlobals::valueLootFrom, 0, 1000000, "%d")) configManager.SaveConfig();
            ImGui::PopItemWidth();
            

            ImGui::SetCursorPos(ImVec2(20, 200));
            static bool containerPopupOpen = false;

            if (ImGui::Button("Container Options"))
            {
                containerPopupOpen = true;
                ImGui::OpenPopup("Container Settings");
            }

            if (ImGui::BeginPopupModal("Container Settings", &containerPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(520, 600));

                struct ContainerOption
                {
                    const char* label;
                    bool* value;
                };

                ContainerOption options[] =
                {
                    { "Drawer",        &Loot.drawDrawer },
                    { "Duffle",        &Loot.drawDuffle },
                    { "Safe",          &Loot.drawSafe },
                    { "Weapon Box",    &Loot.drawWeaponBox },
                    { "Tech Crate",    &Loot.drawTechCrate },
                    { "Ration Crate",  &Loot.drawRationCrate },
                    { "Medical Crate", &Loot.drawMedicalCrate },
                    { "Jacket",        &Loot.drawJacket },
                    { "Med Package",   &Loot.drawMedPackage },
                    { "Med Box",       &Loot.drawMedBox },
                    { "Toolbox",       &Loot.drawToolbox },
                    { "Grenade Box",   &Loot.drawGrenadeBox },
                    { "Buried Stash",  &Loot.drawBuriedStash },
                    { "Ground Cache",  &Loot.drawGroundCache },
                    { "Wooden Crate",  &Loot.drawWoodenCrate },
                    { "Suitcase",      &Loot.drawSuitcase },
                    { "Ammo Box",      &Loot.drawAmmoBox },
                    { "Dead Body",     &Loot.drawDeadBody },
                    { "PC Block",      &Loot.drawPCBlock },
                    { "Register",      &Loot.drawRegister },
                    { "Airdrop",       &Loot.drawAirDrops },
                    // { "Xmas Loot",   &Loot.drawXmas },
                };

                const int optionCount = sizeof(options) / sizeof(options[0]);
                const int splitIndex = (optionCount + 1) / 2;

                ImGui::Text("Container Settings");
                ImGui::Separator();

                
                if (ImGui::Button("Disable All", ImVec2(120, 0)))
                {
                    for (int i = 0; i < optionCount; i++)
                        *options[i].value = false;
                }

                ImGui::Separator();

                if (ImGui::BeginChild("##container_settings_child", ImVec2(0, -45), true))
                {
                    if (ImGui::BeginTable("##container_settings_table", 2, ImGuiTableFlags_BordersInnerV))
                    {
                        ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        for (int i = 0; i < splitIndex; i++)
                        {
                            ImGui::Checkbox(options[i].label, options[i].value);
                        }

                        ImGui::TableSetColumnIndex(1);
                        for (int i = splitIndex; i < optionCount; i++)
                        {
                            ImGui::Checkbox(options[i].label, options[i].value);
                        }

                        ImGui::EndTable();
                    }

                    ImGui::EndChild();
                }

               

                ImGui::EndPopup();
            }


            ImGui::SetCursorPosX(20);
            //wishlist items
            static bool wishListPopupOpen = false;
            if (ImGui::Button("Wishlist Items"))
            {
                wishListPopupOpen = true;
                ImGui::OpenPopup("WishList");
            }

            if (ImGui::BeginPopupModal("WishList", &wishListPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(700, 400));

                ImGui::Text("Wishlist Items");
                ImGui::Separator();

                if (wishListData.empty())
                {
                    ImGui::TextDisabled("Wishlist data empty.. In raid?");
                }
                else
                {
                    if (ImGui::BeginChild("##wishlist_child", ImVec2(0, -40), true))
                    {
                        if (ImGui::BeginTable("##wishlist_table", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY))
                        {
                            ImGui::TableSetupColumn("BSG ID", ImGuiTableColumnFlags_WidthStretch, 0.55f);
                            ImGui::TableSetupColumn("Short Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                            ImGui::TableHeadersRow();

                            for (const auto& wishList : wishListData)
                            {
                                ImGui::TableNextRow();

                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(wishList.bsgId.c_str());

                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextUnformatted(wishList.shortName.c_str());
                            }

                            ImGui::EndTable();
                        }
                        ImGui::EndChild();
                    }
                }


                ImGui::EndPopup();
            }

            //questloot items etc
            ImGui::SetCursorPosX(20);
            
            static bool questLootPopupOpen = false;

            if (ImGui::Button("Quest Loot"))
            {
                questLootPopupOpen = true;
                ImGui::OpenPopup("Quest Loot Manager");
            }

            if (ImGui::BeginPopupModal("Quest Loot Manager", &questLootPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(900, 500));

                std::vector<LootList> cacheLoot = Loot.getCacheLoot();

                // ---------------------------------------------------------
                // Search state for Tab 1
                // ---------------------------------------------------------
                static char questLootSearch[128] = "";
                std::string searchText = TrimEFT(std::string(questLootSearch));

                auto containsInsensitive = [](const std::string& text, const std::string& search) -> bool
                    {
                        if (search.empty())
                            return true;

                        std::string a = text;
                        std::string b = search;

                        std::transform(a.begin(), a.end(), a.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                        std::transform(b.begin(), b.end(), b.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                        return a.find(b) != std::string::npos;
                    };

                // ---------------------------------------------------------
                // Lookup for current raid loot (Tab 1)
                // ---------------------------------------------------------
                std::unordered_map<std::string, LootList*> lootById;
                lootById.reserve(cacheLoot.size());

                for (auto& loot : cacheLoot)
                {
                    std::string id = TrimEFT(loot.bsgId);
                    if (id.empty())
                        continue;

                    lootById[id] = &loot;
                }

                // ---------------------------------------------------------
                // Lookup for full market item database (Tab 2)
                // ---------------------------------------------------------
                std::unordered_map<std::string, const gameItemList*> marketById;
                marketById.reserve(marketList.size());

                for (const auto& item : marketList)
                {
                    std::string id = TrimEFT(item.bsgid);
                    if (id.empty())
                        continue;

                    marketById[id] = &item;
                }

                ImGui::Text("Quest Loot Manager");
                ImGui::Separator();

                if (ImGui::BeginTabBar("##QuestLootTabs"))
                {
                    // =========================================================
                    // TAB 1 - CURRENT RAID QUEST LOOT
                    // =========================================================
                    if (ImGui::BeginTabItem("Current Raid Quest Loot"))
                    {
                        if (ImGui::BeginChild("##CurrentRaidLootChild", ImVec2(0, -40), true))
                        {
                            ImGui::SetNextItemWidth(300.0f);
                            ImGui::InputTextWithHint("##QuestLootSearch", "Search name or BSG ID...", questLootSearch, IM_ARRAYSIZE(questLootSearch));
                            ImGui::Separator();

                            int visibleRows = 0;

                            for (const auto& loot : cacheLoot)
                            {
                                if (!loot.isQuestItem)
                                    continue;

                                std::string shortName = TrimEFT(loot.shortName);
                                std::string bsgId = TrimEFT(loot.bsgId);

                                if (!containsInsensitive(shortName, searchText) &&
                                    !containsInsensitive(bsgId, searchText))
                                {
                                    continue;
                                }

                                visibleRows++;
                            }

                            if (visibleRows == 0)
                            {
                                ImGui::TextDisabled("No matching quest loot found in current raid.");
                            }
                            else if (ImGui::BeginTable("##CurrentRaidLootTable", 3,
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Borders |
                                ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable))
                            {
                                ImGui::TableSetupColumn("Wanted", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                ImGui::TableSetupColumn("BSG ID", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableHeadersRow();

                                int row = 0;
                                for (auto& loot : cacheLoot)
                                {
                                    if (!loot.isQuestItem)
                                        continue;

                                    std::string shortName = TrimEFT(loot.shortName);
                                    std::string bsgId = TrimEFT(loot.bsgId);

                                    if (!containsInsensitive(shortName, searchText) &&
                                        !containsInsensitive(bsgId, searchText))
                                    {
                                        continue;
                                    }

                                    ImGui::PushID(row++);

                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    bool wanted = loot.wanted;
                                    if (ImGui::Checkbox("##wanted", &wanted))
                                    {
                                        loot.wanted = wanted;

                                        if (loot.wanted)
                                            loot.color = coloursGlobals::questColour;
                                    }

                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(loot.shortName.c_str());

                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::TextUnformatted(bsgId.c_str());

                                    ImGui::PopID();
                                }

                                ImGui::EndTable();
                            }
                        }

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    // =========================================================
                    // TAB 2 - QUEST ITEMS FROM MASTER ITEM DATABASE
                    // =========================================================
                    if (ImGui::BeginTabItem("Quest Items"))
                    {
                        if (ImGui::BeginChild("##MasterItemsChild", ImVec2(0, -40), true))
                        {
                            std::unordered_set<std::string> seenMasterIds;
                            seenMasterIds.reserve(masterItems.size());

                            int visibleRows = 0;
                            for (const auto& rawMasterId : masterItems)
                            {
                                std::string masterId = TrimEFT(rawMasterId);
                                if (masterId.empty())
                                    continue;

                                if (!seenMasterIds.insert(masterId).second)
                                    continue;

                                visibleRows++;
                            }

                            if (visibleRows == 0)
                            {
                                ImGui::TextDisabled("Task Quest Items list is empty.");
                            }
                            else if (ImGui::BeginTable("##MasterItemsTable", 2,
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Borders |
                                ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_Resizable))
                            {
                                ImGui::TableSetupColumn("Short Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                                ImGui::TableSetupColumn("BSG ID", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                                ImGui::TableHeadersRow();

                                seenMasterIds.clear();

                                for (const auto& rawMasterId : masterItems)
                                {
                                    std::string masterId = TrimEFT(rawMasterId);
                                    if (masterId.empty())
                                        continue;

                                    if (!seenMasterIds.insert(masterId).second)
                                        continue;

                                    std::string shortName = "Unknown";

                                    auto it = marketById.find(masterId);
                                    if (it != marketById.end() && it->second)
                                    {
                                        shortName = TrimEFT(it->second->shortName); // adjust if your field name differs
                                        if (shortName.empty())
                                            shortName = "Unknown";
                                    }

                                    ImGui::PushID(masterId.c_str());

                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextUnformatted(shortName.c_str());

                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(masterId.c_str());

                                    ImGui::PopID();
                                }

                                ImGui::EndTable();
                            }
                        }

                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }

                ImGui::EndPopup();
            }

            ImGui::SetCursorPosX(20);
            //fullLootListPopupOpen items
            static bool fullLootListPopupOpen = false;
            if (ImGui::Button("Debug LootList"))
            {
                fullLootListPopupOpen = true;
                ImGui::OpenPopup("lootList");
            }

            std::vector<LootList> lootCache = Loot.getCacheLoot();

            if (ImGui::BeginPopupModal("lootList", &fullLootListPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(950, 500), ImGuiCond_Always);

                static std::vector<LootList*> normalLootRows;
                static std::vector<LootList*> questLootRows;
                static std::vector<LootList*> wantedRows;

                BuildLootListDebugRows(lootCache, normalLootRows, questLootRows, wantedRows);

                if (ImGui::BeginTabBar("##lootListTabs"))
                {
                    if (ImGui::BeginTabItem("Loot"))
                    {
                        ImGui::Separator();
                        DrawLootListDebugTable(normalLootRows, "##lootListNormalTable");
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Quest Items"))
                    {
                        ImGui::Separator();
                        DrawLootListDebugTable(questLootRows, "##lootListQuestTable");
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Wanted"))
                    {
                        ImGui::Separator();
                        DrawLootListDebugTable(wantedRows, "##lootListWantedTable");
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }

                ImGui::Spacing();
                ImGui::Separator();

                if (ImGui::Button("Close", ImVec2(120, 0)))
                {
                    fullLootListPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }

        }

        //right top
        {
            // Set position
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - 480, 45));

            // Draw window frame
            CustomChildWindowWithTitle(" Loot Filters ", ImVec2(470, 300));

            // Draw inside window
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - 110, 55)); // Top right corner

            // Button to open the add filter popup
            if (ImGui::Button("+ Add Filter", ImVec2(90, 29))) {
                addFilterPopupOpen = true;
            }

            // Position for filter list
            ImVec2 tableSize = ImVec2(450.0f, 250.0f); // Set the desired size for the table

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - 470, 90));

            if (ImGui::BeginTable("##LootFiltersTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, tableSize)) {
                // Set up the table headers
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.f);
                ImGui::TableSetupColumn("Filter Name", ImGuiTableColumnFlags_WidthFixed, 320.f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.f);


                // Iterate through the lootFilters vector and display each entry
                for (size_t i = 0; i < lootFilters.size(); ++i) {
                    ImGui::TableNextRow();

                    // Make the row selectable and check if it is clicked
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(i); // Ensure unique ID for each row
                    bool isSelected = (selectedLootFilterID == lootFilters[i].id);
                    if (ImGui::Selectable(("##row" + std::to_string(i)).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                        selectedLootFilterID = lootFilters[i].id;
                    }
                    ImGui::PopID();

                    // Active column
                    ImGui::TableSetColumnIndex(1);
                    if (ImGui::Checkbox(("##Active" + std::to_string(lootFilters[i].id)).c_str(), &lootFilters[i].active))
                    {
                        configManager.SaveLootFilterConfig();
                    }


                    // Filter colour column
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::ColorEdit4(("##Color" + std::to_string(lootFilters[i].id)).c_str(), (float*)&lootFilters[i].filterColour, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs))
                    {
                        configManager.SaveLootFilterConfig();
                    }


                    // Filter Name column
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", lootFilters[i].filterName.c_str());

                    // Delete button column
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::Button(("X##" + std::to_string(lootFilters[i].id)).c_str())) {
                        // Delete the current row
                        lootFilters.erase(lootFilters.begin() + i);
                        // Adjust the index after deletion to avoid skipping an entry
                        --i;
                        configManager.SaveLootFilterConfig();
                    }

                }

                ImGui::EndTable();
            }

            ImGui::PopStyleVar(2);



        }

        //bottom
        {
            //set position
            ImGui::SetCursorPos(ImVec2(10, 360));

            // draw window frame
            CustomChildWindowWithTitle(" Loot Filter Items ", ImVec2(ImGui::GetWindowSize().x - 20, ImGui::GetWindowSize().y - 365));

            if (selectedLootFilterID != -1)
            {
                
                ImGui::SetCursorPos(ImVec2(ImGui::GetWindowSize().x - 110, 370)); // top right corner

                if (ImGui::Button("+ Add Loot", ImVec2(90, 29))) {
                    addLootPopupOpen = true;
                }

                //position for table
                ImGui::SetCursorPos(ImVec2(20, 410)); // top right corner

                ImVec2 tableSize = ImVec2(ImGui::GetWindowSize().x - 40, (ImGui::GetWindowSize().y - ImGui::GetCursorPosY()) - 15); // Set the desired size for the table

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

                if (ImGui::BeginTable("##LootInFilter", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, tableSize)) {
                    // Set up the table columns
                    ImGui::TableSetupColumn("Item Name", ImGuiTableColumnFlags_WidthFixed, 470.f);
                    ImGui::TableSetupColumn("Trader", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("Market", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 50.f);
                    ImGui::TableHeadersRow();

                    //get loot items from selected filter
                    for (auto& lootFilter : lootFilters)
                    {
                        if (lootFilter.id == selectedLootFilterID)
                        {


                            // Iterate through the lootFilterItems
                            for (size_t i = 0; i < lootFilter.lootItems.size(); ++i) {
                                ImGui::TableNextRow();

                                // Name column
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%s", lootFilter.lootItems[i].name.c_str());

                                // Trader Price column
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%s", FormatPrice(lootFilter.lootItems[i].traderPrice).c_str());

                                // Market Price column
                                ImGui::TableSetColumnIndex(2);
                                ImGui::Text("%s", FormatPrice(lootFilter.lootItems[i].marketPrice).c_str());

                                // Delete button column
                                ImGui::TableSetColumnIndex(3);
                                if (ImGui::Button(("X##" + lootFilter.lootItems[i].bsgid).c_str())) {
                                    // Delete the current row
                                    lootFilter.lootItems.erase(lootFilter.lootItems.begin() + i);
                                    // Adjust the index after deletion to avoid skipping an entry
                                    --i;
                                    configManager.SaveLootFilterConfig();
                                }
                            }

                            break;
                        }
                    }






                    ImGui::EndTable();
                }

                ImGui::PopStyleVar(2);
            }
        }

    }
    ImGui::End();

    // Get the currentLootFilter reference based on selectedLootFilterID
    for (auto& filter : lootFilters) {
        if (filter.id == selectedLootFilterID) {
            currentLootFilter = &filter;
            break;
        }
    }

    ShowAddFilterPopup(&addFilterPopupOpen);
    ShowAddLootPopup(&addLootPopupOpen, *currentLootFilter);
}


bool showActive = false; 
const char* options[] = { "All", "Active" };
static int currentSelection = 0; // 0 = All, 1 = Active

static void renderQuestsWindow()
{
    std::string windowNameMain = "Quests Manager";
    static ImGuiWindowFlags flagss = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2((viewport->Pos.x + viewport->Size.x) - 810, viewport->Pos.y + 10));
    ImGui::SetNextWindowSize(ImVec2(750, viewport->Size.y - 50));

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::appQuests, flagss))
    {
        {
            ImGui::SetCursorPos(ImVec2(10, 45));

            if (ImGui::Combo("Filter", &currentSelection, options, IM_ARRAYSIZE(options)))
            {
                showActive = (currentSelection == 1);
            }
        }

        {
            ImGui::SetCursorPos(ImVec2(10, 105));

            CustomChildWindowWithTitle(" Quest Manager ", ImVec2(ImGui::GetWindowSize().x - 20, ImGui::GetWindowSize().y - 20));

            ImGui::SetCursorPos(ImVec2(20, 120));

            ImVec2 tableSize = ImVec2(ImGui::GetWindowSize().x - 40,
                (ImGui::GetWindowSize().y - ImGui::GetCursorPosY()) - 15);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

            if (ImGui::BeginTable("##QuestDatabase", 2,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable,
                tableSize))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 290.f);
                ImGui::TableSetupColumn("Objectives", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                // -------------------------
                // Static (API) renderer
                // -------------------------
                auto RenderTaskRow_Static = [&](const TarkovDevTasks& quest)
                    {
                        ImGui::PushID(quest.qID.c_str());

                        ImGui::TableNextRow();

                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(quest.qName.c_str());
                        if (!quest.qID.empty())
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%s)", quest.qID.c_str());
                        }

                        ImGui::TableSetColumnIndex(1);

                        const int objCount = (int)quest.objectives.size();
                        if (ImGui::TreeNode("##obj_tree", "Show (%d) Objectives", objCount))
                        {
                            for (int i = 0; i < objCount; ++i)
                            {
                                const auto& obj = quest.objectives[i];
                                ImGui::PushID(i);

                                if (!obj.id.empty())
                                    ImGui::BulletText("Type: %s  |  ObjID: %s", obj.type.c_str(), obj.id.c_str());
                                else
                                    ImGui::BulletText("Type: %s", obj.type.c_str());

                                if (!obj.itemId.empty())
                                    ImGui::Text("Item ID: %s", obj.itemId.c_str());

                                if (!obj.questItemId.empty())
                                    ImGui::Text("QuestItem ID: '%s'", obj.questItemId.c_str());

                                if (!obj.maps.empty())
                                {
                                    ImGui::Text("Maps (%d):", (int)obj.maps.size());
                                    ImGui::Indent();
                                    for (int mi = 0; mi < (int)obj.maps.size(); ++mi)
                                        ImGui::BulletText("%s", obj.maps[mi].c_str());
                                    ImGui::Unindent();
                                }

                                if (!obj.zones.empty())
                                {
                                    ImGui::Text("Zones (%d):", (int)obj.zones.size());
                                    ImGui::Indent();
                                    for (int zi = 0; zi < (int)obj.zones.size(); ++zi)
                                    {
                                        const auto& z = obj.zones[zi];
                                        ImGui::PushID(zi);

                                        ImGui::BulletText("Map: %s", z.mapNameId.c_str());
                                        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", z.position.x, z.position.y, z.position.z);

                                        ImGui::PopID();
                                    }
                                    ImGui::Unindent();
                                }

                                ImGui::Separator();
                                ImGui::PopID();
                            }

                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    };

                // -------------------------
                // Active (memory) renderer
                // -------------------------
                auto RenderTaskRow_Active = [&](const QuestData& quest)
                    {
                        ImGui::PushID(quest.questId.c_str());

                        ImGui::TableNextRow();

                        // Column 0: Quest Name (+ optional ID)
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(quest.questName.c_str());
                        if (!quest.questId.empty())
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(%s)", quest.questId.c_str());
                        }

                        // Column 1: Objectives (Tree)
                        ImGui::TableSetColumnIndex(1);

                        const int objCount = (int)quest.objectives.size();
                        const int condCount = (int)quest.completedConditions.size();

                        if (ImGui::TreeNode("##active_obj_tree", "Show (%d) Objectives  |  CompletedConditions (%d)", objCount, condCount))
                        {
                            for (int i = 0; i < objCount; ++i)
                            {
                                const auto& obj = quest.objectives[i];
                                ImGui::PushID(i);

                                // Header line
                                if (!obj.objectiveId.empty())
                                    ImGui::BulletText("Type: %s  |  ObjID: %s", obj.type.c_str(), obj.objectiveId.c_str());
                                else
                                    ImGui::BulletText("Type: %s", obj.type.c_str());

                                // Item / QuestItem IDs
                                if (!obj.itemId.empty())
                                    ImGui::Text("Item ID: %s", obj.itemId.c_str());

                                if (!obj.questItemId.empty())
                                    ImGui::Text("QuestItem ID: %s", obj.questItemId.c_str());

                                // Maps
                                if (!obj.maps.empty())
                                {
                                    ImGui::Text("Maps (%d):", (int)obj.maps.size());
                                    ImGui::Indent();
                                    for (int mi = 0; mi < (int)obj.maps.size(); ++mi)
                                        ImGui::BulletText("%s", obj.maps[mi].c_str());
                                    ImGui::Unindent();
                                }

                                // Zones (positions)
                                if (!obj.zones.empty())
                                {
                                    ImGui::Text("Zones (%d):", (int)obj.zones.size());
                                    ImGui::Indent();
                                    for (int zi = 0; zi < (int)obj.zones.size(); ++zi)
                                    {
                                        const auto& z = obj.zones[zi];
                                        ImGui::PushID(zi);

                                        ImGui::BulletText("Map: %s", z.mapNameId.c_str());
                                        ImGui::Text("Pos: (%.2f, %.2f, %.2f)", z.position.x, z.position.y, z.position.z);

                                        ImGui::PopID();
                                    }
                                    ImGui::Unindent();
                                }

                                ImGui::Separator();
                                ImGui::PopID();
                            }

                            ImGui::TreePop();
                        }

                        ImGui::PopID();
                    };

                if (!showActive)
                {
                    for (const auto& quest : tarkovDevTasksData)
                        RenderTaskRow_Static(quest);
                }
                else
                {
                    for (const auto& quest : questDataActive)
                        RenderTaskRow_Active(quest);
                }

                ImGui::EndTable();
            }

            ImGui::PopStyleVar(2);
        }
    }
    ImGui::End();
}

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return "";

    const int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (needed <= 1)
        return "";

    std::string result(static_cast<size_t>(needed - 1), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        needed,
        nullptr,
        nullptr
    );

    return result;
}

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    const int needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0
    );

    if (needed <= 1)
        return L"";

    std::wstring result(static_cast<size_t>(needed - 1), L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        needed
    );

    return result;
}

static std::string BuildMonitorLabel(const DxMonitorInfo& monitor)
{
    std::string name = WideToUtf8(monitor.name);

    if (name.empty())
        name = WideToUtf8(monitor.deviceName);

    if (name.empty())
        name = "Monitor";

    std::string label =
        name +
        " (" +
        std::to_string(monitor.x) +
        ", " +
        std::to_string(monitor.y) +
        ") " +
        std::to_string(monitor.width) +
        "x" +
        std::to_string(monitor.height) +
        " @" +
        std::to_string(monitor.refreshRate) +
        "Hz";

    if (monitor.primary)
        label += " [Primary]";

    return label;
}

static int GetFontIndexFromName(const std::wstring& fontName)
{
    static const wchar_t* fonts[] =
    {
        L"Arial",
        L"Segoe UI",
        L"Tahoma",
        L"Verdana",
        L"Calibri"
    };

    for (int i = 0; i < IM_ARRAYSIZE(fonts); ++i)
    {
        if (_wcsicmp(fontName.c_str(), fonts[i]) == 0)
            return i;
    }

    return 0;
}

static const wchar_t* GetFontNameFromIndex(int index)
{
    static const wchar_t* fonts[] =
    {
        L"Arial",
        L"Segoe UI",
        L"Tahoma",
        L"Verdana",
        L"Calibri"
    };

    index = std::clamp(index, 0, IM_ARRAYSIZE(fonts) - 1);
    return fonts[index];
}

static const char* GetFontPreviewName(int index)
{
    static const char* fonts[] =
    {
        "Arial",
        "Segoe UI",
        "Tahoma",
        "Verdana",
        "Calibri"
    };

    index = std::clamp(index, 0, IM_ARRAYSIZE(fonts) - 1);
    return fonts[index];
}

static void RestartDxFuserWindow(DxWindowConfig& cfg)
{
    if (g_DxWindow.IsRunning())
        g_DxWindow.Stop();

    g_DxWindow.Init(cfg);
    g_DxWindow.Start();
}

static void ApplyAndSaveFuserConfig(DxWindowConfig& cfg)
{
    cfg.defaultFont.italic = false;

    g_DxWindow.SetConfig(cfg);
    configManager.SaveConfig();
}

static void RestartAndSaveFuserWindow(DxWindowConfig& cfg)
{
    cfg.defaultFont.italic = false;

    g_DxWindow.SetConfig(cfg);
    configManager.SaveConfig();

    RestartDxFuserWindow(cfg);
}

static void renderFuserWindow()
{
    std::string windowNameMain = "Fuser";

    static ImGuiWindowFlags flagss =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2((viewport->Pos.x + viewport->Size.x) - 410, viewport->Pos.y + 10)
    );

    const float windowWidth = 350.0f;
    const float maxWindowHeight = viewport->Size.y - 20.0f;

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(windowWidth, 0.0f),
        ImVec2(windowWidth, maxWindowHeight)
    );

    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    static bool firstLoad = true;

    if (firstLoad)
    {
        g_DxWindow.RefreshMonitorList();
        firstLoad = false;
    }

    DxWindowConfig editorConfig = g_DxWindow.GetConfig();

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::appFuser, flagss))
    {
        if (ImGui::BeginTabBar("##fuserTabs", ImGuiTabBarFlags_FittingPolicyResizeDown))
        {
            // -------------------------------------------------
            // CONTROL TAB
            // -------------------------------------------------
            if (ImGui::BeginTabItem("Control"))
            {
                ImGui::SeparatorText("Fuser Control");

                const bool isRunning = g_DxWindow.IsRunning();

                if (!isRunning)
                {
                    if (ImGui::Button("Launch", ImVec2(140, 30)))
                    {
                        g_DxWindow.Init(editorConfig);
                        g_DxWindow.Start();
                    }
                }
                else
                {
                    if (ImGui::Button("Stop", ImVec2(140, 30)))
                    {
                        g_DxWindow.Stop();
                    }
                }

                ImGui::Spacing();

                ImGui::SeparatorText("Status");

                if (isRunning)
                {
                    ImGui::TextColored(
                        ImVec4(0.2f, 1.0f, 0.2f, 1.0f),
                        "Current State: Running"
                    );
                }
                else
                {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                        "Current State: Stopped"
                    );
                }

                ImGui::EndTabItem();
            }

            // -------------------------------------------------
            // MONITOR TAB
            // -------------------------------------------------
            if (ImGui::BeginTabItem("Monitor"))
            {
                bool changed = false;

                ImGui::SeparatorText("Monitor");

                if (ImGui::Button("Refresh Monitors", ImVec2(160, 26)))
                {
                    g_DxWindow.RefreshMonitorList();
                }

                std::vector<DxMonitorInfo> monitors = g_DxWindow.GetMonitors();

                if (monitors.empty())
                {
                    g_DxWindow.RefreshMonitorList();
                    monitors = g_DxWindow.GetMonitors();
                }

                if (!monitors.empty())
                {
                    if (editorConfig.monitorIndex < 0 ||
                        editorConfig.monitorIndex >= static_cast<int>(monitors.size()))
                    {
                        editorConfig.monitorIndex = 0;
                        changed = true;
                    }

                    std::vector<std::string> monitorLabels;
                    monitorLabels.reserve(monitors.size());

                    for (const auto& monitor : monitors)
                        monitorLabels.push_back(BuildMonitorLabel(monitor));

                    const char* preview =
                        editorConfig.monitorIndex >= 0 &&
                        editorConfig.monitorIndex < static_cast<int>(monitorLabels.size())
                        ? monitorLabels[editorConfig.monitorIndex].c_str()
                        : "Select";

                    ImGui::Text("Selected Monitor");

                    if (ImGui::BeginCombo("##MonitorCombo", preview))
                    {
                        for (size_t i = 0; i < monitorLabels.size(); ++i)
                        {
                            const bool selected = editorConfig.monitorIndex == static_cast<int>(i);

                            if (ImGui::Selectable(monitorLabels[i].c_str(), selected))
                            {
                                editorConfig.monitorIndex = static_cast<int>(i);
                                changed = true;
                            }

                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }

                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextUnformatted("No monitors found");
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Window");

                changed |= ImGui::Checkbox("Auto Start", &editorConfig.autoStart);
                changed |= ImGui::Checkbox("Fullscreen", &editorConfig.fullscreen);

                if (editorConfig.fullscreen)
                {
                    if (!editorConfig.borderless)
                    {
                        editorConfig.borderless = true;
                        changed = true;
                    }

                    if (!editorConfig.useMonitorSize)
                    {
                        editorConfig.useMonitorSize = true;
                        changed = true;
                    }
                }
                else
                {
                    if (editorConfig.useMonitorSize)
                    {
                        editorConfig.useMonitorSize = false;
                        changed = true;
                    }
                }

                changed |= ImGui::Checkbox("Borderless", &editorConfig.borderless);
                changed |= ImGui::Checkbox("Top Most", &editorConfig.topMost);
                changed |= ImGui::Checkbox("Show In Taskbar", &editorConfig.showInTaskbar);

                ImGui::Spacing();
                ImGui::SeparatorText("Background");

                changed |= ImGui::Checkbox("Transparent Background", &editorConfig.transparentBackground);

                if (!editorConfig.transparentBackground)
                {
                    changed |= ImGui::ColorEdit4(
                        "Background",
                        (float*)&editorConfig.backgroundColour,
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs
                    );
                }

                if (changed)
                {
                    ApplyAndSaveFuserConfig(editorConfig);
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Apply");

                if (ImGui::Button("Restart Window", ImVec2(160, 28)))
                {
                    RestartAndSaveFuserWindow(editorConfig);
                }

                ImGui::TextWrapped(
                    "Fullscreen uses the selected monitor size. "
                    "Restart after monitor, window or transparency changes."
                );

                ImGui::EndTabItem();
            }

            // -------------------------------------------------
            // RENDER TAB
            // -------------------------------------------------
            if (ImGui::BeginTabItem("Render"))
            {
                bool changed = false;

                ImGui::SeparatorText("Frame Timing");

                changed |= ImGui::Checkbox("Use VSync", &editorConfig.useVSync);
                changed |= ImGui::Checkbox("Use Monitor Refresh", &editorConfig.useMonitorRefreshRate);

                if (!editorConfig.useVSync && !editorConfig.useMonitorRefreshRate)
                {
                    changed |= ImGui::SliderInt("Max FPS", &editorConfig.maxFPS, 30, 360);
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Scale / Quality");

                changed |= ImGui::Checkbox("Anti Aliasing", &editorConfig.antiAliasing);
                changed |= ImGui::Checkbox("Use DPI Scale", &editorConfig.useDpiScale);

                changed |= ImGui::SliderFloat(
                    "Render Scale",
                    &editorConfig.renderScale,
                    0.50f,
                    2.50f,
                    "%.2f"
                );

                editorConfig.renderScale = std::clamp(editorConfig.renderScale, 0.05f, 5.0f);

                ImGui::Spacing();
                ImGui::SeparatorText("Font");

                int selectedFontIndex = GetFontIndexFromName(editorConfig.defaultFont.name);

                if (ImGui::BeginCombo("Font", GetFontPreviewName(selectedFontIndex)))
                {
                    for (int i = 0; i < 5; ++i)
                    {
                        const bool selected = selectedFontIndex == i;

                        if (ImGui::Selectable(GetFontPreviewName(i), selected))
                        {
                            selectedFontIndex = i;
                            editorConfig.defaultFont.name = GetFontNameFromIndex(i);
                            changed = true;
                        }

                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }

                    ImGui::EndCombo();
                }

                changed |= ImGui::Checkbox("Bold", &editorConfig.defaultFont.bold);

                if (changed)
                {
                    ApplyAndSaveFuserConfig(editorConfig);
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Apply");

                if (ImGui::Button("Apply Live", ImVec2(140, 28)))
                {
                    ApplyAndSaveFuserConfig(editorConfig);
                }

                ImGui::SameLine();

                if (ImGui::Button("Restart Window", ImVec2(150, 28)))
                {
                    RestartAndSaveFuserWindow(editorConfig);
                }

                ImGui::TextWrapped(
                    "Use Render Scale to control text and drawing size."
                );

                ImGui::EndTabItem();
            }

            // -------------------------------------------------
            // DEBUG TAB
            // -------------------------------------------------
            if (ImGui::BeginTabItem("Debug"))
            {
                ImGui::SeparatorText("Debug Rendering");

                bool testSceneEnabled = fuserRender::IsTestSceneEnabled();

                if (ImGui::Checkbox("Render Test Scene", &testSceneEnabled))
                {
                    fuserRender::SetTestSceneEnabled(testSceneEnabled);

                }

                ImGui::TextWrapped(
                    "Shows moving boxes, text, circles, lines, markers and FPS on the fuser window."
                );

                ImGui::Spacing();
                ImGui::SeparatorText("Selected Monitor Details");

                std::vector<DxMonitorInfo> monitors = g_DxWindow.GetMonitors();

                if (monitors.empty())
                {
                    ImGui::TextUnformatted("No monitor data loaded");
                }
                else if (editorConfig.monitorIndex < 0 ||
                    editorConfig.monitorIndex >= static_cast<int>(monitors.size()))
                {
                    ImGui::TextUnformatted("Invalid selected monitor index");
                }
                else
                {
                    const DxMonitorInfo& selectedMonitor = monitors[editorConfig.monitorIndex];

                    ImGui::Text("Name: %s", WideToUtf8(selectedMonitor.name).c_str());
                    ImGui::Text("Device: %s", WideToUtf8(selectedMonitor.deviceName).c_str());
                    ImGui::Text("Position: %d, %d", selectedMonitor.x, selectedMonitor.y);
                    ImGui::Text("Size: %d x %d", selectedMonitor.width, selectedMonitor.height);
                    ImGui::Text("Refresh: %d Hz", selectedMonitor.refreshRate);
                    ImGui::Text("Primary: %s", selectedMonitor.primary ? "Yes" : "No");
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Runtime");

                ImGui::Text("Window Ready: %s", g_DxWindow.IsWindowReady() ? "Yes" : "No");
                ImGui::Text("Window Size: %d x %d", g_DxWindow.GetWindowWidth(), g_DxWindow.GetWindowHeight());
                ImGui::Text("Final Scale: %.2f", g_DxWindow.GetFinalRenderScale());
                ImGui::Text("HWND: %s", g_DxWindow.GetHWND() ? "Valid" : "None");

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}

static void renderMenuSettings()
{


    std::string windowNameMain = "App Settings";

    static ImGuiWindowFlags flagss = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2((viewport->Pos.x + viewport->Size.x) - 410, viewport->Pos.y + 10));

    ImGui::SetNextWindowSize(ImVec2(350, viewport->Size.y - 50));
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);



    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::appSettings, flagss))
    {

        if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_FittingPolicyResizeDown))
        {
            if (ImGui::BeginTabItem("App"))
            {


                ImGui::SeparatorText("Radar Control");
                const bool dmaConnected = memoryGlobals::dmaConnected.load(
                    std::memory_order_acquire
                );

                const bool processFound = memoryGlobals::processFound.load(
                    std::memory_order_acquire
                );

                const bool working = mem.IsInitRunning();
                const bool stopping = mem.IsDisconnectRequested();

                if (working)
                {
                    ImGui::BeginDisabled();

                    ImGui::Button(
                        stopping ? "Stopping..." : "Working...",
                        ImVec2(140, 30)
                    );

                    ImGui::EndDisabled();

                    ImGui::SameLine();

                    if (!stopping && ImGui::Button("Disconnect", ImVec2(140, 30)))
                    {
                        mem.doDMADisconnect();
                    }
                }
                else if (!dmaConnected)
                {
                    if (ImGui::Button("Connect", ImVec2(140, 30)))
                    {
                        mem.doDMAConnect();
                    }
                }
                else
                {
                    if (ImGui::Button("Disconnect", ImVec2(140, 30)))
                    {
                        mem.doDMADisconnect();
                    }

                    ImGui::SameLine();

                    if (processFound &&
                        ImGui::Button("Soft Restart", ImVec2(140, 30)))
                    {
                        players.softRestart();
                    }
                }


                if (!memoryGlobals::dmaConnected)
                {
                    if (ImGui::Checkbox(" Auto Connect", &memoryGlobals::dmaAutoConnect)) configManager.SaveConfig(); ImGui::SameLine(); ImGui::Checkbox(" Close Connections", &memoryGlobals::dmaCloseAll);

                }
                ImGui::Checkbox(" Show Stats", &memoryGlobals::dmaShowStats);

                ImGui::NewLine();

                ImGui::SeparatorText("App Settings");
                ImGui::PushItemWidth(150); if (ImGui::SliderFloat(" Window Alpha", &globals::appWindowAlpha, 0.f, 1.f, "%.1f")) configManager.SaveConfig(); ImGui::PopItemWidth();
                ImGui::PushItemWidth(150); if (ImGui::SliderFloat(" Radar Max FPS", &globals::appRadarMaxFPS, 30.f, 60.f, "%.f")) configManager.SaveConfig(); ImGui::PopItemWidth();
                ImGui::PushItemWidth(150); if (showResSelectionBox()) configManager.SaveConfig(); ImGui::PopItemWidth();

                ImGui::NewLine();
                //dogtag api connect details
                {
                    ImGui::SeparatorText("Dogtag Cloud API");

                    static char apiKeyBuffer[256]{};
                    static bool keyLoadedToBuffer = false;

                    static bool keyStatusChecked = false;
                    static bool keyIsValid = false;
                    static std::string keyStatusText = "Not checked";
                    static std::string lastError;

                    if (!keyLoadedToBuffer)
                    {
                        strncpy_s(apiKeyBuffer, globals::dogTagAPIKey.c_str(), sizeof(apiKeyBuffer) - 1);

                        keyLoadedToBuffer = true;
                    }

                    ImGui::TextWrapped(" Visit apicloud.meatyradar.co.uk/ for FREE\n Help build the database of players!\n");

                    ImGui::InputText(" API Key", apiKeyBuffer, IM_ARRAYSIZE(apiKeyBuffer), ImGuiInputTextFlags_Password);

                    if (ImGui::Button("Save Key", ImVec2(140, 30)))
                    {
                        std::string key = apiKeyBuffer;
                        g_DogTagAPI.setApiKey(key);

                        
                        globals::dogTagAPIKey = key;
                        configManager.SaveConfig();

                        keyStatusChecked = false;
                        keyIsValid = false;
                        keyStatusText = "Saved. Not checked yet.";
                        lastError.clear();
                    }

                    ImGui::SameLine();

                    if (ImGui::Button("Check Status", ImVec2(140, 30)))
                    {
                        std::string key = apiKeyBuffer;
                        g_DogTagAPI.setApiKey(key);

                        keyStatusChecked = true;
                        keyIsValid = false;
                        keyStatusText = "Checking...";
                        lastError.clear();

                        auto status = g_DogTagAPI.getKeyStatus();

                        if (status && status->valid)
                        {
                            keyIsValid = true;
                            keyStatusText = "Active";
                        }
                        else
                        {
                            keyIsValid = false;
                            keyStatusText = "Invalid / Disabled / Banned";

                            if (status && !status->error.empty())
                                lastError = status->error;
                            else
                                lastError = "Failed to check API key status.";
                        }
                    }

                    if (globals::dogTagAPIKey != "")
                    {
                        ImGui::NewLine();

                        if (ImGui::Button("Clear Key", ImVec2(140, 30)))
                        {
                            memset(apiKeyBuffer, 0, sizeof(apiKeyBuffer));
                            g_DogTagAPI.clearApiKey();

                            globals::dogTagAPIKey = "";
                            configManager.SaveConfig();

                            keyStatusChecked = false;
                            keyIsValid = false;
                            keyStatusText = "No key set";
                            lastError.clear();
                        }
                    }

                    ImGui::Spacing();
                    ImGui::SeparatorText("Status");

                    if (!keyStatusChecked)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "Status: %s", keyStatusText.c_str());
                    }
                    else if (keyIsValid)
                    {
                        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.35f, 1.0f), "Status: Active");
                    }
                    else
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "Status: %s", keyStatusText.c_str());
                    }

                    if (!lastError.empty())
                        ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "Error: %s", lastError.c_str());
                }

                ImGui::SeparatorText("App Timings");
                auto DrawTaskTiming = [](const char* label, double& value, double minValue, double maxValue, double speed = 1.0)
                    {
                        const double oldValue = value;

                        ImGui::SetNextItemWidth(200.0f);

                        if (ImGui::DragScalar(
                            label,
                            ImGuiDataType_Double,
                            &value,
                            static_cast<float>(speed),
                            &minValue,
                            &maxValue,
                            "%.0f ms",
                            ImGuiSliderFlags_AlwaysClamp))
                        {
                            value = std::clamp(value, minValue, maxValue);
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip("%s\nCurrent: %.2f ms\nApprox rate: %.1f Hz",
                                label,
                                value,
                                value > 0.0 ? (1000.0 / value) : 0.0
                            );
                        }

                        return oldValue != value;
                    };

                bool timingsChanged = false;

                timingsChanged |= DrawTaskTiming(
                    "Camera",
                    globals::taskCamera,
                    1.0,
                    100.0,
                    0.5
                );

                timingsChanged |= DrawTaskTiming(
                    "Players",
                    globals::taskPlayers,
                    5.0,
                    500.0,
                    1.0
                );

                timingsChanged |= DrawTaskTiming(
                    "Player Bones",
                    globals::taskPlayersBones,
                    5.0,
                    500.0,
                    1.0
                );

                timingsChanged |= DrawTaskTiming(
                    "Loot",
                    globals::taskLoot,
                    100.0,
                    30000.0,
                    100.0
                );

                timingsChanged |= DrawTaskTiming(
                    "Player Equipment",
                    globals::taskPlayersEquipment,
                    100.0,
                    30000.0,
                    100.0
                );

                timingsChanged |= DrawTaskTiming(
                    "Grenades",
                    globals::taskGrenades,
                    10.0,
                    5000.0,
                    10.0
                );

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings"))
            {
                ImGui::SeparatorText("Radar Settings");
                if (ImGui::Checkbox(" Draw Players", &radarGlobals::drawPlayers)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Grenades", &radarGlobals::drawGrenades)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Exfils", &radarGlobals::drawExfils)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Loot", &radarGlobals::drawLoot)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Quest Helper", &radarGlobals::drawQuestHelper)) configManager.SaveConfig();
                ImGui::Text("Local AimLine     "); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("##localAimLine", &radarGlobals::localAimLine, 4, 500, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();
                ImGui::Text("Friend AimLine    "); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("##friendAimLine", &radarGlobals::friendAimLine, 4, 500, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();
                ImGui::Text("Enemy AimLine   "); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("##enemyAimLine", &radarGlobals::enemyAimLine, 4, 500, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();


                ImGui::SeparatorText("Fuser/ESP Settings");

                if (ImGui::Checkbox(" Draw Players        ", &espGlobals::drawPlayers)) configManager.SaveConfig(); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("m##player", &espGlobals::drawPlayerDist, 10, 1000, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();
                if (ImGui::Checkbox(" Draw Grenades    ", &espGlobals::drawGrenades)) configManager.SaveConfig(); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("m##grenade", &espGlobals::drawGrenadesDist, 10, 400, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();
                if (ImGui::Checkbox(" Draw Loot             ", &espGlobals::drawLoot)) configManager.SaveConfig(); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("m##loot", &espGlobals::drawLootDist, 5, 400, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();
                if (ImGui::Checkbox(" Draw Corpse          ", &espGlobals::drawCorpse)) configManager.SaveConfig(); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("m##corpse", &espGlobals::drawCorpseDist, 5, 400, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();
                if (ImGui::Checkbox(" Draw Box's", &espGlobals::drawBoxPlayers)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Quest Helper. ", &espGlobals::drawQuestHelper)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Skeleton", &espGlobals::drawSkeletons)) configManager.SaveConfig(); ImGui::SameLine(); if (ImGui::Checkbox(" Only closest", &espGlobals::skeletonsOnlyClosest)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Head Dot", &espGlobals::drawHeadDot)) configManager.SaveConfig();ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderFloat("##headdotSize", &espGlobals::headDotSize, 0.5, 10, "%.0f", ImGuiSliderFlags_AlwaysClamp)) espGlobals::headDotSize = std::round(espGlobals::headDotSize * 10.0f) / 10.0f; configManager.SaveConfig(); ImGui::PopItemWidth();
                if (ImGui::Checkbox(" Draw Crosshair", &espGlobals::drawCrosshair)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Fireport line", &espGlobals::drawFireportLine)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Draw Exfils             ", &espGlobals::drawExfil)) configManager.SaveConfig(); ImGui::SameLine(); ImGui::PushItemWidth(150); if (ImGui::SliderInt("m##exfildist", &espGlobals::drawExfilDist, 5, 1000, "%d")) configManager.SaveConfig(); ImGui::PopItemWidth();


                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Features"))
            {
                ImGui::SeparatorText("Safe Features");
                if (ImGui::Checkbox(" Get Equipment Info", &radarGlobals::getPlayerEquip)) configManager.SaveConfig();
                if (ImGui::Checkbox(" Get TarkovDev Info", &radarGlobals::getPlayerStats)) configManager.SaveConfig();


                ImGui::SeparatorText("Risky Features");
                

                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Colours"))
            {
                ImGui::SeparatorText("Player Colours");
                if (ImGui::ColorEdit4(" PMC Player", (float*)&coloursGlobals::playerPMC, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Player Scav", (float*)&coloursGlobals::playerScav, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" AI Player", (float*)&coloursGlobals::playerAI, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Local Player", (float*)&coloursGlobals::playerLocal, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Friendly Player", (float*)&coloursGlobals::playerFriendly, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" AI Boss", (float*)&coloursGlobals::playerBoss, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" BTR", (float*)&coloursGlobals::aiBTR, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Watched Player", (float*)&coloursGlobals::playerWatched, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();

                ImGui::SeparatorText("Radar / ESP Colours");
                if (ImGui::ColorEdit4(" Player GroupLine", (float*)&coloursGlobals::playerGroupLine, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Player Corpse", (float*)&coloursGlobals::playerCorpse, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Grenades", (float*)&coloursGlobals::grenades, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Exfils", (float*)&coloursGlobals::exfils, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Crosshair", (float*)&coloursGlobals::crosshair, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" FOV Circle", (float*)&coloursGlobals::fovCircle, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();
                if (ImGui::ColorEdit4(" Quest Markers", (float*)&coloursGlobals::questMarker, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs)) configManager.SaveConfig();


                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Misc"))
            {
                ImGui::SeparatorText("Keybind Settings");
                if (ShowKeySelectionBox(keyGlobals::aimKey, " Aim Key")) configManager.SaveConfig();
                if (ShowKeySelectionBox(keyGlobals::toggleFollow, " Toggle Follow")) configManager.SaveConfig();
                if (ShowKeySelectionBox(keyGlobals::battleMode, " Battle Mode")) configManager.SaveConfig();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }





    }
    ImGui::End();
}

std::string formatDataRate(size_t bytes) {
    const char* suffix[] = { "B/s", "KB/s", "MB/s", "GB/s", "TB/s" };
    size_t i = 0;
    double dblBytes = static_cast<double>(bytes);

    while (dblBytes >= 1024 && i < 4) {
        dblBytes /= 1024;
        i++;
    }

    char formatted[32];
    snprintf(formatted, sizeof(formatted), "%.2f %s", dblBytes, suffix[i]);
    return std::string(formatted);
}

static void renderBottomInfo()
{
    // view port
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Render our app version always
    ImGui::SetCursorPos(ImVec2(viewport->Size.x - 150, viewport->Size.y - 30));
    ImGui::Text("MeatyEFT: %s", globals::appVersion);

    // Render DMA Stats if selected
    // Data
    if (memoryGlobals::dmaShowStats)
    {
            ImGui::SetCursorPos(ImVec2(viewport->Size.x - viewport->Size.x + 20, viewport->Size.y - 30));
            ImGui::Text(mem.GetTrafficStatsString().c_str());
    }

}
// Helper function to convert MessageLevel to string
std::string messageLevelToString(MessageLevel level) {
    switch (level) {
    case MessageLevel::INFO:
        return "INFO";
    case MessageLevel::WARN:
        return "WARN";
    case MessageLevel::ERR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

// Helper function to export messages to a text file
void exportMessagesToFile(const std::vector<Message>& messages, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) return;

    for (const auto& msg : messages) {
        file << "Timestamp: " << msg.timestamp << "s, Type: " << messageLevelToString(msg.level).c_str()
            << ", Message: " << msg.text << std::endl;
    }

    file.close();
}

int CountNonZeroEntries(const uint64_t* buffer, int size) {
    int count = 0;
    for (int i = 0; i < size; ++i) {
        if (buffer[i] != 0) {
            ++count;
        }
    }
    return count;
}

// Function to convert glm::mat4 to string
std::string Mat4ToString(const glm::highp_mat4& mat) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    for (int i = 0; i < 4; ++i) {
        const glm::vec4& row = glm::row(mat, i);
        oss << "[" << row.x << ", " << row.y << ", " << row.z << ", " << row.w << "]\n";
    }
    return oss.str();
}

// Function to display matrix as tooltip in ImGui
void ShowMatrixTooltip(const glm::highp_mat4& mat) {
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(Mat4ToString(mat).c_str());
        ImGui::EndTooltip();
    }
}

// helpers for debug camera
static const char* BoolText(bool v)
{
    return v ? "TRUE" : "FALSE";
}

static void DebugTextBool(const char* label, bool value)
{
    ImGui::Text("%s: %s", label, BoolText(value));
}

static void DebugTextPtr(const char* label, uint64_t ptr)
{
    const bool valid = Utils::valid_pointer(ptr);

    ImGui::Text("%s: 0x%llX  [%s]",
        label,
        static_cast<unsigned long long>(ptr),
        valid ? "VALID" : "INVALID"
    );
}

static bool DebugMatrixLooksValid(const glm::highp_mat4& m)
{
    int nonZeroCount = 0;
    float maxAbs = 0.0f;

    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            const float v = m[c][r];

            if (!std::isfinite(v))
                return false;

            const float av = std::fabs(v);

            if (av > 100000.0f)
                return false;

            if (av > 0.00001f)
            {
                ++nonZeroCount;

                if (av > maxAbs)
                    maxAbs = av;
            }
        }
    }

    return nonZeroCount >= 6 && maxAbs >= 0.0001f;
}

static void DebugMatrixSummary(const char* label, const glm::highp_mat4& m)
{
    const bool valid = DebugMatrixLooksValid(m);

    ImGui::Text("%s: %s  (hover)", label, valid ? "VALID" : "INVALID");

    if (ImGui::IsItemHovered())
        ShowMatrixTooltip(m);
}

static void renderDebugWindow()
{
    std::string windowNameMain = "Debug";
    static ImGuiWindowFlags flagss = ImGuiWindowFlags_NoCollapse;

    ImGui::SetNextWindowSize(ImVec2(900, 500), ImGuiCond_FirstUseEver);

    static bool showInfo = true;
    static bool showWarn = true;
    static bool showError = true;

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetDebug, flagss))
    {
        if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_FittingPolicyResizeDown))
        {
            if (ImGui::BeginTabItem("Console"))
            {

                ImGui::Checkbox("Show Info", &showInfo); ImGui::SameLine(); ImGui::Checkbox("Show Warn", &showWarn); ImGui::SameLine(); ImGui::Checkbox("Show Error", &showError);

                if (ImGui::Button("Export to file")) {
                    exportMessagesToFile(LOGS.getMessages(), "debug_log.txt");
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Logs")) {
                    LOGS.clearLog();
                }

                ImGui::Separator();

                // Setup table
                if (ImGui::BeginTable("DebugTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    const auto& messages = LOGS.getMessages();

                    if (messages.size() > 3000)
                        LOGS.clearLog();

                    for (const auto& msg : messages) {
                        bool showMessage = false;
                        ImVec4 color;

                        switch (msg.level) {
                        case MessageLevel::INFO:
                            showMessage = showInfo;
                            color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                            break;
                        case MessageLevel::WARN:
                            showMessage = showWarn;
                            color = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                            break;
                        case MessageLevel::ERR:
                            showMessage = showError;
                            color = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
                            break;
                        }

                        if (showMessage) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%.2fs", msg.timestamp);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(color, "%s", messageLevelToString(msg.level).c_str());


                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(color, "%s", msg.text.c_str());
                        }
                    }

                    ImGui::SetScrollHereY(1.0f);
                }
                ImGui::EndTable();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Performance"))
            {
                static bool freezeList = false;
                static bool newestFirst = false;
                static bool jumpToNewest = false;

                static auto displaySamples = PerfMonitor::Instance().GetRecent();

                static auto lastRefresh = std::chrono::steady_clock::time_point{};

                const auto now = std::chrono::steady_clock::now();

                // Only refresh the table data four times per second unless frozen
                if (!freezeList &&
                    (lastRefresh == std::chrono::steady_clock::time_point{} ||
                        now - lastRefresh >= std::chrono::milliseconds(250)))
                {
                    displaySamples = PerfMonitor::Instance().GetRecent();
                    lastRefresh = now;
                }

                const double peakMs = PerfMonitor::Instance().GetPeakMs();
                const std::string peakName = PerfMonitor::Instance().GetPeakName();
                const std::string peakDetail = PerfMonitor::Instance().GetPeakDetail();

                ImGui::Text("Peak spike: %.1f ms", peakMs);

                if (!peakName.empty())
                    ImGui::Text("Source: %s", peakName.c_str());

                if (!peakDetail.empty())
                    ImGui::Text("Detail: %s", peakDetail.c_str());

                if (ImGui::Button("Reset peak"))
                    PerfMonitor::Instance().ResetPeak();

                ImGui::SameLine();

                if (ImGui::Button("Refresh now"))
                {
                    displaySamples = PerfMonitor::Instance().GetRecent();
                    lastRefresh = now;
                }

                ImGui::Separator();

                if (ImGui::Checkbox("Freeze list", &freezeList))
                {
                    // Capture a final stable snapshot at the instant it is frozen
                    if (freezeList)
                    {
                        displaySamples = PerfMonitor::Instance().GetRecent();
                        lastRefresh = now;
                    }
                }

                ImGui::SameLine();

                if (ImGui::Checkbox("Newest first", &newestFirst))
                    jumpToNewest = true;

                ImGui::SameLine();

                if (ImGui::Button("Jump to newest"))
                    jumpToNewest = true;

                ImGui::TextWrapped(
                    "Silent lag usually comes from DMA mutex contention: multiple threads "
                    "(players, bones, camera, loot, grenades) waiting on scatter reads. "
                    "Entries below are tasks/scatters slower than ~25-35ms."
                );

                if (ImGui::BeginTable(
                    "PerfTable",
                    4,
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_BordersInnerV,
                    ImVec2(0, 360)))
                {
                    ImGui::TableSetupColumn(
                        "Time",
                        ImGuiTableColumnFlags_WidthFixed,
                        75.0f
                    );

                    ImGui::TableSetupColumn(
                        "ms",
                        ImGuiTableColumnFlags_WidthFixed,
                        55.0f
                    );

                    ImGui::TableSetupColumn(
                        "Name",
                        ImGuiTableColumnFlags_WidthFixed,
                        180.0f
                    );

                    ImGui::TableSetupColumn(
                        "Detail",
                        ImGuiTableColumnFlags_WidthStretch
                    );

                    ImGui::TableHeadersRow();

                    auto drawSample = [](const auto& sample)
                        {
                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%.1fs", sample.timestampSec);

                            ImGui::TableSetColumnIndex(1);

                            if (sample.durationMs >= 100.0)
                            {
                                ImGui::TextColored(
                                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                    "%.0f",
                                    sample.durationMs
                                );
                            }
                            else if (sample.durationMs >= 50.0)
                            {
                                ImGui::TextColored(
                                    ImVec4(1.0f, 0.75f, 0.25f, 1.0f),
                                    "%.0f",
                                    sample.durationMs
                                );
                            }
                            else
                            {
                                ImGui::Text("%.0f", sample.durationMs);
                            }

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted(sample.name.c_str());

                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextUnformatted(sample.detail.c_str());
                        };

                    if (newestFirst)
                    {
                        // Latest event at the top.
                        for (auto it = displaySamples.rbegin();
                            it != displaySamples.rend();
                            ++it)
                        {
                            drawSample(*it);
                        }

                        if (jumpToNewest)
                        {
                            ImGui::SetScrollY(0.0f);
                            jumpToNewest = false;
                        }
                    }
                    else
                    {
                        for (const auto& sample : displaySamples)
                            drawSample(sample);

                        if (jumpToNewest)
                        {
                            ImGui::SetScrollHereY(1.0f);
                            jumpToNewest = false;
                        }
                    }

                    ImGui::EndTable();
                }

                ImGui::EndTabItem();
            }
           if (ImGui::BeginTabItem("Memory"))
            {
                if (ImGui::BeginTabBar("##Tabsmemory", ImGuiTabBarFlags_FittingPolicyResizeDown))
                {
                    if (ImGui::BeginTabItem("mainGame"))
                    {
                        ImGui::Text("gameObjectManager: %llu", mainGame.gameObjectManager);

                        ImGui::Text("gameWorld: %llu", mainGame.gameWorld);
                        ImGui::Text("localGameWorld: %llu", mainGame.localGameWorld);

                        ImGui::Text("registeredPlayers: %llu", mainGame.registeredPlayers);
                        ImGui::Text("registeredPlayersList: %llu", mainGame.registeredPlayersList);
                        ImGui::Text("registeredPlayersCount: %d", mainGame.registeredPlayersCount);

                        int playerBufferCount = CountNonZeroEntries(mainGame.player_buffer, 127);
                        ImGui::Text("player_buffer entries: %d", playerBufferCount);

                        ImGui::Text("selectedLocation: %s", mainGame.selectedLocation.c_str());

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("local"))
                    {
                        
                        ImGui::Text("localPlayerPtr: %llu", mainGame.localPlayerPtr);
                        ImGui::Text("localPlayerHands: %llu", mainGame.localPlayerHands);
                        ImGui::Text("localPlayerPWA: %llu", mainGame.localPlayerPWA);
                        ImGui::Text("localplayerProfile: %llu", mainGame.localplayerProfile);

                        ImGui::Text("worldLocation: (%.2f, %.2f, %.2f)", mainGame.localLocation.x, mainGame.localLocation.y, mainGame.localLocation.z);
                        ImGui::Text("rotation: (%.2f, %.2f)", mainGame.localRotation.x, mainGame.localRotation.y);

                        ImGui::Text("groupid: %s", mainGame.localGroupId.c_str());
                        if (mainGame.localIsScoped) ImGui::Text("isScoped: TRUE"); else ImGui::Text("isScoped: FALSE");

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("player"))
                    {
                        const std::vector<PlayerCache> cache =
                            players.getCacheSnapshot();

                        static ImGuiTextFilter playerFilter;
                        static bool showDead = true;
                        static bool showAi = true;
                        static bool showPlayers = true;
                        static bool showBtr = true;
                        static bool onlyMissingBones = false;
                        static uint64_t selectedInstance = 0;

                        auto IsValidPtr = [](uint64_t value) -> bool
                            {
                                return Utils::valid_pointer(value);
                            };

                        auto GetPlayerType = [](const PlayerCache& player) -> const char*
                            {
                                if (player.isBTR)
                                    return "BTR";

                                if (player.isLocal)
                                    return "LOCAL";

                                if (player.isBoss)
                                    return "BOSS";

                                if (player.isPlayerScav)
                                    return "P-SCAV";

                                if (player.isAi)
                                    return "AI";

                                if (player.isPlayer)
                                    return "PMC";

                                return "UNKNOWN";
                            };

                        auto GetStateText = [](const PlayerCache& player) -> const char*
                            {
                                if (!Utils::valid_pointer(player.instance))
                                    return "INVALID";

                                if (player.hasExfiled)
                                    return "EXFIL";

                                if (player.isDead)
                                    return "DEAD";

                                return "LIVE";
                            };

                        auto GetStateColour = [](const PlayerCache& player) -> ImVec4
                            {
                                if (!Utils::valid_pointer(player.instance))
                                    return ImVec4(0.85f, 0.25f, 0.25f, 1.0f);

                                if (player.hasExfiled)
                                    return ImVec4(0.85f, 0.65f, 0.20f, 1.0f);

                                if (player.isDead)
                                    return ImVec4(0.75f, 0.30f, 0.30f, 1.0f);

                                if (player.isLocal)
                                    return ImVec4(0.25f, 0.75f, 1.0f, 1.0f);

                                if (player.isBTR)
                                    return ImVec4(1.0f, 0.55f, 0.15f, 1.0f);

                                return ImVec4(0.35f, 0.90f, 0.45f, 1.0f);
                            };

                        auto BonePtrAt = [&](const PlayerCache& player,
                            boneListIndexes index) -> uint64_t
                            {
                                const size_t slot =
                                    static_cast<size_t>(index);

                                if (slot >= player.bonePtrs.size())
                                    return 0;

                                return player.bonePtrs[slot];
                            };

                        auto BonePositionAt = [&](const PlayerCache& player,
                            boneListIndexes index) -> glm::vec3
                            {
                                const size_t slot =
                                    static_cast<size_t>(index);

                                if (slot >= player.bonePositions.size())
                                    return glm::vec3(0.0f);

                                return player.bonePositions[slot];
                            };

                        auto CountValidBonePtrs = [&](const PlayerCache& player) -> size_t
                            {
                                size_t count = 0;

                                for (const uint64_t ptr : player.bonePtrs)
                                {
                                    if (IsValidPtr(ptr))
                                        ++count;
                                }

                                return count;
                            };

                        auto HasMinimalBones = [&](const PlayerCache& player) -> bool
                            {
                                return IsValidPtr(
                                    BonePtrAt(player, boneListIndexes::Base)
                                ) &&
                                    IsValidPtr(
                                        BonePtrAt(player, boneListIndexes::LFoot)
                                    ) &&
                                    IsValidPtr(
                                        BonePtrAt(player, boneListIndexes::RFoot)
                                    );
                            };

                        auto DrawPointerLine = [&](const char* label,
                            uint64_t pointer)
                            {
                                const bool valid = IsValidPtr(pointer);

                                ImGui::TextUnformatted(label);
                                ImGui::SameLine(210.0f);

                                ImGui::TextColored(
                                    valid
                                    ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                                    : ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                                    "0x%016llX",
                                    static_cast<unsigned long long>(pointer)
                                );
                            };

                        auto DrawPlayerTooltip = [&](const PlayerCache& player)
                            {
                                ImGui::BeginTooltip();

                                ImGui::Text(
                                    "%s | %s",
                                    player.name.empty()
                                    ? "Unnamed"
                                    : player.name.c_str(),
                                    GetPlayerType(player)
                                );

                                ImGui::Separator();

                                ImGui::Text(
                                    "Instance: 0x%016llX",
                                    static_cast<unsigned long long>(
                                        player.instance
                                        )
                                );

                                ImGui::Text(
                                    "Distance: %dm",
                                    player.distance
                                );

                                ImGui::Text(
                                    "Location: %.2f, %.2f, %.2f",
                                    player.location.x,
                                    player.location.y,
                                    player.location.z
                                );

                                ImGui::Text(
                                    "Rotation Raw: %.2f, %.2f",
                                    player.rotationRAW.x,
                                    player.rotationRAW.y
                                );

                                ImGui::Text(
                                    "Rotation Fixed: %.2f, %.2f",
                                    player.rotation.x,
                                    player.rotation.y
                                );

                                ImGui::Separator();
                                ImGui::TextUnformatted("Pointers");

                                DrawPointerLine(
                                    "Bone Matrix",
                                    player.playerBoneMatrixPtr
                                );

                                DrawPointerLine(
                                    "Observed Controller",
                                    player.P_ObservedPlayerController
                                );

                                DrawPointerLine(
                                    "Observed Health",
                                    player.P_ObservedHealthController
                                );

                                DrawPointerLine(
                                    "Movement Context",
                                    player.P_MovementContext
                                );

                                DrawPointerLine(
                                    "Rotation Address",
                                    player.P_RotationAddress
                                );

                                DrawPointerLine(
                                    "Inventory Controller Addr",
                                    player.P_InventoryControllerAddr
                                );

                                DrawPointerLine(
                                    "Hands Controller Addr",
                                    player.P_HandsControllerAddr
                                );

                                DrawPointerLine(
                                    "Hands Controller",
                                    player.P_HandsController
                                );

                                DrawPointerLine(
                                    "Profile",
                                    player.P_Profile
                                );

                                DrawPointerLine(
                                    "PWA",
                                    player.P_PWA
                                );

                                DrawPointerLine(
                                    "Corpse Address",
                                    player.P_CorpseAddr
                                );

                                DrawPointerLine(
                                    "Corpse Class",
                                    player.P_CorpseClass
                                );

                                ImGui::Separator();
                                ImGui::TextUnformatted("Bone Cache");

                                ImGui::Text(
                                    "Pointers: %zu / %zu",
                                    CountValidBonePtrs(player),
                                    player.bonePtrs.size()
                                );

                                ImGui::Text(
                                    "Need Resolve: %s",
                                    player.bonePointersNeedResolve
                                    ? "YES"
                                    : "NO"
                                );

                                ImGui::Text(
                                    "Minimal Bones: %s",
                                    HasMinimalBones(player)
                                    ? "READY"
                                    : "MISSING"
                                );

                                DrawPointerLine(
                                    "Base",
                                    BonePtrAt(
                                        player,
                                        boneListIndexes::Base
                                    )
                                );

                                DrawPointerLine(
                                    "LFoot",
                                    BonePtrAt(
                                        player,
                                        boneListIndexes::LFoot
                                    )
                                );

                                DrawPointerLine(
                                    "RFoot",
                                    BonePtrAt(
                                        player,
                                        boneListIndexes::RFoot
                                    )
                                );

                                ImGui::EndTooltip();
                            };

                        size_t localCount = 0;
                        size_t pmcCount = 0;
                        size_t aiCount = 0;
                        size_t bossCount = 0;
                        size_t scavCount = 0;
                        size_t deadCount = 0;
                        size_t btrCount = 0;
                        size_t missingBonesCount = 0;
                        size_t equipmentReadyCount = 0;

                        for (const PlayerCache& player : cache)
                        {
                            if (player.isLocal)
                                ++localCount;

                            if (player.isPlayer &&
                                !player.isPlayerScav &&
                                !player.isAi)
                            {
                                ++pmcCount;
                            }

                            if (player.isAi)
                                ++aiCount;

                            if (player.isBoss)
                                ++bossCount;

                            if (player.isPlayerScav)
                                ++scavCount;

                            if (player.isDead)
                                ++deadCount;

                            if (player.isBTR)
                                ++btrCount;

                            if (!player.isBTR &&
                                !HasMinimalBones(player))
                            {
                                ++missingBonesCount;
                            }

                            if (player.equipInited)
                                ++equipmentReadyCount;
                        }

                        ImGui::Text(
                            "Cached Players: %zu",
                            cache.size()
                        );

                        ImGui::SameLine();
                        ImGui::TextDisabled("|");

                        ImGui::SameLine();
                        ImGui::Text(
                            "Local: %zu",
                            localCount
                        );

                        ImGui::SameLine();
                        ImGui::Text(
                            "PMC: %zu",
                            pmcCount
                        );

                        ImGui::SameLine();
                        ImGui::Text(
                            "AI: %zu",
                            aiCount
                        );

                        ImGui::SameLine();
                        ImGui::Text(
                            "Boss: %zu",
                            bossCount
                        );

                        ImGui::SameLine();
                        ImGui::Text(
                            "PScav: %zu",
                            scavCount
                        );

                        ImGui::SameLine();
                        ImGui::Text(
                            "Dead: %zu",
                            deadCount
                        );

                        ImGui::SameLine();
                        ImGui::Text(
                            "BTR: %zu",
                            btrCount
                        );

                        ImGui::Separator();

                        playerFilter.Draw("Search", 260.0f);

                        ImGui::SameLine();
                        ImGui::Checkbox("Show Dead", &showDead);

                        ImGui::SameLine();
                        ImGui::Checkbox("Show AI", &showAi);

                        ImGui::SameLine();
                        ImGui::Checkbox("Show PMC/PScav", &showPlayers);

                        ImGui::SameLine();
                        ImGui::Checkbox("Show BTR", &showBtr);

                        ImGui::SameLine();
                        ImGui::Checkbox(
                            "Only Missing Bones",
                            &onlyMissingBones
                        );

                        ImGui::TextDisabled(
                            "Equipment Ready: %zu | Missing Base/LFoot/RFoot: %zu",
                            equipmentReadyCount,
                            missingBonesCount
                        );

                        ImGui::Separator();

                        const ImGuiTableFlags tableFlags =
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_Reorderable |
                            ImGuiTableFlags_Hideable |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp;

                        if (ImGui::BeginTable(
                            "##PlayerCacheTable",
                            10,
                            tableFlags,
                            ImVec2(0.0f, 390.0f)))
                        {
                            ImGui::TableSetupScrollFreeze(0, 1);

                            ImGui::TableSetupColumn(
                                "State",
                                ImGuiTableColumnFlags_WidthFixed,
                                70.0f
                            );

                            ImGui::TableSetupColumn(
                                "Type",
                                ImGuiTableColumnFlags_WidthFixed,
                                62.0f
                            );

                            ImGui::TableSetupColumn(
                                "Name",
                                ImGuiTableColumnFlags_WidthStretch,
                                1.40f
                            );

                            ImGui::TableSetupColumn(
                                "Distance",
                                ImGuiTableColumnFlags_WidthFixed,
                                70.0f
                            );

                            ImGui::TableSetupColumn(
                                "Position",
                                ImGuiTableColumnFlags_WidthStretch,
                                1.20f
                            );

                            ImGui::TableSetupColumn(
                                "Rotation",
                                ImGuiTableColumnFlags_WidthFixed,
                                95.0f
                            );

                            ImGui::TableSetupColumn(
                                "Health",
                                ImGuiTableColumnFlags_WidthFixed,
                                62.0f
                            );

                            ImGui::TableSetupColumn(
                                "Equipment",
                                ImGuiTableColumnFlags_WidthStretch,
                                1.00f
                            );

                            ImGui::TableSetupColumn(
                                "Bones",
                                ImGuiTableColumnFlags_WidthFixed,
                                88.0f
                            );

                            ImGui::TableSetupColumn(
                                "Group",
                                ImGuiTableColumnFlags_WidthStretch,
                                0.90f
                            );

                            ImGui::TableHeadersRow();

                            ImGuiListClipper clipper;
                            clipper.Begin(
                                static_cast<int>(cache.size())
                            );

                            while (clipper.Step())
                            {
                                for (int i = clipper.DisplayStart;
                                    i < clipper.DisplayEnd;
                                    ++i)
                                {
                                    const PlayerCache& player =
                                        cache[static_cast<size_t>(i)];

                                    if (!showDead &&
                                        (player.isDead || player.hasExfiled))
                                    {
                                        continue;
                                    }

                                    if (!showAi &&
                                        player.isAi &&
                                        !player.isBTR)
                                    {
                                        continue;
                                    }

                                    if (!showPlayers &&
                                        (player.isPlayer ||
                                            player.isPlayerScav ||
                                            player.isLocal))
                                    {
                                        continue;
                                    }

                                    if (!showBtr && player.isBTR)
                                        continue;

                                    if (onlyMissingBones &&
                                        HasMinimalBones(player))
                                    {
                                        continue;
                                    }

                                    std::string searchText;

                                    searchText.reserve(
                                        player.name.size() +
                                        player.className.size() +
                                        player.groupId.size() +
                                        64
                                    );

                                    searchText += player.name;
                                    searchText += " ";
                                    searchText += player.className;
                                    searchText += " ";
                                    searchText += player.groupId;
                                    searchText += " ";
                                    searchText += GetPlayerType(player);

                                    if (!playerFilter.PassFilter(
                                        searchText.c_str()))
                                    {
                                        continue;
                                    }

                                    ImGui::PushID(
                                        static_cast<int>(
                                            player.instance & 0x7FFFFFFF
                                            )
                                    );

                                    ImGui::TableNextRow(
                                        ImGuiTableRowFlags_None,
                                        24.0f
                                    );

                                    ImGui::TableSetColumnIndex(0);

                                    const bool isSelected =
                                        selectedInstance ==
                                        player.instance;

                                    ImGui::PushStyleColor(
                                        ImGuiCol_Text,
                                        GetStateColour(player)
                                    );

                                    if (ImGui::Selectable(
                                        GetStateText(player),
                                        isSelected,
                                        ImGuiSelectableFlags_None,
                                        ImVec2(0.0f, 0.0f)))
                                    {
                                        selectedInstance =
                                            player.instance;
                                    }

                                    ImGui::PopStyleColor();

                                    if (ImGui::IsItemHovered(
                                        ImGuiHoveredFlags_DelayShort))
                                    {
                                        DrawPlayerTooltip(player);
                                    }

                                    ImGui::TableSetColumnIndex(1);

                                    ImGui::TextUnformatted(
                                        GetPlayerType(player)
                                    );

                                    ImGui::TableSetColumnIndex(2);

                                    const char* displayName =
                                        player.name.empty()
                                        ? "<unnamed>"
                                        : player.name.c_str();

                                    ImGui::TextUnformatted(displayName);

                                    if (ImGui::IsItemHovered(
                                        ImGuiHoveredFlags_DelayShort))
                                    {
                                        DrawPlayerTooltip(player);
                                    }

                                    ImGui::TableSetColumnIndex(3);

                                    ImGui::Text(
                                        "%dm",
                                        player.distance
                                    );

                                    ImGui::TableSetColumnIndex(4);

                                    ImGui::Text(
                                        "%.1f / %.1f / %.1f",
                                        player.location.x,
                                        player.location.y,
                                        player.location.z
                                    );

                                    ImGui::TableSetColumnIndex(5);

                                    ImGui::Text(
                                        "%.1f / %.1f",
                                        player.rotation.x,
                                        player.rotation.y
                                    );

                                    ImGui::TableSetColumnIndex(6);

                                    ImGui::Text(
                                        "%d",
                                        player.healthETAG
                                    );

                                    ImGui::TableSetColumnIndex(7);

                                    if (!player.equipInited)
                                    {
                                        ImGui::TextColored(
                                            ImVec4(
                                                1.0f,
                                                0.75f,
                                                0.20f,
                                                1.0f
                                            ),
                                            "INIT"
                                        );
                                    }
                                    else
                                    {
                                        ImGui::Text(
                                            "%zu slots | %d",
                                            player._slots.size(),
                                            player.playerValue
                                        );
                                    }

                                    ImGui::TableSetColumnIndex(8);

                                    const size_t validBones =
                                        CountValidBonePtrs(player);

                                    const bool minimalBones =
                                        HasMinimalBones(player);

                                    ImGui::TextColored(
                                        minimalBones
                                        ? ImVec4(
                                            0.35f,
                                            0.90f,
                                            0.45f,
                                            1.0f
                                        )
                                        : ImVec4(
                                            0.95f,
                                            0.35f,
                                            0.35f,
                                            1.0f
                                        ),
                                        "%zu/%zu%s",
                                        validBones,
                                        player.bonePtrs.size(),
                                        player.bonePointersNeedResolve
                                        ? " *"
                                        : ""
                                    );

                                    ImGui::TableSetColumnIndex(9);

                                    if (player.groupId.empty())
                                    {
                                        ImGui::TextDisabled("-");
                                    }
                                    else
                                    {
                                        ImGui::TextUnformatted(
                                            player.groupId.c_str()
                                        );
                                    }

                                    ImGui::PopID();
                                }
                            }

                            ImGui::EndTable();
                        }

                        const PlayerCache* selectedPlayer = nullptr;

                        for (const PlayerCache& player : cache)
                        {
                            if (player.instance == selectedInstance)
                            {
                                selectedPlayer = &player;
                                break;
                            }
                        }

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::TextUnformatted("Selected Player Inspector");

                        ImGui::BeginChild(
                            "##PlayerInspector",
                            ImVec2(0.0f, 260.0f),
                            true
                        );

                        if (!selectedPlayer)
                        {
                            ImGui::TextDisabled(
                                "Select a player row to inspect its cached state."
                            );
                        }
                        else
                        {
                            const PlayerCache& player =
                                *selectedPlayer;

                            ImGui::TextColored(
                                GetStateColour(player),
                                "%s | %s | %s",
                                player.name.empty()
                                ? "<unnamed>"
                                : player.name.c_str(),
                                GetPlayerType(player),
                                GetStateText(player)
                            );

                            ImGui::Text(
                                "Class: %s",
                                player.className.c_str()
                            );

                            ImGui::Text(
                                "Instance: 0x%016llX",
                                static_cast<unsigned long long>(
                                    player.instance
                                    )
                            );

                            ImGui::Separator();

                            if (ImGui::CollapsingHeader(
                                "Runtime Cache",
                                ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::Text(
                                    "Location: %.3f, %.3f, %.3f",
                                    player.location.x,
                                    player.location.y,
                                    player.location.z
                                );

                                ImGui::Text(
                                    "Distance: %dm",
                                    player.distance
                                );

                                ImGui::Text(
                                    "Rotation Raw: %.3f, %.3f",
                                    player.rotationRAW.x,
                                    player.rotationRAW.y
                                );

                                ImGui::Text(
                                    "Rotation Corrected: %.3f, %.3f",
                                    player.rotation.x,
                                    player.rotation.y
                                );

                                ImGui::Text(
                                    "Health ETag: %d",
                                    player.healthETAG
                                );

                                ImGui::Text(
                                    "Aiming: %s",
                                    player.isAiming ? "YES" : "NO"
                                );

                                ImGui::Text(
                                    "Dead: %s | Exfil: %s",
                                    player.isDead ? "YES" : "NO",
                                    player.hasExfiled ? "YES" : "NO"
                                );
                            }

                            if (ImGui::CollapsingHeader(
                                "Equipment Cache",
                                ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::Text(
                                    "Initialised: %s",
                                    player.equipInited ? "YES" : "NO"
                                );

                                ImGui::Text(
                                    "Slot Count: %zu",
                                    player._slots.size()
                                );

                                ImGui::Text(
                                    "Value: %d",
                                    player.playerValue
                                );

                                ImGui::Text(
                                    "Item In Hand: %s",
                                    player.itemInHand.empty()
                                    ? "-"
                                    : player.itemInHand.c_str()
                                );

                                ImGui::Text(
                                    "Hands Controller: 0x%016llX",
                                    static_cast<unsigned long long>(
                                        player.P_HandsController
                                        )
                                );

                                if (!player._slots.empty() &&
                                    ImGui::TreeNode("Cached Slots"))
                                {
                                    for (const auto& slot : player._slots)
                                    {
                                        ImGui::BulletText(
                                            "%s | %s | %d | %s",
                                            slot.name.c_str(),
                                            slot.equipName.empty()
                                            ? "-"
                                            : slot.equipName.c_str(),
                                            slot.price,
                                            slot.wanted
                                            ? "WANTED"
                                            : ""
                                        );
                                    }

                                    ImGui::TreePop();
                                }
                            }

                            if (ImGui::CollapsingHeader(
                                "Pointers",
                                ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                DrawPointerLine(
                                    "Bone Matrix",
                                    player.playerBoneMatrixPtr
                                );

                                DrawPointerLine(
                                    "Observed Controller",
                                    player.P_ObservedPlayerController
                                );

                                DrawPointerLine(
                                    "Observed Health",
                                    player.P_ObservedHealthController
                                );

                                DrawPointerLine(
                                    "Movement Context",
                                    player.P_MovementContext
                                );

                                DrawPointerLine(
                                    "Rotation Address",
                                    player.P_RotationAddress
                                );

                                DrawPointerLine(
                                    "Inventory Controller Address",
                                    player.P_InventoryControllerAddr
                                );

                                DrawPointerLine(
                                    "Hands Controller Address",
                                    player.P_HandsControllerAddr
                                );

                                DrawPointerLine(
                                    "Hands Controller",
                                    player.P_HandsController
                                );

                                DrawPointerLine(
                                    "Profile",
                                    player.P_Profile
                                );

                                DrawPointerLine(
                                    "Profile Info",
                                    player.P_Info
                                );

                                DrawPointerLine(
                                    "Player Body",
                                    player.P_Body
                                );

                                DrawPointerLine(
                                    "PWA",
                                    player.P_PWA
                                );

                                DrawPointerLine(
                                    "Corpse Address",
                                    player.P_CorpseAddr
                                );

                                DrawPointerLine(
                                    "Corpse Class",
                                    player.P_CorpseClass
                                );
                            }

                            if (ImGui::CollapsingHeader(
                                "Bones",
                                ImGuiTreeNodeFlags_DefaultOpen))
                            {
                                ImGui::Text(
                                    "Bone Pointers: %zu / %zu",
                                    CountValidBonePtrs(player),
                                    player.bonePtrs.size()
                                );

                                ImGui::Text(
                                    "Need Pointer Resolve: %s",
                                    player.bonePointersNeedResolve
                                    ? "YES"
                                    : "NO"
                                );

                                ImGui::Text(
                                    "Base/LFoot/RFoot Ready: %s",
                                    HasMinimalBones(player)
                                    ? "YES"
                                    : "NO"
                                );

                                const glm::vec3 base =
                                    BonePositionAt(
                                        player,
                                        boneListIndexes::Base
                                    );

                                const glm::vec3 lFoot =
                                    BonePositionAt(
                                        player,
                                        boneListIndexes::LFoot
                                    );

                                const glm::vec3 rFoot =
                                    BonePositionAt(
                                        player,
                                        boneListIndexes::RFoot
                                    );

                                ImGui::Text(
                                    "Base:  %.2f, %.2f, %.2f",
                                    base.x,
                                    base.y,
                                    base.z
                                );

                                ImGui::Text(
                                    "LFoot: %.2f, %.2f, %.2f",
                                    lFoot.x,
                                    lFoot.y,
                                    lFoot.z
                                );

                                ImGui::Text(
                                    "RFoot: %.2f, %.2f, %.2f",
                                    rFoot.x,
                                    rFoot.y,
                                    rFoot.z
                                );

                                if (ImGui::TreeNode("All Bone Pointers"))
                                {
                                    for (size_t i = 0;
                                        i < player.bonePtrs.size();
                                        ++i)
                                    {
                                        const uint64_t ptr =
                                            player.bonePtrs[i];

                                        const bool valid =
                                            IsValidPtr(ptr);

                                        ImGui::TextColored(
                                            valid
                                            ? ImVec4(
                                                0.35f,
                                                0.90f,
                                                0.45f,
                                                1.0f
                                            )
                                            : ImVec4(
                                                0.95f,
                                                0.30f,
                                                0.30f,
                                                1.0f
                                            ),
                                            "[%02zu] BoneId %d | 0x%016llX",
                                            i,
                                            i < player.boneList.size()
                                            ? static_cast<int>(
                                                player.boneList[i]
                                                )
                                            : -1,
                                            static_cast<unsigned long long>(
                                                ptr
                                                )
                                        );
                                    }

                                    ImGui::TreePop();
                                }
                            }
                        }

                        ImGui::EndChild();

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("camera"))
                    {
                        const auto& matrixDebug = camera.getMatrixActivityDebug();

                        const bool fpsCameraValid = Utils::valid_pointer(camera.fpsCamera);
                        const bool opticCameraValid = Utils::valid_pointer(camera.opticCamera);
                        const bool fpsMatrixPtrValid = Utils::valid_pointer(camera.fpsMatrixAddr);
                        const bool opticMatrixPtrValid = Utils::valid_pointer(camera.opticMatrixAddr);

                        const bool fpsRawValid = DebugMatrixLooksValid(camera.g_viewMatrixRAW);
                        const bool fpsTransValid = DebugMatrixLooksValid(camera.g_viewMatrix);
                        const bool opticRawValid = DebugMatrixLooksValid(camera.g_viewMatrixOpticRAW);
                        const bool opticTransValid = DebugMatrixLooksValid(camera.g_viewMatrixOptic);

                        ImGui::Text("Camera Debug");
                        ImGui::Separator();

                        if (ImGui::Button("Refresh Camera Pointers"))
                        {
                            camera.getCameraPtrs();
                            camera.getMatrixPtrs();
                        }

                        ImGui::SameLine();

                        if (ImGui::Button("Clear Camera Cache"))
                        {
                            camera.clearCache();
                        }

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Main State");

                        DebugTextBool("Camera Inited", camera.initedCamera);
                        DebugTextBool("Raid Started", mainGame.checkIfRaidStarted());
                        DebugTextBool("mainGame.localIsScoped", mainGame.localIsScoped);
                        DebugTextBool("camera.localmpCamera / Using Optic Matrix", camera.localmpCamera);

                        if (camera.localmpCamera)
                            ImGui::Text("Active Matrix: OPTIC");
                        else
                            ImGui::Text("Active Matrix: FPS");

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Camera Pointers");

                        DebugTextPtr("fpsCamera", camera.fpsCamera);
                        DebugTextPtr("opticCamera", camera.opticCamera);
                        DebugTextPtr("fpsMatrixAddr", camera.fpsMatrixAddr);
                        DebugTextPtr("opticMatrixAddr", camera.opticMatrixAddr);
                        DebugTextPtr("cameraEntity", camera.cameraEntity);
                        DebugTextPtr("opticCameraMatrix", camera.opticCameraMatrix);

                        ImGui::Spacing();

                        DebugTextBool("fpsCamera Valid", fpsCameraValid);
                        DebugTextBool("opticCamera Valid", opticCameraValid);
                        DebugTextBool("fpsMatrixAddr Valid", fpsMatrixPtrValid);
                        DebugTextBool("opticMatrixAddr Valid", opticMatrixPtrValid);

                        const bool allCameraPtrsReady =
                            fpsCameraValid &&
                            opticCameraValid &&
                            fpsMatrixPtrValid &&
                            opticMatrixPtrValid;

                        DebugTextBool("All Camera Pointers Ready", allCameraPtrsReady);

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Camera Values");

                        ImGui::Text("gameFOV: %.3f", camera.gameFOV);
                        ImGui::Text("gameAspect: %.3f", camera.gameAspect);

                        DebugTextBool(
                            "FOV Looks Valid",
                            std::isfinite(camera.gameFOV) && camera.gameFOV > 1.0f && camera.gameFOV < 180.0f
                        );

                        DebugTextBool(
                            "Aspect Looks Valid",
                            std::isfinite(camera.gameAspect) && camera.gameAspect > 0.1f && camera.gameAspect < 10.0f
                        );

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Matrix Activity Debug");

                        DebugTextBool("Local Scoped", matrixDebug.localScoped);

                        ImGui::Spacing();

                        DebugTextBool("FPS Matrix Valid", matrixDebug.fpsMatrixValid);
                        DebugTextBool("Optic Matrix Valid", matrixDebug.opticMatrixValid);

                        ImGui::Spacing();

                        DebugTextBool("Optic Matrix Changed", matrixDebug.opticMatrixChanged);
                        DebugTextBool("Optic Matrix Active", matrixDebug.opticMatrixActive);
                        DebugTextBool("Using Optic Matrix", matrixDebug.usingOpticMatrix);

                        ImGui::Spacing();

                        ImGui::Text("activityTick: %d", matrixDebug.activityTick);
                        ImGui::Text("noChangeSamples: %d", matrixDebug.noChangeSamples);
                        ImGui::Text("opticMatrixDiff: %.8f", matrixDebug.opticMatrixDiff);

                        ImGui::Spacing();

                        if (matrixDebug.localScoped && matrixDebug.opticMatrixActive)
                        {
                            ImGui::Text("Decision: scoped + optic matrix active = OPTIC");
                        }
                        else if (matrixDebug.localScoped && !matrixDebug.opticMatrixActive)
                        {
                            ImGui::Text("Decision: scoped but optic matrix static = FPS");
                        }
                        else
                        {
                            ImGui::Text("Decision: not scoped = FPS");
                        }

                        ImGui::Spacing();

                        if (matrixDebug.localScoped && matrixDebug.opticMatrixActive && !camera.localmpCamera)
                        {
                            ImGui::Text("WARNING: Optic matrix active but camera.localmpCamera is FALSE");
                        }

                        if (!matrixDebug.opticMatrixActive && camera.localmpCamera)
                        {
                            ImGui::Text("WARNING: camera.localmpCamera is TRUE but optic matrix is not active");
                        }

                        if (!matrixDebug.fpsMatrixValid)
                        {
                            ImGui::Text("WARNING: FPS matrix invalid");
                        }

                        if (!matrixDebug.opticMatrixValid)
                        {
                            ImGui::Text("WARNING: Optic matrix invalid");
                        }

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Matrix Validation");

                        DebugTextBool("FPS RAW Valid", fpsRawValid);
                        DebugTextBool("FPS Transposed Valid", fpsTransValid);
                        DebugTextBool("Optic RAW Valid", opticRawValid);
                        DebugTextBool("Optic Transposed Valid", opticTransValid);

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Matrices");

                        DebugMatrixSummary("fpsCamera RAW", camera.g_viewMatrixRAW);
                        DebugMatrixSummary("fpsCamera Transposed", camera.g_viewMatrix);
                        DebugMatrixSummary("opticCamera RAW", camera.g_viewMatrixOpticRAW);
                        DebugMatrixSummary("opticCamera Transposed", camera.g_viewMatrixOptic);

                        ImGui::Spacing();
                        ImGui::Separator();

                        ImGui::Text("Closest Player");

                        DebugTextPtr("closestPlayer", camera.closestPlayer);
                        ImGui::Text("closestPlayerDist: %.2f", camera.closestPlayerDist);

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("aim"))
                    {
                        
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("features"))
                    {

                        ImGui::SeparatorText("Exfils");

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Loot"))
                    {
                        const std::vector<LootList> cacheLoot = Loot.getCacheLoot();

                        size_t pendingCount = 0;
                        size_t failedCount = 0;
                        size_t successfulCount = 0;

                        size_t validPositionCount = 0;
                        size_t invalidPositionCount = 0;

                        size_t itemCount = 0;
                        size_t questItemCount = 0;
                        size_t containerCount = 0;
                        size_t corpseCount = 0;
                        size_t airdropCount = 0;
                        size_t wantedCount = 0;

                        for (const auto& item : cacheLoot)
                        {
                            if (item.pendingResolve)
                            {
                                ++pendingCount;
                                continue;
                            }

                            if (item.failed)
                            {
                                ++failedCount;
                                continue;
                            }

                            ++successfulCount;

                            if (item.hasValidPosition)
                                ++validPositionCount;
                            else
                                ++invalidPositionCount;

                            if (item.isItem)
                                ++itemCount;

                            if (item.isQuestItem)
                                ++questItemCount;

                            if (item.isContainer)
                                ++containerCount;

                            if (item.isCorpse)
                                ++corpseCount;

                            if (item.isAirdrop)
                                ++airdropCount;

                            if (item.wanted)
                                ++wantedCount;
                        }

                        const bool lootListPValid =
                            Utils::valid_pointer(Loot.lootListP);

                        const bool lootListPtrValid =
                            Utils::valid_pointer(Loot.lootListPtr);

                        const ImVec4 goodColour{
                            0.25f,
                            0.90f,
                            0.25f,
                            1.00f
                        };

                        const ImVec4 badColour{
                            0.95f,
                            0.25f,
                            0.25f,
                            1.00f
                        };

                        const ImVec4 warningColour{
                            0.95f,
                            0.75f,
                            0.20f,
                            1.00f
                        };

                        // pointers

                        ImGui::SeparatorText("Main Pointers");

                        if (ImGui::BeginTable(
                            "##loot_main_pointers",
                            3,
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp))
                        {
                            ImGui::TableSetupColumn("Pointer");
                            ImGui::TableSetupColumn("Address");
                            ImGui::TableSetupColumn("State");
                            ImGui::TableHeadersRow();

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("lootListP");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text(
                                "0x%llX",
                                static_cast<unsigned long long>(Loot.lootListP)
                            );

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(
                                lootListPValid ? goodColour : badColour,
                                lootListPValid ? "Valid" : "Invalid"
                            );

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("lootListPtr");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text(
                                "0x%llX",
                                static_cast<unsigned long long>(Loot.lootListPtr)
                            );

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(
                                lootListPtrValid ? goodColour : badColour,
                                lootListPtrValid ? "Valid" : "Invalid"
                            );

                            ImGui::EndTable();
                        }

                        // cache summary

                        ImGui::Spacing();
                        ImGui::SeparatorText("Cache Summary");

                        if (ImGui::BeginTable(
                            "##loot_cache_summary",
                            5,
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchSame))
                        {
                            ImGui::TableSetupColumn("Live Count");
                            ImGui::TableSetupColumn("Cached");
                            ImGui::TableSetupColumn("Pending");
                            ImGui::TableSetupColumn("Successful");
                            ImGui::TableSetupColumn("Failed");
                            ImGui::TableHeadersRow();

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("%ld", Loot.lootCount);

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%zu", cacheLoot.size());

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextColored(
                                pendingCount == 0 ? goodColour : warningColour,
                                "%zu",
                                pendingCount
                            );

                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextColored(
                                goodColour,
                                "%zu",
                                successfulCount
                            );

                            ImGui::TableSetColumnIndex(4);
                            ImGui::TextColored(
                                failedCount == 0 ? goodColour : badColour,
                                "%zu",
                                failedCount
                            );

                            ImGui::EndTable();
                        }

                        if (Loot.lootCount > 0)
                        {
                            const float cacheRatio = std::clamp(
                                static_cast<float>(cacheLoot.size()) /
                                static_cast<float>(Loot.lootCount),
                                0.0f,
                                1.0f
                            );

                            char overlay[64]{};

                            std::snprintf(
                                overlay,
                                sizeof(overlay),
                                "%zu / %ld cached",
                                cacheLoot.size(),
                                Loot.lootCount
                            );

                            ImGui::ProgressBar(
                                cacheRatio,
                                ImVec2(-FLT_MIN, 0.0f),
                                overlay
                            );
                        }

                        // resolved summary

                        ImGui::Spacing();
                        ImGui::SeparatorText("Resolved Types");

                        if (ImGui::BeginTable(
                            "##loot_type_summary",
                            4,
                            ImGuiTableFlags_Borders |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchSame))
                        {
                            ImGui::TableSetupColumn("Type");
                            ImGui::TableSetupColumn("Count");
                            ImGui::TableSetupColumn("Type");
                            ImGui::TableSetupColumn("Count");

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("Loose Items");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%zu", itemCount);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted("Quest Items");

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%zu", questItemCount);

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("Containers");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%zu", containerCount);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted("Corpses");

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%zu", corpseCount);

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("Airdrops");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%zu", airdropCount);

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted("Wanted");

                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%zu", wantedCount);

                            ImGui::TableNextRow();

                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("Valid Positions");

                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextColored(
                                goodColour,
                                "%zu",
                                validPositionCount
                            );

                            ImGui::TableSetColumnIndex(2);
                            ImGui::TextUnformatted("Invalid Positions");

                            ImGui::TableSetColumnIndex(3);
                            ImGui::TextColored(
                                invalidPositionCount == 0
                                ? goodColour
                                : warningColour,
                                "%zu",
                                invalidPositionCount
                            );

                            ImGui::EndTable();
                        }

                        ImGui::Spacing();

                        // Pending entries

                        if (ImGui::CollapsingHeader(
                            "Pending Cache Entries",
                            pendingCount > 0
                            ? ImGuiTreeNodeFlags_DefaultOpen
                            : 0))
                        {
                            if (pendingCount == 0)
                            {
                                ImGui::TextColored(
                                    goodColour,
                                    "No pending loot entries."
                                );
                            }
                            else if (ImGui::BeginTable(
                                "##pending_loot_entries",
                                6,
                                ImGuiTableFlags_Borders |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp,
                                ImVec2(0.0f, 260.0f)))
                            {
                                ImGui::TableSetupScrollFreeze(0, 1);

                                ImGui::TableSetupColumn(
                                    "Instance",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    125.0f
                                );

                                ImGui::TableSetupColumn("Class");
                                ImGui::TableSetupColumn("Object Name");

                                ImGui::TableSetupColumn(
                                    "Attempt",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    75.0f
                                );

                                ImGui::TableSetupColumn(
                                    "Position",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    80.0f
                                );

                                ImGui::TableSetupColumn(
                                    "Last Failure",
                                    ImGuiTableColumnFlags_WidthStretch,
                                    2.0f
                                );

                                ImGui::TableHeadersRow();

                                for (const auto& item : cacheLoot)
                                {
                                    if (!item.pendingResolve)
                                        continue;

                                    ImGui::PushID(
                                        reinterpret_cast<const void*>(
                                            static_cast<uintptr_t>(item.instance)
                                            )
                                    );

                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text(
                                        "0x%llX",
                                        static_cast<unsigned long long>(item.instance)
                                    );

                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(
                                        item.m_objectClassName.empty()
                                        ? "<unknown>"
                                        : item.m_objectClassName.c_str()
                                    );

                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::TextUnformatted(
                                        item.gameObjectName.empty()
                                        ? "<unknown>"
                                        : item.gameObjectName.c_str()
                                    );

                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text(
                                        "%u / 20",
                                        static_cast<unsigned>(item.resolveAttempts)
                                    );

                                    ImGui::TableSetColumnIndex(4);
                                    ImGui::TextColored(
                                        item.hasValidPosition
                                        ? goodColour
                                        : warningColour,
                                        item.hasValidPosition
                                        ? "Valid"
                                        : "Invalid"
                                    );

                                    ImGui::TableSetColumnIndex(5);
                                    ImGui::TextWrapped(
                                        item.failureReason.empty()
                                        ? "<no reason>"
                                        : item.failureReason.c_str()
                                    );

                                    ImGui::PopID();
                                }

                                ImGui::EndTable();
                            }
                        }

                        // failed entries

                        if (ImGui::CollapsingHeader(
                            "Failed Cache Entries",
                            failedCount > 0
                            ? ImGuiTreeNodeFlags_DefaultOpen
                            : 0))
                        {
                            if (failedCount == 0)
                            {
                                ImGui::TextColored(
                                    goodColour,
                                    "No failed loot entries."
                                );
                            }
                            else if (ImGui::BeginTable(
                                "##failed_loot_entries",
                                6,
                                ImGuiTableFlags_Borders |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp,
                                ImVec2(0.0f, 260.0f)))
                            {
                                ImGui::TableSetupScrollFreeze(0, 1);

                                ImGui::TableSetupColumn(
                                    "Instance",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    125.0f
                                );

                                ImGui::TableSetupColumn("Class");
                                ImGui::TableSetupColumn("Object Name");

                                ImGui::TableSetupColumn(
                                    "Attempts",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    75.0f
                                );

                                ImGui::TableSetupColumn(
                                    "Position",
                                    ImGuiTableColumnFlags_WidthFixed,
                                    80.0f
                                );

                                ImGui::TableSetupColumn(
                                    "Failure Reason",
                                    ImGuiTableColumnFlags_WidthStretch,
                                    2.0f
                                );

                                ImGui::TableHeadersRow();

                                for (const auto& item : cacheLoot)
                                {
                                    if (!item.failed)
                                        continue;

                                    ImGui::PushID(
                                        reinterpret_cast<const void*>(
                                            static_cast<uintptr_t>(item.instance)
                                            )
                                    );

                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text(
                                        "0x%llX",
                                        static_cast<unsigned long long>(item.instance)
                                    );

                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(
                                        item.m_objectClassName.empty()
                                        ? "<unknown>"
                                        : item.m_objectClassName.c_str()
                                    );

                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::TextUnformatted(
                                        item.gameObjectName.empty()
                                        ? "<unknown>"
                                        : item.gameObjectName.c_str()
                                    );

                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text(
                                        "%u",
                                        static_cast<unsigned>(item.resolveAttempts)
                                    );

                                    ImGui::TableSetColumnIndex(4);
                                    ImGui::TextColored(
                                        item.hasValidPosition
                                        ? goodColour
                                        : badColour,
                                        item.hasValidPosition
                                        ? "Valid"
                                        : "Invalid"
                                    );

                                    ImGui::TableSetColumnIndex(5);
                                    ImGui::TextWrapped(
                                        item.failureReason.empty()
                                        ? "<no reason>"
                                        : item.failureReason.c_str()
                                    );

                                    ImGui::PopID();
                                }

                                ImGui::EndTable();
                            }
                        }

                        // airdrop

                        if (ImGui::CollapsingHeader("Airdrop Entries"))
                        {
                            if (airdropCount == 0)
                            {
                                ImGui::TextUnformatted(
                                    "No successfully resolved airdrops currently cached."
                                );
                            }
                            else if (ImGui::BeginTable(
                                "##airdrop_entries",
                                6,
                                ImGuiTableFlags_Borders |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_Resizable |
                                ImGuiTableFlags_SizingStretchProp))
                            {
                                ImGui::TableSetupColumn("Instance");
                                ImGui::TableSetupColumn("Position State");
                                ImGui::TableSetupColumn("Distance");
                                ImGui::TableSetupColumn("X");
                                ImGui::TableSetupColumn("Y");
                                ImGui::TableSetupColumn("Z");
                                ImGui::TableHeadersRow();

                                for (const auto& item : cacheLoot)
                                {
                                    if (item.pendingResolve || item.failed)
                                        continue;

                                    if (!item.isAirdrop)
                                        continue;

                                    ImGui::TableNextRow();

                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::Text(
                                        "0x%llX",
                                        static_cast<unsigned long long>(item.instance)
                                    );

                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextColored(
                                        item.hasValidPosition
                                        ? goodColour
                                        : warningColour,
                                        item.hasValidPosition
                                        ? "Valid"
                                        : "Awaiting Update"
                                    );

                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("%d", item.distance);

                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%.2f", item.worldLocation.x);

                                    ImGui::TableSetColumnIndex(4);
                                    ImGui::Text("%.2f", item.worldLocation.y);

                                    ImGui::TableSetColumnIndex(5);
                                    ImGui::Text("%.2f", item.worldLocation.z);
                                }

                                ImGui::EndTable();
                            }
                        }

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
                ImGui::EndTabItem();
            }
           if (ImGui::BeginTabItem("Map Settings"))
           {
               if (!mainGame.selectedLocation.empty())
               {
                   ImGui::TextUnformatted("The below information is for debug / map position corrections, they don't stick!");
                   ImGui::Separator();

                   ImGui::TextUnformatted("Local Player World Position");

                   ImGui::Text(
                       "x: %.2f  y: %.2f  z: %.2f",
                       mainGame.localLocation.x,
                       mainGame.localLocation.y,
                       mainGame.localLocation.z
                   );

                   ImGui::Separator();

                   ImGui::Text("Selected map: %s", mainGame.selectedLocation.c_str());

                   ImGui::Spacing();

                   ImGui::TextUnformatted("Map Position Correction");
                   ImGui::SameLine();

                   ImGui::TextDisabled("(?)");
                   if (ImGui::IsItemHovered())
                   {
                       ImGui::BeginTooltip();
                       ImGui::TextUnformatted("Step controls:");
                       ImGui::BulletText("Click +/- to change by 0.1");
                       ImGui::BulletText("Hold Ctrl and click +/- to change by 1.0");
                       ImGui::EndTooltip();
                   }

                   ImGui::PushItemWidth(120.0f);

                   ImGui::InputFloat(
                       "Config X",
                       &currentMap::configX,
                       0.1f,
                       1.0f,
                       "%.2f"
                   );

                   ImGui::InputFloat(
                       "Config Y",
                       &currentMap::configY,
                       0.1f,
                       1.0f,
                       "%.2f"
                   );

                   ImGui::InputFloat(
                       "Config Scale",
                       &currentMap::configScale,
                       0.1f,
                       1.0f,
                       "%.2f"
                   );

                   //prevent scale going to 0 or negative
                   if (currentMap::configScale < 0.1f)
                       currentMap::configScale = 0.1f;

                   ImGui::PopItemWidth();

                   ImGui::Separator();

                   ImGui::Text(
                       "Current config: X %.2f | Y %.2f | Scale %.2f",
                       currentMap::configX,
                       currentMap::configY,
                       currentMap::configScale
                   );

                   ImGui::Text(
                       "Current Loaded Map : %s (X %.2f | Y %.2f)",
                       currentMap::mapPathName.c_str(),
                       currentMap::mapSizeX,
                       currentMap::mapSizeY
                   );
               }
               else
               {
                   ImGui::TextUnformatted("NOTE : Only visible when in raid");
               }

               ImGui::EndTabItem();
           }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();

}

static void renderLeftIcons()
{
    //icons
    std::string followIcon = ICON_FK_STREET_VIEW;

    // view port
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - viewport->Size.x + 10.f), 10.f });

    if (ImGui::ButtonMenu(followIcon.c_str(), ImVec2(40, 40), ImVec2(15, 10)))
    {
        mapGlobals::followLocal = !mapGlobals::followLocal;
        mapGlobals::focusPoint = { 0.f, 0.f, 0.f };

    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("Follow Toggle");
            ImGui::EndTooltip();
        }
    }
}

static void renderMenuIcons()
{
    // Icons Menu

    std::string settingIcon = ICON_FK_COGS;
    std::string fuserIcon = ICON_FK_TELEVISION;
    std::string filterIcon = ICON_FK_FILTER;
    std::string makcuIcon = ICON_FK_CROSSHAIRS;
    std::string questsIcon = ICON_FK_FILES_O;
    std::string watchlistIcon = ICON_FK_USERS;


    std::string widgetExitIcon = ICON_FK_SIGN_OUT;
    std::string widgetLootIcon = ICON_FK_CUBES;
    std::string widgetPlayersIcon = ICON_FK_USERS;
    std::string widgetDebugIcon = ICON_FK_STETHOSCOPE;

    // view port
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 10.f });

    // Settings Icon
    if (ImGui::ButtonMenu(settingIcon.c_str(), ImVec2(40, 40), ImVec2(60, 10)))
    {
        appMenu::appSettings = !appMenu::appSettings;
        closeSettingWindows("settings");
    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("Settings");
            ImGui::EndTooltip();
        }
    }

    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 55.f });
    if (ImGui::ButtonMenu(fuserIcon.c_str(), ImVec2(40, 40), ImVec2(22, 10))) {
        appMenu::appFuser = !appMenu::appFuser;
        closeSettingWindows("fuser");
    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("ESP");
            ImGui::EndTooltip();
        }
    }


    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 100.f });
    if (ImGui::ButtonMenu(makcuIcon.c_str(), ImVec2(40, 40), ImVec2(15, 10))) {
        appMenu::appMakcu = !appMenu::appMakcu;
        closeSettingWindows("makcu");
    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("Makcu Settings");
            ImGui::EndTooltip();
        }
    }

    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 145.f });
    if (ImGui::ButtonMenu(filterIcon.c_str(), ImVec2(40, 40), ImVec2(17, 10))) {
        appMenu::appLootFilters = !appMenu::appLootFilters;
        closeSettingWindows("lootfilters");
    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("Loot Filters");
            ImGui::EndTooltip();
        }
    }

    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 190.f });
    if (ImGui::ButtonMenu(questsIcon.c_str(), ImVec2(40, 40), ImVec2(20, 10))) {
        appMenu::appQuests = !appMenu::appQuests;
        closeSettingWindows("quests");
    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("Quests");
            ImGui::EndTooltip();
        }
    }

    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 235.f });
    if (ImGui::ButtonMenu(watchlistIcon.c_str(), ImVec2(40, 40), ImVec2(20, 10))) {
        appMenu::appWatchList = !appMenu::appWatchList;
        closeSettingWindows("watchlist");
    }
    else
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::BeginTooltip();
            ImGui::Text("WatchList");
            ImGui::EndTooltip();
        }
    }



    //widgets


    ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 300.f });
    if (ImGui::ButtonMenu(widgetDebugIcon.c_str(), ImVec2(40, 40), ImVec2(15, 10))) { appMenu::widgetDebug = !appMenu::widgetDebug; }



    //should only display the other items below when in raid
    if (mainGame.localPlayerPtr)
    {

        ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 350.f });
        if (ImGui::ButtonMenu(widgetExitIcon.c_str(), ImVec2(40, 40), ImVec2(15, 10))) { appMenu::widgetExfil = !appMenu::widgetExfil; }

        ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 400.f });
        if (ImGui::ButtonMenu(widgetLootIcon.c_str(), ImVec2(40, 40), ImVec2(30, 10))) { appMenu::widgetTopLoot = !appMenu::widgetTopLoot; }

        ImGui::SetCursorPos(ImVec2{ (viewport->Size.x - 50.f), 450.f });
        if (ImGui::ButtonMenu(widgetPlayersIcon.c_str(), ImVec2(40, 40), ImVec2(30, 10))) { appMenu::widgetPlayers = !appMenu::widgetPlayers; }


    }

    if (appMenu::appSettings)
        renderMenuSettings(); // display settings menu screen
    if (appMenu::appLootFilters)
        renderLootFiltersMenu(); // display loot filters screen

    if (appMenu::widgetDebug)
        renderDebugWindow();

    if (appMenu::appFuser)
        renderFuserWindow();

    if (appMenu::appQuests)
        renderQuestsWindow();

    if (appMenu::appWatchList)
        watchListManager.RenderWindow();

    if (appMenu::appMakcu)
    {
        RenderMakcuWindow(
            &appMenu::appMakcu,
            globals::appWindowAlpha,
            []()
            {
                configManager.SaveConfig();
            }
        );
    }



}



//This is where we render certain screens depending on conditions and selections/inputs
static void renderMainScreen()
{


    // Viewport Info
    static ImGuiWindowFlags flags = ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDecoration;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y));

    if (ImGui::Begin("MainScreen", NULL, flags))
    {
        const char* Text = "";

        const bool dmaConnected = memoryGlobals::dmaConnected.load(
            std::memory_order_acquire
        );

        const bool processFound = memoryGlobals::processFound.load(
            std::memory_order_acquire
        );

        const bool working = mem.IsInitRunning();
        const bool stopping = mem.IsDisconnectRequested();

        if (stopping)
        {
            Text = "Disconnecting DMA";
        }
        else if (working && !dmaConnected)
        {
            Text = "Connecting to DMA";
        }
        else if (!dmaConnected)
        {
            Text = "Waiting for DMA Connection";
        }
        else if (!processFound)
        {
            Text = "Waiting for Process";
        }
        else if (!appGlobals::runRadar.load(std::memory_order_acquire))
        {
            Text = "Waiting for Raid Start";
        }
        else
        {
            Text = "Connected";
        }


        //display text if not in raid or runRadar is not set true
        if (!appGlobals::runRadar.load(std::memory_order_acquire))
        {
            ImVec2 centerScreen = viewport->GetWorkCenter();

            DrawRadarMainText(centerScreen.x, centerScreen.y, { 1,0,0,1 }, Text);
            DrawRadarSubText(centerScreen.x, centerScreen.y + 45, { 1,0,0,1 }, globals::radarSubText.c_str());

            setCurrentMapSpecs = false;

        }
        else
        {
            // consider in raid? render what we only have access to in raid!
            renderMapDetails();

            //render what we want on map as runRadar is true
            drawLocalPlayer();
            drawPlayers();
            drawExfils();
            drawGrenades();
            drawLoot();

            drawQuests();

            drawWidgetPlayers();
            drawWidgetExfils();
            drawWidgetTopLoot();


            renderLeftIcons();


        }

        renderMenuIcons();
        renderBottomInfo();
    }
    ImGui::End();

}

static void load_styles()
{
    ImVec4* colors = ImGui::GetStyle().Colors;
    {
        colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 255.00f);

        colors[ImGuiCol_FrameBg] = ImColor(11, 11, 11, 255);
        colors[ImGuiCol_FrameBgHovered] = ImColor(11, 11, 11, 255);

        colors[ImGuiCol_Button] = ImColor(0, 0, 255, 255);
        colors[ImGuiCol_ButtonActive] = ImColor(0, 0, 255, 255);
        colors[ImGuiCol_ButtonHovered] = ImColor(0, 0, 255, 100);

        colors[ImGuiCol_TextDisabled] = ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
    }

    ImGuiStyle* style = &ImGui::GetStyle();
    {
        style->WindowPadding = ImVec2(6, 6);
        style->WindowBorderSize = 1.f;
        style->WindowRounding = 5.f;

        style->FramePadding = ImVec2(8, 6);
        style->FrameRounding = 3.f;
        style->FrameBorderSize = 1.f;
    }
}


bool renderThread()
{
    bool done = false;
    bool doOnce = false;

    // Create application window
    //ImGui_ImplWin32_EnableDpiAwareness();
    std::wstring windowTitle = L"MeatyEFT - " + std::wstring(globals::appVersion.begin(), globals::appVersion.end());

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, windowTitle.c_str(), nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, windowTitle.c_str(), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }



    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    io.IniFilename = "INImeatyEFT.ini";

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!doOnce)
    {
        load_styles();
        doOnce = true;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX9_Init(g_pd3dDevice);

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != nullptr);

    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\Calibri.ttf", 12.0f, NULL, io.Fonts->GetGlyphRangesDefault());

    // Other Fonts
    //io.Fonts->AddFontFromMemoryTTF((void*)Font, sizeof(Font), 16.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());

    // Font Awesome
    float iconFontSize = 24.f; //24
    static const ImWchar icons_ranges[] = { ICON_MIN_FK, ICON_MAX_16_FK, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_FK, iconFontSize, &icons_config, icons_ranges);




    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    // Main loop

    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle lost D3D9 device
        if (g_DeviceLost)
        {
            HRESULT hr = g_pd3dDevice->TestCooperativeLevel();
            if (hr == D3DERR_DEVICELOST)
            {
                ::Sleep(10);
                continue;
            }
            if (hr == D3DERR_DEVICENOTRESET)
                ResetDevice();
            g_DeviceLost = false;
        }

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            g_d3dpp.BackBufferWidth = g_ResizeWidth;
            g_d3dpp.BackBufferHeight = g_ResizeHeight;
            g_ResizeWidth = g_ResizeHeight = 0;
            ResetDevice();
        }

        



        // Start the Dear ImGui frame
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Our app function for rendering whats on screen
        renderMainScreen();

        // Rendering
        ImGui::EndFrame();

        ImGui::GetIO().FontGlobalScale = globals::appTextScale;


        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        D3DCOLOR clear_col_dx = D3DCOLOR_RGBA((int)(clear_color.x * clear_color.w * 255.0f), (int)(clear_color.y * clear_color.w * 255.0f), (int)(clear_color.z * clear_color.w * 255.0f), (int)(clear_color.w * 255.0f));
        g_pd3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear_col_dx, 1.0f, 0);
        if (g_pd3dDevice->BeginScene() >= 0)
        {
            ImGui::Render();

            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

            g_pd3dDevice->EndScene();
        }

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        HRESULT result = g_pd3dDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST)
            g_DeviceLost = true;

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    // Cleanup
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
    if ((g_pD3D = Direct3DCreate9(D3D_SDK_VERSION)) == nullptr)
        return false;

    // Create the D3DDevice
    ZeroMemory(&g_d3dpp, sizeof(g_d3dpp));
    g_d3dpp.Windowed = TRUE;
    g_d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    g_d3dpp.BackBufferFormat = D3DFMT_UNKNOWN; // Need to use an explicit format with alpha if needing per-pixel alpha composition.
    g_d3dpp.EnableAutoDepthStencil = TRUE;
    g_d3dpp.AutoDepthStencilFormat = D3DFMT_D16;
    g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;           // Present with vsync
    //g_d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;   // Present without vsync, maximum unthrottled framerate
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &g_d3dpp, &g_pd3dDevice) < 0)
        return false;

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    if (g_pD3D) { g_pD3D->Release(); g_pD3D = nullptr; }
}

void ResetDevice()
{
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_pd3dDevice->Reset(&g_d3dpp);
    if (hr == D3DERR_INVALIDCALL)
        IM_ASSERT(0);
    ImGui_ImplDX9_CreateDeviceObjects();
}

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_DPICHANGED:
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DpiEnableScaleViewports)
        {
            //const int dpi = HIWORD(wParam);
            //printf("WM_DPICHANGED to %d (%.0f%%)\n", dpi, (float)dpi / 96.0f * 100.0f);
            const RECT* suggested_rect = (RECT*)lParam;
            ::SetWindowPos(hWnd, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}