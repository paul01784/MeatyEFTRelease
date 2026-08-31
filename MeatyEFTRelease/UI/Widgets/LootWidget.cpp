#include "LootWidget.h"

#include "../includes.h"
#include "../config.h"
#include "../globals.h"
#include "../maps.h"
#include "../menuLayout.h"
#include "../../Tarkov/GameWorld/Loot/Loot.h"
#include "../../Tarkov/GameWorld/Loot/WishList.h"
#include "../../Tarkov/GameWorld/QuestManager.h"
#include "../../Web/TarkovDev/TarkovDevClient.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <unordered_map>

namespace uiWidgets
{
static char filterName[128] = "";

static void customChildWindowWithTitle(const char* title, const ImVec2& size)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float titleHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);

    drawList->AddRect(position, ImVec2(position.x + size.x, position.y + size.y), borderColor);

    const ImVec2 titlePosition = ImVec2(
        position.x + ImGui::GetStyle().FramePadding.x,
        position.y - titleHeight / 2.0f);
    const ImVec2 titleSize = ImGui::CalcTextSize(title);

    drawList->AddRectFilled(
        titlePosition,
        ImVec2(titlePosition.x + titleSize.x, titlePosition.y + titleHeight),
        ImGui::GetColorU32(ImGuiCol_WindowBg));

    ImGui::SetCursorScreenPos(ImVec2(position.x + 7.0f, position.y + 6.0f - titleHeight / 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.22f, 0.25f, 1.0f));
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
}

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
std::string toLower(const std::string& str) {
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
        std::string lowerSearchQuery = toLower(searchQuery);
        for (size_t i = 0; i < marketList.size(); ++i) {
            std::string lowerItemName = toLower(marketList[i].name);
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
static void DrawLootListInfoTooltip(const LootEntity& loot)
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
    ImGui::Text("corpseValue: %d", loot.getCorpseValue());

    ImGui::Separator();

    ImGui::Text("isItem: %s", loot.isItem() ? "true" : "false");
    ImGui::Text("isContainer: %s", loot.isContainer() ? "true" : "false");
    ImGui::Text("isQuestItem: %s", loot.isQuestItem() ? "true" : "false");
    ImGui::Text("isCorpse: %s", loot.isCorpse() ? "true" : "false");
    ImGui::Text("wanted: %s", loot.wanted ? "true" : "false");
    ImGui::Text("filterWanted: %s", loot.filterWanted ? "true" : "false");
    ImGui::Text("forceWanted: %s", loot.forceWanted ? "true" : "false");

    ImGui::Text("color: %.2f, %.2f, %.2f, %.2f",
        loot.color.x, loot.color.y, loot.color.z, loot.color.w);

    ImGui::EndTooltip();
}

static void BuildLootListDebugRows(
    std::vector<LootEntity>& lootCache,
    std::vector<LootEntity*>& normalLootRows,
    std::vector<LootEntity*>& questLootRows,
    std::vector<LootEntity*>& wantedRows)
{
    normalLootRows.clear();
    questLootRows.clear();
    wantedRows.clear();

    normalLootRows.reserve(lootCache.size());
    questLootRows.reserve(lootCache.size());
    wantedRows.reserve(lootCache.size());

    for (auto& loot : lootCache)
    {
        if (loot.isContainer())
            continue;

        if (loot.isCorpse())
            continue;

        if (loot.isQuestItem())
        {
            questLootRows.push_back(&loot);

            if (loot.wanted || loot.forceWanted)
                wantedRows.push_back(&loot);

            continue;
        }

        if (loot.isItem())
        {
            normalLootRows.push_back(&loot);

            if (loot.wanted || loot.forceWanted)
                wantedRows.push_back(&loot);
        }
    }
}

static void DrawLootListDebugTable(std::vector<LootEntity*>& rows, const char* tableId)
{
    if (!ImGui::BeginTable(tableId, 5,
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable))
    {
        return;
    }

    ImGui::TableSetupColumn("Wanted", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Force", ImGuiTableColumnFlags_WidthFixed, 46.0f);
    ImGui::TableSetupColumn("Short Name", ImGuiTableColumnFlags_WidthStretch, 220.0f);
    ImGui::TableSetupColumn("BSG ID", ImGuiTableColumnFlags_WidthStretch, 260.0f);
    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthFixed, 30.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < rows.size(); ++i)
    {
        LootEntity* loot = rows[i];
        if (!loot)
            continue;

        ImGui::TableNextRow();
        ImGui::PushID(loot);

        // Wanted is the final display state. It is intentionally read-only:
        // rule-owned entries cannot be manually changed.
        ImGui::TableSetColumnIndex(0);
        bool wantedTick = loot->wanted;
        ImGui::BeginDisabled();
        ImGui::Checkbox("##wanted", &wantedTick);
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(
                loot->filterWanted
                    ? "Wanted by an active loot rule"
                    : "Currently shown on the radar");
        }

        // Only items not selected by an active rule can be forced manually.
        ImGui::TableSetColumnIndex(1);
        bool forceWanted = loot->forceWanted;
        const bool forceLocked =
            loot->filterWanted || loot->pendingResolve || loot->failed;
        ImGui::BeginDisabled(forceLocked);
        if (ImGui::Checkbox("##forceWanted", &forceWanted))
        {
            Loot.setLootWanted(
                loot->instance,
                forceWanted,
                coloursGlobals::valueLootColour);
            loot->forceWanted = forceWanted;
            loot->wanted = forceWanted;
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                forceLocked
                    ? "Items selected by active rules cannot be force-changed"
                    : "Force this individual item to display");

        // Short Name
        ImGui::TableSetColumnIndex(2);
        const std::string shortName = TrimEFT(loot->shortName);
        const std::string longName = TrimEFT(loot->longName);

        ImGui::TextUnformatted(shortName.c_str());

        if (ImGui::IsItemHovered() && !longName.empty())
        {
            ImGui::SetTooltip("%s", longName.c_str());
        }

        // BSG ID
        ImGui::TableSetColumnIndex(3);
        const std::string bsgId = TrimEFT(loot->bsgId);
        ImGui::TextUnformatted(bsgId.c_str());

        // Info
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted("(i)");
        if (ImGui::IsItemHovered())
        {
            DrawLootListInfoTooltip(*loot);
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

static void DrawRaidLootWindow(std::vector<LootEntity>& lootCache)
{
    static char searchBuffer[128]{};
    static std::string selectedCategory;
    static std::unordered_map<std::string, std::vector<std::string>>
        categoriesByItemId;
    static std::uint64_t indexedMarketRevision =
        std::numeric_limits<std::uint64_t>::max();

    const std::uint64_t currentMarketRevision =
        marketListRevision.load(std::memory_order_acquire);

    if (indexedMarketRevision != currentMarketRevision)
    {
        categoriesByItemId.clear();
        categoriesByItemId.reserve(marketList.size());

        for (const gameItemList& item : marketList)
        {
            if (!item.bsgid.empty())
                categoriesByItemId.emplace(item.bsgid, item.bsgCategory);
        }

        indexedMarketRevision = currentMarketRevision;
    }

    std::vector<std::string> categoryOptions;

    for (const LootEntity& loot : lootCache)
    {
        if (!loot.isItem() || loot.isQuestItem() || loot.isContainer() || loot.isCorpse())
            continue;

        const auto categoriesIt = categoriesByItemId.find(loot.bsgId);
        if (categoriesIt == categoriesByItemId.end())
            continue;

        for (const std::string& category : categoriesIt->second)
        {
            if (!category.empty())
                categoryOptions.push_back(category);
        }
    }

    std::sort(categoryOptions.begin(), categoryOptions.end());
    categoryOptions.erase(
        std::unique(categoryOptions.begin(), categoryOptions.end()),
        categoryOptions.end());

    if (!selectedCategory.empty() &&
        !std::binary_search(
            categoryOptions.begin(),
            categoryOptions.end(),
            selectedCategory))
    {
        selectedCategory.clear();
    }

    ImGui::SetNextItemWidth(190.0f);
    ImGui::InputTextWithHint(
        "##raidLootSearch",
        "Search name or BSG ID...",
        searchBuffer,
        IM_ARRAYSIZE(searchBuffer));

    ImGui::SameLine();
    ImGui::TextUnformatted("Category");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(140.0f);
    const char* categoryPreview = selectedCategory.empty()
        ? "All categories"
        : selectedCategory.c_str();

    if (ImGui::BeginCombo("##raidLootCategory", categoryPreview))
    {
        if (ImGui::Selectable("All categories", selectedCategory.empty()))
            selectedCategory.clear();

        for (const std::string& category : categoryOptions)
        {
            const bool selected = selectedCategory == category;
            if (ImGui::Selectable(category.c_str(), selected))
                selectedCategory = category;
        }

        ImGui::EndCombo();
    }

    const std::string lowerSearch = toLower(TrimEFT(searchBuffer));

    struct LootGroup
    {
        std::string bsgId;
        std::string shortName;
        std::string longName;
        std::vector<LootEntity*> items;
        LootEntity* representative = nullptr;
        uint64_t forcedInstance = 0;
        bool wanted = false;
        bool filterWanted = false;
        bool forceWanted = false;
        bool canFocus = false;
    };

    std::vector<LootGroup> groups;
    groups.reserve(lootCache.size());
    std::unordered_map<std::string, size_t> groupIndexByKey;
    groupIndexByKey.reserve(lootCache.size());

    for (LootEntity& loot : lootCache)
    {
        if (!loot.isItem() || loot.isQuestItem() || loot.isContainer() || loot.isCorpse())
            continue;

        const std::string shortName = TrimEFT(loot.shortName);
        const std::string longName = TrimEFT(loot.longName);
        const std::string bsgId = TrimEFT(loot.bsgId);
        const auto categoriesIt = categoriesByItemId.find(bsgId);
        const std::vector<std::string>* categories =
            categoriesIt == categoriesByItemId.end()
            ? nullptr
            : &categoriesIt->second;

        if (!lowerSearch.empty() &&
            toLower(shortName).find(lowerSearch) == std::string::npos &&
            toLower(longName).find(lowerSearch) == std::string::npos &&
            toLower(bsgId).find(lowerSearch) == std::string::npos)
        {
            continue;
        }

        if (!selectedCategory.empty() &&
            (!categories ||
                std::find(
                    categories->begin(),
                    categories->end(),
                    selectedCategory) == categories->end()))
        {
            continue;
        }

        const std::string groupKey = bsgId.empty()
            ? "instance:" + std::to_string(loot.instance)
            : bsgId;

        size_t groupIndex = 0;
        const auto existingGroup = groupIndexByKey.find(groupKey);

        if (existingGroup == groupIndexByKey.end())
        {
            groupIndex = groups.size();
            groupIndexByKey.emplace(groupKey, groupIndex);
            groups.push_back(LootGroup{
                bsgId,
                shortName,
                longName,
                {},
                &loot
            });
        }
        else
        {
            groupIndex = existingGroup->second;
        }

        LootGroup& group = groups[groupIndex];
        group.items.push_back(&loot);
        group.wanted = group.wanted || loot.wanted;
        group.filterWanted = group.filterWanted || loot.filterWanted;
        group.canFocus = group.canFocus ||
            (!loot.pendingResolve && !loot.failed && loot.hasValidPosition);

        if (loot.forceWanted)
        {
            group.forceWanted = true;
            group.forcedInstance = loot.instance;
        }
    }

    std::sort(
        groups.begin(),
        groups.end(),
        [](const LootGroup& left, const LootGroup& right)
        {
            const std::string& leftName = left.shortName.empty()
                ? left.longName
                : left.shortName;
            const std::string& rightName = right.shortName.empty()
                ? right.longName
                : right.shortName;
            return toLower(leftName) < toLower(rightName);
        });

    const float tableHeight =
        ImGui::GetFrameHeightWithSpacing() +
        (ImGui::GetTextLineHeightWithSpacing() * 10.0f) +
        6.0f;

    if (!ImGui::BeginTable(
        "##raidLootTable",
        3,
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_NoSavedSettings,
        ImVec2(0.0f, tableHeight)))
    {
        return;
    }

    ImGui::TableSetupColumn("Loot", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Wanted", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("##map", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableHeadersRow();

    for (size_t index = 0; index < groups.size(); ++index)
    {
        LootGroup& group = groups[index];

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(index));

        ImGui::TableSetColumnIndex(0);
        const std::string& displayName = group.shortName.empty()
            ? group.longName
            : group.shortName;
        ImGui::Text("%zux %s", group.items.size(), displayName.c_str());

        ImGui::TableSetColumnIndex(1);
        bool wanted = group.wanted;
        const bool canToggleWanted = group.forceWanted
            ? !group.filterWanted && group.forcedInstance != 0
            : !group.wanted && !group.filterWanted && group.canFocus;
        ImGui::BeginDisabled(!canToggleWanted);
        if (ImGui::Checkbox("##wanted", &wanted))
        {
            if (wanted)
            {
                Loot.focusClosestLootItem(
                    group.representative->instance,
                    group.bsgId,
                    coloursGlobals::valueLootColour);
            }
            else
            {
                Loot.setLootWanted(
                    group.forcedInstance,
                    false,
                    coloursGlobals::valueLootColour);
            }
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(
                group.filterWanted
                    ? "Items selected by active rules cannot be force-changed"
                    : group.wanted
                        ? "Wanted by an active rule"
                        : "Force the closest instance in this group to display");
        }

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(!group.canFocus);
        if (ImGui::SmallButton(ICON_FA_LOCATION_CROSSHAIRS))
        {
            const auto focusLocation = Loot.focusClosestLootItem(
                group.representative->instance,
                group.bsgId,
                coloursGlobals::valueLootColour);

            if (focusLocation)
            {
                mapGlobals::followLocal = false;
                mapGlobals::focusPoint = *focusLocation;
                mapGlobals::startLootFocusRipple(*focusLocation);
            }
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip(
                group.canFocus
                    ? "Center the map on the closest matching loot item"
                    : "This item does not have a valid raid position yet");
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

void renderRaidLootWidget()
{
    if (!appMenu::widgetLoot)
        return;

    ImGui::SetNextWindowPos(
        menuLayout::TopLeftWidgetPosition(),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 315.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (ImGui::Begin(
        "Loot",
        &appMenu::widgetLoot,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize))
    {
        std::vector<LootEntity> lootCache = Loot.getCacheLoot();
        DrawRaidLootWindow(lootCache);
    }
    ImGui::End();
}

// Variable to hold the selected ID
//static LootFilters currentLootFilter;
static long selectedLootFilterID = -1;
static LootFilters* currentLootFilter = nullptr;

void renderLootFiltersMenu()
{



    std::string windowNameMain = "Loot Filters";

    static ImGuiWindowFlags flagss = ImGuiWindowFlags_NoCollapse;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + 30.0f, viewport->Pos.y + 30.0f),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(ImVec2(930.0f, 720.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(760.0f, 560.0f),
        ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f)
    );
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    static bool addFilterPopupOpen = false;
    static bool addLootPopupOpen = false;



    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::appLootFilters, flagss))
    {
        constexpr float panelMargin = 10.0f;
        constexpr float topPanelY = 45.0f;
        constexpr float leftPanelWidth = 250.0f;
        constexpr float panelGap = 20.0f;
        constexpr float topPanelHeight = 355.0f;
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const float rightPanelX = panelMargin + leftPanelWidth + panelGap;
        const float rightPanelWidth = windowSize.x - rightPanelX - panelMargin;
        const float bottomPanelY = topPanelY + topPanelHeight + panelGap;
        const float bottomPanelHeight = windowSize.y - bottomPanelY - panelMargin;
        constexpr float leftContentX = 26.0f;
        constexpr float leftToggleX = 145.0f;
        constexpr float leftColourX = 226.0f;
        constexpr float leftValueX = 116.0f;
        constexpr float leftActionY = 235.0f;
        const float leftBottomActionY =
            topPanelY + topPanelHeight - 30.0f;
        constexpr float leftButtonWidth = 105.0f;
        constexpr float leftButtonGap = 8.0f;
        const float leftSecondButtonX = leftContentX + leftButtonWidth + leftButtonGap;

        //left top
        {
            //set position
            ImGui::SetCursorPos(ImVec2(panelMargin, topPanelY));

            // draw window frame
            customChildWindowWithTitle(" Display & price ", ImVec2(leftPanelWidth, topPanelHeight));

            //draw inside window
            ImGui::SetCursorPos(ImVec2(leftContentX, 60.0f));

            const auto lootToggleWithColour = [&](const char* label, const char* id, bool* value, const char* colourId, float* colour)
            {
                ImGui::SetCursorPosX(leftContentX);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(leftToggleX);
                const bool toggleChanged = ImGui::Checkbox(id, value);
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(leftColourX);
                const bool colourChanged = ImGui::ColorEdit4(
                    colourId,
                    colour,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs
                );
                return toggleChanged || colourChanged;
            };

            if (lootToggleWithColour("Show Quest Loot", "##showQuestLoot", &lootGlobals::enableQuestLoot, "##questcolour", (float*)&coloursGlobals::questColour))
                configManager.SaveConfig();
            if (lootToggleWithColour("Show WishList Loot", "##showWishListLoot", &lootGlobals::enableWishListLoot, "##wishlistcolour", (float*)&coloursGlobals::wishListColour))
                configManager.SaveConfig();
            if (lootToggleWithColour("Show Value Loot", "##showValueLoot", &lootGlobals::enableValueLoot, "##valuelistcolour", (float*)&coloursGlobals::valueLootColour))
                configManager.SaveConfig();

            ImGui::SetCursorPosX(leftContentX);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Show Loot Value");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(leftToggleX);
            if (ImGui::Checkbox("##showLootValue", &lootGlobals::showLootValue))
                configManager.SaveConfig();

            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(leftToggleX + 31.0f);
            ImGui::PushItemWidth(74.0f);
            if (ImGui::Combo("##LootValuePriceSource", &lootGlobals::lootValuePriceSource, "Market\0Trader\0")) configManager.SaveConfig();
            ImGui::PopItemWidth();

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Price source for loot labels and value filters. Market falls back to Trader when unavailable.");

            const auto lootValueSlider = [&](const char* label, const char* id, int* value)
            {
                ImGui::SetCursorPosX(leftContentX);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::SameLine();
                ImGui::SetCursorPosX(leftValueX);
                ImGui::SetNextItemWidth(110.0f);
                return ImGui::SliderInt(id, value, 0, 1000000, "%d");
            };

            if (lootValueSlider("Loot value", "##lootValueThreshold", &lootGlobals::valueLootFrom))
                configManager.SaveConfig();
            if (lootValueSlider("Equipment", "##equipmentValueThreshold", &lootGlobals::valueLootFromEquip))
                configManager.SaveConfig();
            

            ImGui::SetCursorPos(ImVec2(leftContentX, leftActionY));
            static bool containerPopupOpen = false;

            if (ImGui::Button("Containers", ImVec2(leftButtonWidth, 0)))
            {
                containerPopupOpen = true;
                ImGui::OpenPopup("Container Settings");
            }

            static bool categoryPopupOpen = false;
            const std::string categoryButtonLabel =
                "Categories (" +
                std::to_string(lootGlobals::selectedLootCategories.size()) +
                ")";

            ImGui::SetCursorPos(ImVec2(leftSecondButtonX, leftActionY));
            if (ImGui::Button(categoryButtonLabel.c_str(), ImVec2(leftButtonWidth, 0)))
            {
                categoryPopupOpen = true;
                ImGui::OpenPopup("Loot Category Settings");
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
                    { "Drawer",        &lootGlobals::drawDrawer },
                    { "Duffle",        &lootGlobals::drawDuffle },
                    { "Safe",          &lootGlobals::drawSafe },
                    { "Weapon Box",    &lootGlobals::drawWeaponBox },
                    { "Tech Crate",    &lootGlobals::drawTechCrate },
                    { "Ration Crate",  &lootGlobals::drawRationCrate },
                    { "Medical Crate", &lootGlobals::drawMedicalCrate },
                    { "Jacket",        &lootGlobals::drawJacket },
                    { "Med Package",   &lootGlobals::drawMedPackage },
                    { "Med Box",       &lootGlobals::drawMedBox },
                    { "Toolbox",       &lootGlobals::drawToolbox },
                    { "Grenade Box",   &lootGlobals::drawGrenadeBox },
                    { "Buried Stash",  &lootGlobals::drawBuriedStash },
                    { "Ground Cache",  &lootGlobals::drawGroundCache },
                    { "Wooden Crate",  &lootGlobals::drawWoodenCrate },
                    { "Suitcase",      &lootGlobals::drawSuitcase },
                    { "Ammo Box",      &lootGlobals::drawAmmoBox },
                    { "Dead Body",     &lootGlobals::drawDeadBody },
                    { "PC Block",      &lootGlobals::drawPCBlock },
                    { "Register",      &lootGlobals::drawRegister },
                    { "Airdrop",       &lootGlobals::drawAirDrops },
                    // { "Xmas Loot",   &Loot.drawXmas },
                };

                const int optionCount = sizeof(options) / sizeof(options[0]);
                const int splitIndex = (optionCount + 1) / 2;

                ImGui::Text("Container Settings");
                ImGui::Separator();

                ImGui::TextUnformatted("Colour");
                ImGui::SameLine();
                if (ImGui::ColorEdit4(
                    "##containercolour",
                    (float*)&coloursGlobals::containerColour,
                    ImGuiColorEditFlags_Float |
                    ImGuiColorEditFlags_NoInputs))
                {
                    configManager.SaveConfig();
                }

                ImGui::SameLine();
                if (ImGui::Checkbox("Hide searched", &lootGlobals::hideSearched))
                    configManager.SaveConfig();

                ImGui::SameLine();
                if (ImGui::Button("Disable All", ImVec2(120, 0)))
                {
                    for (int i = 0; i < optionCount; i++)
                        *options[i].value = false;

                    configManager.SaveConfig();
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
                            if (ImGui::Checkbox(options[i].label, options[i].value))
                                configManager.SaveConfig();
                        }

                        ImGui::TableSetColumnIndex(1);
                        for (int i = splitIndex; i < optionCount; i++)
                        {
                            if (ImGui::Checkbox(options[i].label, options[i].value))
                                configManager.SaveConfig();
                        }

                        ImGui::EndTable();
                    }

                    ImGui::EndChild();
                }

               

                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("Loot Category Settings", &categoryPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(520, 350), ImGuiCond_Always);

                ImGui::TextUnformatted("Category Colour");
                ImGui::SameLine();
                if (ImGui::ColorEdit4(
                    "##categorylootcolour",
                    (float*)&lootGlobals::categoryLootColour,
                    ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs))
                {
                    configManager.SaveConfig();
                }

                ImGui::SameLine();
                if (ImGui::Button("Clear All", ImVec2(100, 0)))
                {
                    lootGlobals::selectedLootCategories.clear();
                    configManager.SaveConfig();
                }

                ImGui::SameLine();
                ImGui::Text("%zu selected", lootGlobals::selectedLootCategories.size());

                struct LootCategoryGroup
                {
                    const char* name;
                    std::vector<const char*> categories;
                };

                static const std::vector<LootCategoryGroup> categoryGroups =
                {
                    { "Meds", {
                        "med supplies", "Medical supplies", "medical item", "meds", "medikit", "drug"
                    } },
                    { "Food", { "food" } },
                    { "Drink", { "drink" } },
                    { "Stims", { "stimulant" } },
                    { "Backpack", { "backpack" } },
                    { "Weapons", {
                        "assault carbine", "assault rifle", "ubgl", "weapon", "throwable weapon",
                        "shotgun", "sniper rifle", "smg", "rocket launcher", "marksman rifle",
                        "machinegun", "grenade launcher", "knife", "handgun", "revolver",
                        "volumetric throw weapon"
                    } },
                    { "Ammo", { "ammo", "ammo container", "rocket" } },
                    { "Attachments", {
                        "special scope", "sights", "assault scopes", "auxiliary mod", "barrel", "bipod",
                        "charging handle", "comb. muzzle device", "comb. tact device", "compact reflex sight",
                        "cylinder magazine", "essential mod", "flashhider", "foregrip", "functional mod",
                        "handguard", "mount", "magazine", "ironsight", "portable range finder",
                        "reflex sight", "scope", "receiver", "pistol grip", "silencer", "weapon mod",
                        "stock", "spring driven cylinder", "gas block", "muzzle device", "flashlight",
                        "thermal vision"
                    } },
                    { "Wearables", {
                        "gear mod", "headphones", "headwear", "night vision", "arm band", "armored equipment",
                        "armor", "chest rig", "face cover", "armor plate", "equipment", "vis. observ. device"
                    } },
                    { "Barter", {
                        "barter item", "household goods", "multitools", "building material", "tool", "battery",
                        "electronics", "jewelry", "lubricant", "other"
                    } },
                    { "Fuel", { "fuel" } },
                    { "Money", { "money" } },
                    { "Keys", { "keycard", "key", "mechanical key" } },
                    { "Quest Misc", {
                        "tapes", "notes", "info", "completable", "dialog item", "flyer", "map",
                        "mark of the unheard", "radio transmitter", "recorder"
                    } },
                    { "Repair Kits", { "repair kits" } },
                    { "Battle Pass", { "battle pass doc", "Battle Pass Document" } },
                    { "Others", {} }
                };

                auto normalizeCategoryName = [](const std::string& value)
                {
                    std::string normalized;
                    normalized.reserve(value.size());

                    for (const unsigned char character : value)
                    {
                        if (std::isalnum(character))
                            normalized.push_back(static_cast<char>(std::tolower(character)));
                    }

                    if (normalized.size() > 1 && normalized.back() == 's')
                        normalized.pop_back();

                    return normalized;
                };

                auto findCategory = [&normalizeCategoryName](const char* name) -> const gameCatList*
                {
                    const std::string normalizedName = normalizeCategoryName(name);
                    const auto it = std::find_if(
                        catList.begin(),
                        catList.end(),
                        [&normalizeCategoryName, &normalizedName](const gameCatList& category)
                        {
                            return normalizeCategoryName(category.categoryName) == normalizedName;
                        });

                    return it == catList.end() ? nullptr : &(*it);
                };

                auto isCategorySelected = [](const std::string& name)
                {
                    return std::find(
                        lootGlobals::selectedLootCategories.begin(),
                        lootGlobals::selectedLootCategories.end(),
                        name) != lootGlobals::selectedLootCategories.end();
                };

                auto setCategorySelected = [](const std::string& name, bool selected)
                {
                    const auto it = std::find(
                        lootGlobals::selectedLootCategories.begin(),
                        lootGlobals::selectedLootCategories.end(),
                        name);

                    if (selected)
                    {
                        if (it == lootGlobals::selectedLootCategories.end())
                            lootGlobals::selectedLootCategories.push_back(name);
                    }
                    else if (it != lootGlobals::selectedLootCategories.end())
                    {
                        lootGlobals::selectedLootCategories.erase(it);
                    }
                };

                if (ImGui::BeginChild("##loot_category_settings_child", ImVec2(0, -42), true))
                {
                    std::unordered_set<std::string> groupedCategoryNames;
                    bool selectionChanged = false;

                    const bool categoryGridOpen = ImGui::BeginTable(
                        "##loot_category_group_grid",
                        3,
                        ImGuiTableFlags_SizingStretchSame);

                    for (size_t groupIndex = 0; groupIndex < categoryGroups.size(); ++groupIndex)
                    {
                        const LootCategoryGroup& group = categoryGroups[groupIndex];
                        std::vector<const gameCatList*> groupCategories;

                        auto addCategory = [&groupCategories, &groupedCategoryNames](const gameCatList* category)
                        {
                            if (!category || category->categoryName.empty() || category->categoryName == "None")
                                return;

                            if (groupedCategoryNames.insert(category->categoryName).second)
                                groupCategories.push_back(category);
                        };

                        if (group.categories.empty())
                        {
                            for (const auto& category : catList)
                            {
                                if (category.categoryName.empty() ||
                                    category.categoryName == "None" ||
                                    groupedCategoryNames.contains(category.categoryName))
                                {
                                    continue;
                                }

                                groupCategories.push_back(&category);
                            }
                        }
                        else
                        {
                            for (const char* categoryName : group.categories)
                                addCategory(findCategory(categoryName));
                        }

                        if (groupCategories.empty())
                            continue;

                        if (categoryGridOpen)
                            ImGui::TableNextColumn();

                        bool allSelected = std::all_of(
                            groupCategories.begin(),
                            groupCategories.end(),
                            [isCategorySelected](const gameCatList* category)
                            {
                                return isCategorySelected(category->categoryName);
                            });

                        ImGui::PushID(static_cast<int>(groupIndex));
                        if (ImGui::Checkbox(group.name, &allSelected))
                        {
                            for (const gameCatList* category : groupCategories)
                                setCategorySelected(category->categoryName, allSelected);

                            selectionChanged = true;
                        }
                        ImGui::PopID();
                    }

                    if (categoryGridOpen)
                        ImGui::EndTable();

                    if (selectionChanged)
                        configManager.SaveConfig();

                    if (catList.size() <= 1)
                        ImGui::TextDisabled("No Tarkov Dev categories are available yet.");

                    ImGui::EndChild();
                }

                if (ImGui::Button("Close", ImVec2(100, 0)))
                {
                    categoryPopupOpen = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }


            ImGui::SetCursorPos(ImVec2(leftContentX, leftActionY + 29.0f));
            //wishlist items
            static bool wishListPopupOpen = false;
            if (ImGui::Button("Wishlist Items", ImVec2(leftButtonWidth, 0)))
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
            ImGui::SetCursorPos(ImVec2(leftSecondButtonX, leftActionY + 29.0f));
            
            static bool questLootPopupOpen = false;

            if (ImGui::Button("Quest Loot", ImVec2(leftButtonWidth, 0)))
            {
                questLootPopupOpen = true;
                ImGui::OpenPopup("Quest Loot Manager");
            }

            if (ImGui::BeginPopupModal("Quest Loot Manager", &questLootPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(900, 500));

                std::vector<LootEntity> cacheLoot = Loot.getCacheLoot();
                const QuestPublishedSnapshot questSnapshot =
                    GetQuestPublishedSnapshot();
                const std::vector<std::string>& questMasterItems =
                    questSnapshot->masterItems;

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
                std::unordered_map<std::string, LootEntity*> lootById;
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
                                if (!loot.isQuestItem())
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
                                    if (!loot.isQuestItem())
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
                                    bool forceWanted = loot.forceWanted;
                                    const bool forceLocked = loot.filterWanted || loot.pendingResolve || loot.failed;
                                    ImGui::BeginDisabled(forceLocked);
                                    if (ImGui::Checkbox("##wanted", &forceWanted))
                                    {
                                        Loot.setLootWanted(loot.instance, forceWanted, coloursGlobals::questColour);

                                        loot.forceWanted = forceWanted;
                                        loot.wanted = forceWanted;
                                    }
                                    ImGui::EndDisabled();

                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                                    {
                                        ImGui::SetTooltip(
                                            forceLocked
                                                ? "Items selected by active rules cannot be force-changed"
                                                : "Force this individual item to display");
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
                            seenMasterIds.reserve(questMasterItems.size());

                            int visibleRows = 0;
                            for (const auto& rawMasterId : questMasterItems)
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

                                for (const auto& rawMasterId : questMasterItems)
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
            ImGui::SetCursorPos(ImVec2(leftContentX, leftBottomActionY));
            static bool fullLootListPopupOpen = false;
            if (ImGui::Button("Debug Loot", ImVec2(leftButtonWidth, 0)))
            {
                fullLootListPopupOpen = true;
                ImGui::OpenPopup("Debug Loot");
            }

            std::vector<LootEntity> lootCache = Loot.getCacheLoot();

            if (ImGui::BeginPopupModal("Debug Loot", &fullLootListPopupOpen, ImGuiWindowFlags_NoResize))
            {
                ImGui::SetWindowSize(ImVec2(950, 500), ImGuiCond_Always);

                static std::vector<LootEntity*> normalLootRows;
                static std::vector<LootEntity*> questLootRows;
                static std::vector<LootEntity*> wantedRows;

                BuildLootListDebugRows(lootCache, normalLootRows, questLootRows, wantedRows);

                if (ImGui::BeginTabBar("##debugLootTabs"))
                {
                    if (ImGui::BeginTabItem("Quest Items"))
                    {
                        ImGui::Separator();
                        DrawLootListDebugTable(questLootRows, "##debugLootQuestTable");
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Wanted"))
                    {
                        ImGui::Separator();
                        DrawLootListDebugTable(wantedRows, "##debugLootWantedTable");
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
            ImGui::SetCursorPos(ImVec2(rightPanelX, topPanelY));

            // Draw window frame
            customChildWindowWithTitle(" Saved filters ", ImVec2(rightPanelWidth, topPanelHeight));

            // Draw inside window
            ImGui::SetCursorPos(ImVec2(rightPanelX + rightPanelWidth - 100.0f, topPanelY + 10.0f));

            // Button to open the add filter popup
            if (ImGui::Button("+ Add Filter", ImVec2(90, 29))) {
                addFilterPopupOpen = true;
            }

            // Position for filter list
            ImVec2 tableSize = ImVec2(rightPanelWidth - 20.0f, topPanelHeight - 55.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

            ImGui::SetCursorPos(ImVec2(rightPanelX + 10.0f, topPanelY + 45.0f));

            if (ImGui::BeginTable("##LootFiltersTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, tableSize)) {
                // Set up the table headers
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 1.f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.f);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.f);
                ImGui::TableSetupColumn("Filter Name", ImGuiTableColumnFlags_WidthStretch);
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
            ImGui::SetCursorPos(ImVec2(panelMargin, bottomPanelY));

            // draw window frame
            customChildWindowWithTitle(" Items in selected filter ", ImVec2(windowSize.x - (panelMargin * 2.0f), bottomPanelHeight));

            if (selectedLootFilterID != -1)
            {
                
                ImGui::SetCursorPos(ImVec2(windowSize.x - 100.0f, bottomPanelY + 10.0f));

                if (ImGui::Button("+ Add Loot", ImVec2(90, 29))) {
                    addLootPopupOpen = true;
                }

                //position for table
                ImGui::SetCursorPos(ImVec2(panelMargin + 10.0f, bottomPanelY + 45.0f));

                ImVec2 tableSize = ImVec2(windowSize.x - 40.0f, bottomPanelHeight - 55.0f);

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3, 2));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));

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
}
