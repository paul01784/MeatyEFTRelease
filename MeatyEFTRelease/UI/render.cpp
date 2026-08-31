#include "includes.h"
#include "../memory/Memory.h"
#include "debug.h"
#include "globals.h"
#include "perfMonitor.h"

#include "../external/glm/glm.hpp"
#include "../external/glm/gtc/matrix_access.hpp"
#include "maps.h"
#include "../Tarkov/GameWorld/MainGame.h"
#include "../Tarkov/GameWorld/Loot/Loot.h"
#include "../Web/TarkovDev/TarkovDevClient.h"
#include "../Tarkov/GameWorld/RegisteredPlayers.h"
#include "draw.h"
#include "config.h"
#include "DxRenderWindow.h"
#include "fuserRender.h"
#include "../Tarkov/GameWorld/Exits/Exfil.h"
#include "../Tarkov/GameWorld/QuestManager.h"
#include "../Tarkov/GameWorld/Loot/WishList.h"
#include "DogTagAPI.h"
#include "makcu.h"
#include "menuLayout.h"
#include "../Tarkov/GameWorld/Player/WatchList.h"
#include "aimview.h"
#include "Widgets/LootWidget.h"
#include "Widgets/MapWidget.h"
#include "Widgets/QuestWidget.h"
#include "Widgets/FuserWidget.h"
#include "../Tarkov/Features/Visibility/AtlasVisibility.h"
#include "../resource.h"

#include <cctype>
#include <chrono>
#include <limits>

namespace
{
    constexpr int RadarFontFamilyCount = 3;
    constexpr int RadarFontWeightCount = 2;
    constexpr float RadarFontSize = 18.0f;
    constexpr float RadarCounterFontSize = 17.0f;

    const char* const RadarFontNames[RadarFontFamilyCount] =
    {
        "Segoe UI",
        "Arial",
        "Tahoma"
    };

    ImFont* radarFonts[RadarFontFamilyCount][RadarFontWeightCount] = {};

    ImFont* GetSelectedRadarFont()
    {
        const int fontIndex = std::clamp(radarGlobals::fontIndex, 0, RadarFontFamilyCount - 1);
        const int weightIndex = radarGlobals::fontBold ? 1 : 0;
        return radarFonts[fontIndex][weightIndex];
    }

    void renderRadarPlayerCounts()
    {
        struct PlayerCounts
        {
            size_t pmcs{};
            size_t playerScavs{};
            size_t scavs{};
            size_t bosses{};
            size_t usecs{};
            size_t blackDivision{};
        } counts;

        const PlayerSnapshot cacheSnapshot = registeredPlayers.getCacheSnapshot();

        for (const Player& player : *cacheSnapshot)
        {
            if (!Utils::valid_pointer(player.instance) ||
                player.isLocal ||
                player.isDead ||
                player.hasExfiled)
            {
                continue;
            }

            if (player.isBlackDivision)
            {
                ++counts.blackDivision;
                continue;
            }

            if (player.isBoss)
            {
                ++counts.bosses;
                continue;
            }

            if (player.isPlayerScav)
            {
                ++counts.playerScavs;
                continue;
            }

            if (player.isAi && player.name == "Usec")
            {
                ++counts.usecs;
                continue;
            }

            if (player.isAi && !player.isBTR)
            {
                ++counts.scavs;
                continue;
            }

            if (player.isPlayer && !player.isAi)
                ++counts.pmcs;
        }

        struct Counter
        {
            size_t count{};
            const char* tooltip{};
            glm::vec4 colour{};
        };

        const Counter counters[] =
        {
            { counts.pmcs, "PMC Players", coloursGlobals::playerPMC },
            { counts.playerScavs, "Player Scav", coloursGlobals::playerScav },
            { counts.scavs, "Scav", coloursGlobals::playerAI },
            { counts.bosses, "Boss", coloursGlobals::playerBoss },
            { counts.usecs, "USEC Raiders", coloursGlobals::playerAI },
            { counts.blackDivision, "Black Division", coloursGlobals::playerBlackDiv }
        };

        constexpr float boxWidth = 34.0f;
        constexpr float boxHeight = 28.0f;
        constexpr float boxSpacing = 2.0f;
        constexpr float topMargin = 12.0f;
        constexpr ImU32 boxBackground = IM_COL32(17, 19, 22, 235);
        constexpr ImU32 boxBorder = IM_COL32(71, 74, 74, 235);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const ImVec2 windowPosition = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const float totalWidth =
            (boxWidth * IM_ARRAYSIZE(counters)) +
            (boxSpacing * (IM_ARRAYSIZE(counters) - 1));
        const float startX = windowPosition.x + ((windowSize.x - totalWidth) * 0.5f);
        const float topY = windowPosition.y + topMargin;

        for (int index = 0; index < IM_ARRAYSIZE(counters); ++index)
        {
            const Counter& counter = counters[index];
            const ImVec2 boxMin(startX + (index * (boxWidth + boxSpacing)), topY);
            const ImVec2 boxMax(boxMin.x + boxWidth, boxMin.y + boxHeight);
            const std::string value = std::to_string(counter.count);
            const ImVec2 textSize = font->CalcTextSizeA(RadarCounterFontSize, FLT_MAX, 0.0f, value.c_str());
            const ImU32 textColour = ImColor(
                counter.colour.x,
                counter.colour.y,
                counter.colour.z,
                counter.colour.w);

            drawList->AddRectFilled(boxMin, boxMax, boxBackground, 3.0f);
            drawList->AddRect(boxMin, boxMax, boxBorder, 3.0f);
            drawList->AddText(
                font,
                RadarCounterFontSize,
                ImVec2(
                    boxMin.x + ((boxWidth - textSize.x) * 0.5f),
                    boxMin.y + ((boxHeight - textSize.y) * 0.5f)),
                textColour,
                value.c_str());

            if (ImGui::IsMouseHoveringRect(boxMin, boxMax))
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(counter.tooltip);
                ImGui::EndTooltip();
            }
        }
    }
}

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
    case WindowsKey::F11: return "F11";
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
    case WindowsKey::F11: return 9;
    case WindowsKey::F12: return 10;
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
    case 9: return WindowsKey::F11;
    case 10: return WindowsKey::F12;
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
        WindowsKey::F11,
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

    const float rowStartX = ImGui::GetCursorPosX();
    const float controlX = menuLayout::ControlColumnX(
        rowStartX,
        ImGui::GetContentRegionAvail().x
    );
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Game resolution");
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
    ImGui::SetNextItemWidth(220.0f);

    if (ImGui::Combo("##gameResolution", &espGlobals::gameResInt, resolutionOptions, IM_ARRAYSIZE(resolutionOptions))) {
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


    bool changed = false;

    if (ImGui::BeginCombo(selection_name.c_str(), items[currentItem])) {
        for (int i = 0; i < items.size(); i++) {
            bool isSelected = (currentItem == i);
            if (ImGui::Selectable(items[i], isSelected)) {
                currentItem = i;
                aimKey = IndexToWindowsKey(i); // Map selection to enum
                changed = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
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


static void renderMenuSettings()
{
    enum class SettingsPage : int
    {
        App,
        Settings,
        Appearance,
        Keybinds,
        Advanced
    };

    static SettingsPage activePage = SettingsPage::App;
    static char apiKeyBuffer[256]{};
    static bool apiKeyLoaded = false;
    static bool apiKeyChecked = false;
    static bool apiKeyValid = false;
    static std::string apiKeyStatus = "Not checked";
    static std::string apiKeyError;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + 20.0f, viewport->Pos.y + 20.0f),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(ImVec2(900.0f, 720.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(720.0f, 500.0f),
        ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f)
    );
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (!ImGui::Begin("Meaty Settings", &appMenu::appSettings, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const auto saveIfChanged = [](bool changed)
    {
        if (changed)
            configManager.SaveConfig();
    };

    ImGui::BeginChild("##settingsNavigation", ImVec2(0.0f, 42.0f), false);

    const auto navigationItem = [&](const char* icon, const char* label, SettingsPage page)
    {
        if (menuLayout::TopTabButton(icon, label, activePage == page))
            activePage = page;
        ImGui::SameLine();
    };

    navigationItem(ICON_FA_DISPLAY, "App", SettingsPage::App);
    navigationItem(ICON_FA_GEARS, "Settings", SettingsPage::Settings);
    navigationItem(ICON_FA_PALETTE, "Appearance", SettingsPage::Appearance);
    navigationItem(ICON_FA_KEY, "Keybinds", SettingsPage::Keybinds);
    navigationItem(ICON_FA_SLIDERS, "Advanced", SettingsPage::Advanced);
    ImGui::EndChild();
    ImGui::BeginChild("##settingsContent", ImVec2(0.0f, 0.0f), false);
    menuLayout::PushContentInset();
    if (activePage == SettingsPage::App)
    {
        const bool dmaConnected = memoryGlobals::dmaConnected.load(std::memory_order_acquire);
        const bool processFound = memoryGlobals::processFound.load(std::memory_order_acquire);
        const bool working = mem.IsInitRunning();
        const bool stopping = mem.IsDisconnectRequested();

        if (menuLayout::BeginTwoColumns("##appColumns"))
        {
            menuLayout::NextColumn();
            if (menuLayout::Section("Connection"))
            {
                if (working)
                {
                    ImGui::BeginDisabled();
                    ImGui::Button(stopping ? "Stopping..." : "Working...", ImVec2(130.0f, 28.0f));
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (!stopping && ImGui::Button("Disconnect", ImVec2(130.0f, 28.0f)))
                        mem.doDMADisconnect();
                }
                else if (!dmaConnected)
                {
                    if (ImGui::Button("Connect", ImVec2(130.0f, 28.0f)))
                        mem.doDMAConnect();
                }
                else
                {
                    if (ImGui::Button("Disconnect", ImVec2(130.0f, 28.0f)))
                        mem.doDMADisconnect();
                    ImGui::SameLine();
                    if (processFound && ImGui::Button("Soft restart", ImVec2(130.0f, 28.0f)))
                        registeredPlayers.softRestart();
                }

                if (!dmaConnected)
                {
                    saveIfChanged(menuLayout::ToggleRow("Connect automatically", "autoConnect", &memoryGlobals::dmaAutoConnect));
                    saveIfChanged(menuLayout::ToggleRow("Close connections", "closeConnections", &memoryGlobals::dmaCloseAll));
                }
                saveIfChanged(menuLayout::ToggleRow("Show connection stats", "showStats", &memoryGlobals::dmaShowStats));
            }

            menuLayout::NextColumn();
            if (menuLayout::Section("Window & display"))
            {
                saveIfChanged(menuLayout::SliderFloatRow("Window opacity", "windowAlpha", &globals::appWindowAlpha, 0.25f, 1.0f, "%.2f"));
                saveIfChanged(menuLayout::SliderFloatRow("Radar maximum FPS", "radarMaxFps", &globals::appRadarMaxFPS, 15.0f, 240.0f, "%.0f FPS"));
                if (showResSelectionBox())
                    configManager.SaveConfig();
                saveIfChanged(menuLayout::SliderFloatRow("Radar text scale", "radarText", &radarGlobals::textScale, 0.75f, 2.0f, "%.2fx"));
                saveIfChanged(menuLayout::ComboRow("Radar font", "radarFont", &radarGlobals::fontIndex, RadarFontNames, IM_ARRAYSIZE(RadarFontNames)));
                saveIfChanged(menuLayout::ToggleRow("Bold", "radarFontBold", &radarGlobals::fontBold));
            }
            menuLayout::EndTwoColumns();
        }

        if (menuLayout::Section("Dogtag Cloud API"))
        {
            if (!apiKeyLoaded)
            {
                strncpy_s(apiKeyBuffer, globals::dogTagAPIKey.c_str(), sizeof(apiKeyBuffer) - 1);
                apiKeyLoaded = true;
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("API key", apiKeyBuffer, IM_ARRAYSIZE(apiKeyBuffer), ImGuiInputTextFlags_Password);
            if (ImGui::Button("Save key", ImVec2(120.0f, 28.0f)))
            {
                globals::dogTagAPIKey = apiKeyBuffer;
                g_DogTagAPI.setApiKey(globals::dogTagAPIKey);
                configManager.SaveConfig();
                apiKeyChecked = false;
                apiKeyStatus = "Saved — not checked";
                apiKeyError.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Check status", ImVec2(120.0f, 28.0f)))
            {
                g_DogTagAPI.setApiKey(apiKeyBuffer);
                apiKeyChecked = true;
                apiKeyValid = false;
                apiKeyStatus = "Checking...";
                apiKeyError.clear();
                const auto status = g_DogTagAPI.getKeyStatus();
                if (status && status->valid)
                {
                    apiKeyValid = true;
                    apiKeyStatus = "Active";
                }
                else
                {
                    apiKeyStatus = "Invalid, disabled or unavailable";
                    apiKeyError = status && !status->error.empty()
                        ? status->error
                        : "Could not verify the API key.";
                }
            }
            if (!globals::dogTagAPIKey.empty())
            {
                ImGui::SameLine();
                if (ImGui::Button("Clear", ImVec2(80.0f, 28.0f)))
                {
                    memset(apiKeyBuffer, 0, sizeof(apiKeyBuffer));
                    globals::dogTagAPIKey.clear();
                    g_DogTagAPI.clearApiKey();
                    configManager.SaveConfig();
                    apiKeyChecked = false;
                    apiKeyStatus = "No key set";
                    apiKeyError.clear();
                }
            }

            const ImVec4 statusColour = !apiKeyChecked
                ? ImVec4(1.0f, 0.75f, 0.2f, 1.0f)
                : apiKeyValid
                    ? ImVec4(0.2f, 1.0f, 0.35f, 1.0f)
                    : ImVec4(1.0f, 0.25f, 0.25f, 1.0f);
            ImGui::TextColored(statusColour, "Status: %s", apiKeyStatus.c_str());
            if (!apiKeyError.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "%s", apiKeyError.c_str());
        }
    }
    else if (activePage == SettingsPage::Settings)
    {
        auto& aimviewConfig = g_AimViewWidget.GetConfig();
        if (menuLayout::BeginTwoColumns("##featureColumns"))
        {
            menuLayout::NextColumn();
            if (menuLayout::Section("Radar"))
            {
                saveIfChanged(menuLayout::ToggleRow("Players", "radarPlayers", &radarGlobals::drawPlayers));
                saveIfChanged(menuLayout::ToggleRow("Grenades", "radarGrenades", &radarGlobals::drawGrenades));
                saveIfChanged(menuLayout::TogglePairRow(
                    "Tripwires", "radarTripwires", &radarGlobals::drawTripwires,
                    "Tripwire lines", "radarTripwireLines", &radarGlobals::drawTripwireLine));
                saveIfChanged(menuLayout::ToggleRow("Loot", "radarLoot", &radarGlobals::drawLoot));
                saveIfChanged(menuLayout::ToggleRow("Quest helper", "radarQuest", &radarGlobals::drawQuestHelper));
                saveIfChanged(menuLayout::ToggleRow("Aim view", "aimView", &aimviewConfig.enabled));
                saveIfChanged(menuLayout::ToggleRow("Radar min view", "radarMinView", &radarGlobals::minimalView));
            }
            if (menuLayout::Section("Radar aim lines"))
            {
                saveIfChanged(menuLayout::SliderIntRow("Local", "localAimLine", &radarGlobals::localAimLine, 4, 500, "%d px"));
                saveIfChanged(menuLayout::SliderIntRow("Friends", "friendAimLine", &radarGlobals::friendAimLine, 4, 500, "%d px"));
                saveIfChanged(menuLayout::SliderIntRow("Enemies", "enemyAimLine", &radarGlobals::enemyAimLine, 4, 500, "%d px"));
                saveIfChanged(menuLayout::ToggleRow("Extend aimlines", "aimLineTargets", &radarGlobals::drawAimLineTargets));

                ImGui::BeginDisabled(!radarGlobals::drawAimLineTargets);
                saveIfChanged(menuLayout::SliderFloatRow("Target angle", "aimTargetAngle", &radarGlobals::aimLineTargetAngle, 1.0f, 20.0f, "%.1f°"));
                saveIfChanged(menuLayout::SliderIntRow("Looking at you range", "aimTargetRange", &radarGlobals::aimLineTargetMaxDistance, 10, 2000, "%d m"));

                static const char* const aimOverlayAlertOptions[] =
                {
                    "Off",
                    "All",
                    "Players"
                };

                saveIfChanged(menuLayout::ComboRow(
                    "Aim overlay alert",
                    "aimOverlayAlert",
                    &espGlobals::aimOverlayAlert,
                    aimOverlayAlertOptions,
                    IM_ARRAYSIZE(aimOverlayAlertOptions)));
                ImGui::EndDisabled();
            }

            menuLayout::NextColumn();
            if (menuLayout::Section("ESP"))
            {
                saveIfChanged(menuLayout::ToggleIntSliderRow("Grenades", "espGrenades", &espGlobals::drawGrenades, "Range", &espGlobals::drawGrenadesDist, 10, 400, "%d m"));
                saveIfChanged(menuLayout::ToggleIntSliderRow("Tripwires", "espTripwires", &espGlobals::drawTripwires, "Range", &espGlobals::drawTripwiresDist, 10, 400, "%d m"));
                saveIfChanged(menuLayout::ToggleIntSliderRow("Loot", "espLoot", &espGlobals::drawLoot, "Range", &espGlobals::drawLootDist, 5, 400, "%d m"));
                saveIfChanged(menuLayout::ToggleIntSliderRow("Corpses", "espCorpses", &espGlobals::drawCorpse, "Range", &espGlobals::drawCorpseDist, 5, 400, "%d m"));
                saveIfChanged(menuLayout::ToggleRow("Quest helper", "espQuest", &espGlobals::drawQuestHelper));
            }
            if (menuLayout::Section("ESP Players"))
            {
                saveIfChanged(menuLayout::ToggleIntSliderRow("Players", "espPlayers", &espGlobals::drawPlayers, "Range", &espGlobals::drawPlayerDist, 10, 1000, "%d m"));
                saveIfChanged(menuLayout::ToggleRow("Players Equip", "equipmentInfo", &radarGlobals::getPlayerEquip));
                saveIfChanged(menuLayout::ToggleRow("Boxes", "espBoxes", &espGlobals::drawBoxPlayers));
                saveIfChanged(menuLayout::ToggleRow("Skeleton", "espSkeleton", &espGlobals::drawSkeletons));
                saveIfChanged(menuLayout::ToggleFloatSliderRow("Head dot", "espHeadDot", &espGlobals::drawHeadDot, "Size", &espGlobals::headDotSize, 0.5f, 10.0f, "%.1f"));
            }
            if (menuLayout::Section("ESP Local"))
            {
                static const char* const crosshairTypeOptions[] =
                {
                    "Circle",
                    "Cross"
                };

                saveIfChanged(menuLayout::ToggleComboIntSliderRow(
                    "Crosshair",
                    "espCrosshair",
                    &espGlobals::drawCrosshair,
                    "Type",
                    &espGlobals::crosshairType,
                    crosshairTypeOptions,
                    IM_ARRAYSIZE(crosshairTypeOptions),
                    "Size",
                    &espGlobals::crosshairSize,
                    1,
                    20));
            }
            menuLayout::EndTwoColumns();
        }

        if (menuLayout::BeginTwoColumns("##exfilAndDataColumns"))
        {
            menuLayout::NextColumn();
            if (menuLayout::Section("Radar Exfils"))
            {
                saveIfChanged(menuLayout::ToggleRow("Draw extracts", "radarExtracts", &radarGlobals::drawExfils));
                bool radarExfilOptionsChanged = false;
                if (!radarGlobals::drawExfils)
                {
                    radarExfilOptionsChanged |= radarGlobals::drawSecretExfils;
                    radarExfilOptionsChanged |= radarGlobals::drawTransitExfils;
                    radarGlobals::drawSecretExfils = false;
                    radarGlobals::drawTransitExfils = false;
                }
                radarExfilOptionsChanged |= menuLayout::TogglePairRow(
                    "Secret extracts", "radarSecretExtracts", &radarGlobals::drawSecretExfils,
                    "Transits", "radarTransits", &radarGlobals::drawTransitExfils,
                    radarGlobals::drawExfils);
                saveIfChanged(radarExfilOptionsChanged);
            }
            menuLayout::NextColumn();
            if (menuLayout::Section("ESP Exfils"))
            {
                saveIfChanged(menuLayout::ToggleRow("Draw extracts", "espExtracts", &espGlobals::drawExfil));
                bool espExfilOptionsChanged = false;
                if (!espGlobals::drawExfil)
                {
                    espExfilOptionsChanged |= espGlobals::drawSecretExfils;
                    espExfilOptionsChanged |= espGlobals::drawTransitExfils;
                    espGlobals::drawSecretExfils = false;
                    espGlobals::drawTransitExfils = false;
                }
                espExfilOptionsChanged |= menuLayout::TogglePairRow(
                    "Secret extracts", "espSecretExtracts", &espGlobals::drawSecretExfils,
                    "Transits", "espTransits", &espGlobals::drawTransitExfils,
                    espGlobals::drawExfil);
                saveIfChanged(espExfilOptionsChanged);
                saveIfChanged(menuLayout::SliderIntRow(
                    "Extract range",
                    "espExtractRange",
                    &espGlobals::drawExfilDist,
                    5,
                    1000,
                    "%d m",
                    espGlobals::drawExfil));
            }
            menuLayout::EndTwoColumns();
        }

        if (menuLayout::Section("Player data"))
        {
            static const char* const tarkovDevDataModes[] =
            {
                "PVP",
                "PVP-SEASONAL"
            };

            bool changed = menuLayout::InlineToggle(
                "Use Tarkov.dev Data",
                "tarkovDevInfo",
                &radarGlobals::getPlayerStats);
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::SetNextItemWidth(145.0f);
            changed |= ImGui::Combo(
                "##tarkovDevDataMode",
                &radarGlobals::tarkovDevDataMode,
                tarkovDevDataModes,
                IM_ARRAYSIZE(tarkovDevDataModes));
            saveIfChanged(changed);
        }
    }
    else if (activePage == SettingsPage::Appearance)
    {
        if (menuLayout::BeginTwoColumns("##appearanceColumns"))
        {
            menuLayout::NextColumn();
            if (menuLayout::Section("Players"))
            {
                saveIfChanged(menuLayout::ColourRow("PMC", "pmcColour", (float*)&coloursGlobals::playerPMC));
                saveIfChanged(menuLayout::ColourRow("Player scav", "scavColour", (float*)&coloursGlobals::playerScav));
                saveIfChanged(menuLayout::ColourRow("AI", "aiColour", (float*)&coloursGlobals::playerAI));
                saveIfChanged(menuLayout::ColourRow("Boss", "bossColour", (float*)&coloursGlobals::playerBoss));
                saveIfChanged(menuLayout::ColourRow("Black Division", "blackDivColour", (float*)&coloursGlobals::playerBlackDiv));
                saveIfChanged(menuLayout::ColourRow("Local player", "localColour", (float*)&coloursGlobals::playerLocal));
                saveIfChanged(menuLayout::ColourRow("Friendly", "friendlyColour", (float*)&coloursGlobals::playerFriendly));
                saveIfChanged(menuLayout::ColourRow("Watched", "watchedColour", (float*)&coloursGlobals::playerWatched));
            }
            menuLayout::NextColumn();
            if (menuLayout::Section("World"))
            {
                saveIfChanged(menuLayout::ColourRow("Grenades", "grenadesColour", (float*)&coloursGlobals::grenades));
                saveIfChanged(menuLayout::ColourRow("Tripwires", "tripwiresColour", (float*)&coloursGlobals::tripwires));
                saveIfChanged(menuLayout::ColourRow("Extracts", "extractsColour", (float*)&coloursGlobals::exfils));
                saveIfChanged(menuLayout::ColourRow("Quest markers", "questColour", (float*)&coloursGlobals::questMarker));
                saveIfChanged(menuLayout::ColourRow("Containers", "containersColour", (float*)&coloursGlobals::containerColour));
                saveIfChanged(menuLayout::ColourRow("Player group lines", "groupLineColour", (float*)&coloursGlobals::playerGroupLine));
                saveIfChanged(menuLayout::ColourRow("Player corpses", "corpseColour", (float*)&coloursGlobals::playerCorpse));
                saveIfChanged(menuLayout::ColourRow("Crosshair", "crosshairColour", (float*)&coloursGlobals::crosshair));
                saveIfChanged(menuLayout::ColourRow("FOV circle", "fovColour", (float*)&coloursGlobals::fovCircle));
            }
            menuLayout::EndTwoColumns();
        }
    }
    else if (activePage == SettingsPage::Keybinds)
    {
        if (menuLayout::Section("Controls"))
        {
            ImGui::SetNextItemWidth(220.0f);
            if (ShowKeySelectionBox(keyGlobals::aimKey, "Aim key"))
                configManager.SaveConfig();
            ImGui::SetNextItemWidth(220.0f);
            if (ShowKeySelectionBox(keyGlobals::toggleFollow, "Toggle follow"))
                configManager.SaveConfig();
            ImGui::SetNextItemWidth(220.0f);
            if (ShowKeySelectionBox(keyGlobals::battleMode, "Battle mode"))
                configManager.SaveConfig();
            ImGui::SetNextItemWidth(220.0f);
            if (ShowKeySelectionBox(keyGlobals::toggleRadarMinView, "Toggle radar min view"))
                configManager.SaveConfig();
        }
    }
    else
    {
        if (menuLayout::Section("Static visibility (Factory)"))
        {
            bool changed = false;
            changed |= menuLayout::ToggleRow(
                "Enable collision visibility",
                "atlasVisibilityEnabled",
                &atlasVisibilityGlobals::enabled);

            ImGui::BeginDisabled(!atlasVisibilityGlobals::enabled);
            changed |= menuLayout::SliderIntRow(
                "Maximum range",
                "atlasVisibilityRange",
                &atlasVisibilityGlobals::maxDistance,
                10,
                500,
                "%d m");
            ImGui::EndDisabled();

            ImGui::TextDisabled("%s", atlasVisibility.getStatusText().c_str());
            saveIfChanged(changed);
        }

        if (menuLayout::Section("Task intervals"))
        {
            const auto timing = [](const char* label, double& value, double minValue, double maxValue, double speed)
            {
                const double before = value;
                ImGui::SetNextItemWidth(240.0f);
                ImGui::DragScalar(label, ImGuiDataType_Double, &value, static_cast<float>(speed), &minValue, &maxValue, "%.0f ms", ImGuiSliderFlags_AlwaysClamp);
                return before != value;
            };
            bool changed = false;
            changed |= timing("Camera", globals::taskCamera, 1.0, 100.0, 0.5);
            changed |= timing("Players", globals::taskPlayers, 5.0, 500.0, 1.0);
            changed |= timing("Player bone update", globals::taskPlayerPositions, 5.0, 100.0, 0.5);
            changed |= timing("Static visibility", globals::taskStaticVisibility, 50.0, 1000.0, 25.0);
            changed |= timing("Fireport", globals::taskFireport, 5.0, 100.0, 1.0);
            changed |= timing("Full skeleton", globals::taskPlayersBones, 5.0, 500.0, 1.0);
            changed |= timing("Loot", globals::taskLoot, 100.0, 30000.0, 100.0);
            changed |= timing("Equipment", globals::taskPlayersEquipment, 100.0, 30000.0, 100.0);
            changed |= timing("Player metadata", globals::taskPlayerMetadata, 50.0, 5000.0, 25.0);
            changed |= timing("Grenades", globals::taskGrenades, 10.0, 5000.0, 10.0);
            changed |= timing("Tripwires", globals::taskTripWire, 10.0, 5000.0, 10.0);
            saveIfChanged(changed);
        }
    }

    menuLayout::PopContentInset();
    ImGui::EndChild();
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
    enum class DebugPage : int { Console, Performance, Memory, Map };
    static DebugPage activePage = DebugPage::Console;
    std::string windowNameMain = "Debug";
    static ImGuiWindowFlags flagss = ImGuiWindowFlags_NoCollapse;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 40.0f, viewport->Pos.y + 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(980.0f, 680.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(720.0f, 480.0f),
        ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f)
    );

    if (ImGui::Begin(windowNameMain.c_str(), &appMenu::widgetDebug, flagss))
    {
        ImGui::BeginChild("##debugNavigation", ImVec2(0.0f, 42.0f), false);
        const auto nav = [&](const char* icon, const char* label, DebugPage page)
        {
            if (menuLayout::TopTabButton(icon, label, activePage == page))
                activePage = page;
            ImGui::SameLine();
        };
        nav(ICON_FA_TERMINAL, "Console", DebugPage::Console);
        nav(ICON_FA_GAUGE, "Performance", DebugPage::Performance);
        nav(ICON_FA_DATABASE, "Memory", DebugPage::Memory);
        nav(ICON_FA_MAP, "Map", DebugPage::Map);
        ImGui::EndChild();
        ImGui::BeginChild("##debugContent", ImVec2(0.0f, 0.0f), false);
        menuLayout::PushContentInset();
        if (activePage == DebugPage::Console)
        {
                static bool showInfo = true;
                static bool showWarn = true;
                static bool showError = true;
                static bool pauseConsole = false;
                static bool autoScroll = true;
                static ImGuiTextFilter consoleFilter;
                static std::vector<Message> displayMessages;

                if (!pauseConsole || displayMessages.empty())
                    displayMessages = LOGS.getMessages();

                std::size_t infoCount = 0;
                std::size_t warnCount = 0;
                std::size_t errorCount = 0;

                for (const Message& message : displayMessages)
                {
                    switch (message.level)
                    {
                    case MessageLevel::INFO:
                        ++infoCount;
                        break;
                    case MessageLevel::WARN:
                        ++warnCount;
                        break;
                    case MessageLevel::ERR:
                        ++errorCount;
                        break;
                    }
                }

                ImGui::Checkbox("Info", &showInfo);
                ImGui::SameLine();
                ImGui::Checkbox("Warnings", &showWarn);
                ImGui::SameLine();
                ImGui::Checkbox("Errors", &showError);
                ImGui::SameLine();
                ImGui::Checkbox("Pause", &pauseConsole);
                ImGui::SameLine();
                ImGui::Checkbox("Auto-scroll", &autoScroll);

                consoleFilter.Draw("Filter", 260.0f);
                ImGui::SameLine();

                if (ImGui::Button("Clear Console"))
                {
                    LOGS.clearLog();
                    displayMessages.clear();
                }

                ImGui::Text(
                    "%zu info  |  %zu warnings  |  %zu errors  |  %zu retained",
                    infoCount,
                    warnCount,
                    errorCount,
                    displayMessages.size());

                const std::string logPath = LOGS.getErrorLogPath().string();
                ImGui::TextDisabled(
                    "Saved automatically: %s",
                    logPath.empty() ? "<log path unavailable>" : logPath.c_str());

                ImGui::Separator();

                if (ImGui::BeginTable(
                    "DebugTable",
                    3,
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_BordersInnerV,
                    ImVec2(0.0f, 0.0f)))
                {
                    ImGui::TableSetupColumn(
                        "Time",
                        ImGuiTableColumnFlags_WidthFixed,
                        70.0f);
                    ImGui::TableSetupColumn(
                        "Level",
                        ImGuiTableColumnFlags_WidthFixed,
                        70.0f);
                    ImGui::TableSetupColumn(
                        "Message",
                        ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    for (const Message& message : displayMessages)
                    {
                        bool visible = false;
                        ImVec4 colour{};

                        switch (message.level)
                        {
                        case MessageLevel::INFO:
                            visible = showInfo;
                            colour = ImVec4(0.88f, 0.90f, 0.94f, 1.0f);
                            break;
                        case MessageLevel::WARN:
                            visible = showWarn;
                            colour = ImVec4(1.0f, 0.75f, 0.20f, 1.0f);
                            break;
                        case MessageLevel::ERR:
                            visible = showError;
                            colour = ImVec4(1.0f, 0.32f, 0.32f, 1.0f);
                            break;
                        }

                        if (!visible || !consoleFilter.PassFilter(message.text.c_str()))
                            continue;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::Text("%.2fs", message.timestamp);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(
                            colour,
                            "%s",
                            messageLevelToString(message.level).c_str());

                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextColored(
                            colour,
                            "%s",
                            message.text.c_str());
                    }

                    if (autoScroll && !pauseConsole)
                        ImGui::SetScrollHereY(1.0f);

                    ImGui::EndTable();
                }

        }
        if (activePage == DebugPage::Performance)
        {
                static bool freezeRecent = false;
                static bool newestFirst = true;
                static std::vector<PerfSample> recentSamples;
                static std::vector<PerfMetricSnapshot> topTasks;
                static std::vector<PerfMetricSnapshot> topDma;
                static MemoryTrafficStats traffic{};
                static DxFuserPerformanceSnapshot fuserPerformance{};
                static PlayerSnapshotTelemetry playerTelemetry{};
                static CameraProjectionSnapshot cameraProjection;
                static double cameraAgeMs = -1.0;
                static std::uint64_t cameraBusySkips = 0;
                static auto lastRefresh =
                    std::chrono::steady_clock::time_point{};

                const auto now = std::chrono::steady_clock::now();

                if (lastRefresh == std::chrono::steady_clock::time_point{} ||
                    now - lastRefresh >= std::chrono::milliseconds(250))
                {
                    topTasks =
                        PerfMonitor::Instance().GetTopMetrics("task.", 5);
                    topDma =
                        PerfMonitor::Instance().GetTopMetrics("dma.", 5);
                    traffic = mem.GetTrafficStats();
                    fuserPerformance =
                        g_DxWindow.GetPerformanceSnapshot();
                    playerTelemetry = registeredPlayers.getSnapshotTelemetry();
                    cameraProjection = camera.getProjectionSnapshot();
                    cameraBusySkips = camera.getBusyReadSkips();

                    cameraAgeMs = -1.0;
                    if (cameraProjection &&
                        cameraProjection->publishedAt !=
                        std::chrono::steady_clock::time_point{})
                    {
                        cameraAgeMs =
                            std::chrono::duration<double, std::milli>(
                                now - cameraProjection->publishedAt).count();
                    }

                    if (!freezeRecent)
                        recentSamples = PerfMonitor::Instance().GetRecent();

                    lastRefresh = now;
                }

                const bool schedulerRunning =
                    appGlobals::runThreads.load(std::memory_order_acquire);
                const bool dmaReady = mem.IsDmaOperational();
                const double peakMs = PerfMonitor::Instance().GetPeakMs();
                const std::string peakName =
                    PerfMonitor::Instance().GetPeakName();
                const std::string peakDetail =
                    PerfMonitor::Instance().GetPeakDetail();

                ImGui::TextColored(
                    schedulerRunning
                    ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                    : ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                    "Scheduler: %s",
                    schedulerRunning ? "RUNNING" : "IDLE");

                ImGui::SameLine();
                ImGui::TextColored(
                    dmaReady
                    ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                    : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                    "DMA: %s",
                    dmaReady ? "READY" : "NOT READY");

                ImGui::SameLine();
                ImGui::Text(
                    "Read %.0f ops/s | %.0f requests/s | %s",
                    traffic.readOperationsPerSecond,
                    traffic.readRequestsPerSecond,
                    formatDataRate(static_cast<std::size_t>(
                        traffic.readBytesRequestedPerSecond)).c_str());

                ImGui::Text(
                    "Peak: %.1f ms%s%s",
                    peakMs,
                    peakName.empty() ? "" : " | ",
                    peakName.empty() ? "" : peakName.c_str());

                if (!peakDetail.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%s)", peakDetail.c_str());
                }

                if (ImGui::Button("Reset performance history"))
                {
                    PerfMonitor::Instance().ResetStatistics();
                    g_DxWindow.ResetPerformanceStatistics();
                    topTasks.clear();
                    topDma.clear();
                    recentSamples.clear();
                }

                ImGui::SameLine();

                if (ImGui::Button("Reset DMA counters"))
                {
                    mem.ResetTrafficStats();
                    traffic = {};
                }

                auto taskBudget = [](std::string_view name) -> double
                    {
                        if (name == "task.cameraTask")
                            return globals::taskCamera;
                        if (name == "task.fireportTask")
                            return globals::taskFireport;
                        if (name == "task.readOnlyAim")
                            return globals::taskAim;
                        if (name == "task.keyManager")
                            return globals::taskKeyManager;
                        if (name == "task.playersTask")
                            return globals::taskPlayers;
                        if (name == "task.playerBoneTask")
                            return globals::taskPlayerPositions;
                        if (name == "task.raidMonitor")
                            return globals::taskRaidMonitor;
                        if (name == "task.ExplosiveManagerTask")
                            return globals::taskGrenades;
                        if (name == "task.TripwireManagerTask")
                            return globals::taskTripWire;
                        if (name == "task.exfilTask")
                            return globals::taskExfil;
                        if (name == "task.lootTask")
                            return globals::taskLoot;
                        if (name == "task.PlayerEquipmentTask")
                            return globals::taskPlayersEquipment;
                        if (name == "task.PlayerMetadataTask")
                            return globals::taskPlayerMetadata;
                        if (name == "task.questTask")
                            return globals::taskQuest;
                        if (name == "task.wishManagerTask")
                            return globals::taskWishManager;

                        return 0.0;
                    };

                ImGui::Spacing();
                ImGui::SeparatorText("Fuser Frame Pipeline");

                ImGui::Text(
                    "Fuser: %s | %.1f FPS | %zu commands | %llu dropped",
                    g_DxWindow.IsRunning() ? "RUNNING" : "STOPPED",
                    fuserPerformance.presentedFPS,
                    fuserPerformance.commandCount,
                    static_cast<unsigned long long>(
                        fuserPerformance.droppedCommandCount));

                if (ImGui::BeginTable(
                    "FuserPipelineTimings",
                    4,
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Stage");
                    ImGui::TableSetupColumn("Last", 0, 0.7f);
                    ImGui::TableSetupColumn("Rolling", 0, 0.7f);
                    ImGui::TableSetupColumn("Peak", 0, 0.7f);
                    ImGui::TableHeadersRow();

                    const auto drawTimingRow = [](
                        const char* label,
                        const DxTimingSnapshot& timing)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(label);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.2f ms", timing.lastMs);
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.2f ms", timing.averageMs);
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%.2f ms", timing.peakMs);
                        };

                    drawTimingRow(
                        "Frame interval",
                        fuserPerformance.frameInterval);
                    drawTimingRow("Build snapshots", fuserPerformance.build);
                    drawTimingRow("Direct2D draw", fuserPerformance.draw);
                    drawTimingRow("Swap-chain present", fuserPerformance.present);

                    ImGui::EndTable();
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Fuser Data Freshness");

                const auto freshnessColour = [](
                    double ageMs,
                    double targetMs)
                    {
                        if (ageMs < 0.0)
                            return ImVec4(0.60f, 0.62f, 0.68f, 1.0f);
                        if (ageMs <= targetMs * 2.0)
                            return ImVec4(0.35f, 0.90f, 0.45f, 1.0f);
                        if (ageMs <= targetMs * 4.0)
                            return ImVec4(1.0f, 0.72f, 0.20f, 1.0f);
                        return ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
                    };

                ImGui::TextColored(
                    cameraProjection && cameraProjection->valid
                    ? freshnessColour(cameraAgeMs, globals::taskCamera)
                    : ImVec4(1.0f, 0.30f, 0.30f, 1.0f),
                    "Camera (%s): %.1f ms old | rolling %.1f ms | v%llu | busy skips %llu",
                    cameraProjection && cameraProjection->valid
                    ? "VALID"
                    : "INVALID",
                    cameraAgeMs,
                    cameraProjection
                    ? cameraProjection->averageIntervalMs
                    : 0.0,
                    static_cast<unsigned long long>(
                        cameraProjection ? cameraProjection->version : 0),
                    static_cast<unsigned long long>(cameraBusySkips));

                ImGui::TextColored(
                    freshnessColour(
                        playerTelemetry.motionAgeMs,
                        globals::taskPlayerPositions),
                    "Player motion: %.1f ms old | rolling %.1f ms | v%llu | %zu players",
                    playerTelemetry.motionAgeMs,
                    playerTelemetry.averageMotionIntervalMs,
                    static_cast<unsigned long long>(
                        playerTelemetry.motionVersion),
                    playerTelemetry.playerCount);

                ImGui::TextDisabled(
                    "Smooth test scene + stale motion means the data feed is limiting; a high Present time usually means VSync/refresh pacing.");

                ImGui::Spacing();
                ImGui::SeparatorText("Top 5 Scheduled Tasks");

                if (topTasks.empty())
                {
                    ImGui::TextDisabled(
                        "No task samples yet. Enter a raid to populate timings.");
                }
                else if (ImGui::BeginTable(
                    "TopTaskTimings",
                    6,
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Task");
                    ImGui::TableSetupColumn("Last", 0, 0.7f);
                    ImGui::TableSetupColumn("Rolling", 0, 0.7f);
                    ImGui::TableSetupColumn("Peak", 0, 0.7f);
                    ImGui::TableSetupColumn("Budget", 0, 0.7f);
                    ImGui::TableSetupColumn("Load", 0, 0.7f);
                    ImGui::TableHeadersRow();

                    for (const PerfMetricSnapshot& metric : topTasks)
                    {
                        std::string_view displayName(metric.name);

                        if (displayName.starts_with("task."))
                            displayName.remove_prefix(5);

                        const double budget = taskBudget(metric.name);
                        const double loadPercent = budget > 0.0
                            ? (metric.averageMs / budget) * 100.0
                            : 0.0;

                        ImVec4 loadColour(0.35f, 0.90f, 0.45f, 1.0f);

                        if (loadPercent >= 100.0)
                            loadColour = ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
                        else if (loadPercent >= 60.0)
                            loadColour = ImVec4(1.0f, 0.72f, 0.20f, 1.0f);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(displayName.data(), displayName.data() + displayName.size());

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f ms", metric.lastMs);

                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.2f ms", metric.averageMs);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.2f ms", metric.peakMs);

                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text(budget > 0.0 ? "%.1f ms" : "-", budget);

                        ImGui::TableSetColumnIndex(5);
                        ImGui::TextColored(loadColour, budget > 0.0 ? "%.0f%%" : "-", loadPercent);
                    }

                    ImGui::EndTable();
                }

                ImGui::TextDisabled("Rolling is an exponential average. Load compares task runtime with its configured interval.");

                ImGui::Spacing();
                ImGui::SeparatorText("DMA Contention and Execution");

                if (topDma.empty())
                {
                    ImGui::TextDisabled("No DMA timing samples yet.");
                }
                else if (ImGui::BeginTable(
                    "TopDmaTimings",
                    4,
                    ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Operation");
                    ImGui::TableSetupColumn("Last", 0, 0.7f);
                    ImGui::TableSetupColumn("Rolling", 0, 0.7f);
                    ImGui::TableSetupColumn("Peak", 0, 0.7f);
                    ImGui::TableHeadersRow();

                    for (const PerfMetricSnapshot& metric : topDma)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(metric.name.c_str());

                        if (!metric.detail.empty() &&
                            ImGui::IsItemHovered(
                                ImGuiHoveredFlags_DelayShort))
                        {
                            ImGui::SetTooltip("%s", metric.detail.c_str());
                        }

                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f ms", metric.lastMs);
                        ImGui::TableSetColumnIndex(2);
                        ImGui::Text("%.2f ms", metric.averageMs);
                        ImGui::TableSetColumnIndex(3);
                        ImGui::Text("%.2f ms", metric.peakMs);
                    }

                    ImGui::EndTable();
                }

                ImGui::TextDisabled("dma.lock_wait is scheduler contention; dma.execute is device/VMM time.");

                if (ImGui::CollapsingHeader("Recent timing events"))
                {
                    ImGui::Checkbox("Freeze", &freezeRecent);
                    ImGui::SameLine();
                    ImGui::Checkbox("Newest first", &newestFirst);

                    if (ImGui::BeginTable(
                        "RecentPerfEvents",
                        4,
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_ScrollY |
                        ImGuiTableFlags_BordersInnerV,
                        ImVec2(0.0f, 220.0f)))
                    {
                        ImGui::TableSetupColumn(
                            "Time",
                            ImGuiTableColumnFlags_WidthFixed,
                            70.0f);
                        ImGui::TableSetupColumn(
                            "ms",
                            ImGuiTableColumnFlags_WidthFixed,
                            65.0f);
                        ImGui::TableSetupColumn(
                            "Name",
                            ImGuiTableColumnFlags_WidthFixed,
                            190.0f);
                        ImGui::TableSetupColumn(
                            "Detail",
                            ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();

                        auto drawSample = [](const PerfSample& sample)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%.1fs", sample.timestampSec);
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%.2f", sample.durationMs);
                                ImGui::TableSetColumnIndex(2);
                                ImGui::TextUnformatted(sample.name.c_str());
                                ImGui::TableSetColumnIndex(3);
                                ImGui::TextUnformatted(sample.detail.c_str());
                            };

                        if (newestFirst)
                        {
                            for (auto sample = recentSamples.rbegin();
                                sample != recentSamples.rend();
                                ++sample)
                            {
                                drawSample(*sample);
                            }
                        }
                        else
                        {
                            for (const PerfSample& sample : recentSamples)
                                drawSample(sample);
                        }

                        ImGui::EndTable();
                    }
                }

        }
        if (activePage == DebugPage::Memory)
        {
                if (ImGui::BeginTabBar("##Tabsmemory", ImGuiTabBarFlags_FittingPolicyResizeDown))
                {
                    if (ImGui::BeginTabItem("Overview"))
                    {
                        static MemoryConnectionStats connection{};
                        static MemoryTrafficStats traffic{};
                        static auto lastStatsRefresh =
                            std::chrono::steady_clock::time_point{};

                        const auto now = std::chrono::steady_clock::now();

                        if (lastStatsRefresh ==
                                std::chrono::steady_clock::time_point{} ||
                            now - lastStatsRefresh >=
                                std::chrono::milliseconds(500))
                        {
                            connection = mem.GetConnectionStats();
                            traffic = mem.GetTrafficStats();
                            lastStatsRefresh = now;
                        }

                        const bool dmaReady = mem.IsDmaOperational();
                        const bool worldReady =
                            Utils::valid_pointer(mainGame.gameWorld) &&
                            Utils::valid_pointer(mainGame.localGameWorld);
                        const bool localReady =
                            Utils::valid_pointer(mainGame.localPlayerPtr);
                        const bool schedulerRunning =
                            appGlobals::runThreads.load(
                                std::memory_order_acquire);
                        const PlayerSnapshot playerSnapshot =
                            registeredPlayers.getCacheSnapshot();

                        ImGui::SeparatorText("Health");

                        ImGui::TextColored(
                            dmaReady
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                            "DMA %s",
                            dmaReady ? "READY" : "NOT READY");

                        ImGui::SameLine();
                        ImGui::TextColored(
                            worldReady
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                            "| World %s",
                            worldReady ? "READY" : "WAITING");

                        ImGui::SameLine();
                        ImGui::TextColored(
                            localReady
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                            "| Local %s",
                            localReady ? "READY" : "WAITING");

                        ImGui::SameLine();
                        ImGui::TextColored(
                            schedulerRunning
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.70f, 0.70f, 0.70f, 1.0f),
                            "| Workers %s",
                            schedulerRunning ? "RUNNING" : "IDLE");

                        ImGui::SeparatorText("Target");

                        ImGui::Text(
                            "%s | PID %u | Base 0x%016llX | %s",
                            connection.processName.empty()
                            ? "<no process>"
                            : connection.processName.c_str(),
                            connection.processId,
                            static_cast<unsigned long long>(
                                connection.targetBaseAddress),
                            mainGame.selectedLocation.empty()
                            ? "no map"
                            : mainGame.selectedLocation.c_str());

                        ImGui::Text(
                            "Registered: %d | Buffered: %d | Cached: %zu",
                            mainGame.registeredPlayersCount,
                            CountNonZeroEntries(
                                mainGame.player_buffer,
                                static_cast<int>(
                                    std::size(mainGame.player_buffer))),
                            playerSnapshot->size());

                        ImGui::SeparatorText("DMA Traffic");

                        ImGui::Text(
                            "Reads: %.0f ops/s | %.0f requests/s | %s",
                            traffic.readOperationsPerSecond,
                            traffic.readRequestsPerSecond,
                            formatDataRate(static_cast<std::size_t>(
                                traffic.readBytesRequestedPerSecond)).c_str());

                        ImGui::Text(
                            "Writes: %.0f ops/s | %.0f requests/s | %s",
                            traffic.writeOperationsPerSecond,
                            traffic.writeRequestsPerSecond,
                            formatDataRate(static_cast<std::size_t>(
                                traffic.writeBytesRequestedPerSecond)).c_str());

                        ImGui::Text(
                            "Failures: %llu read | %llu write | %llu scatter clear",
                            static_cast<unsigned long long>(
                                traffic.readFailures),
                            static_cast<unsigned long long>(
                                traffic.writeFailures),
                            static_cast<unsigned long long>(
                                traffic.scatterClearFailures));

                        if (ImGui::Button("Reset DMA counters"))
                        {
                            mem.ResetTrafficStats();
                            traffic = {};
                        }

                        if (ImGui::CollapsingHeader(
                            "World pointers and state"))
                        {
                            DebugTextPtr(
                                "Game Object Manager",
                                mainGame.gameObjectManager);
                            DebugTextPtr("Game World", mainGame.gameWorld);
                            DebugTextPtr(
                                "Local Game World",
                                mainGame.localGameWorld);
                            DebugTextPtr(
                                "Registered Players",
                                mainGame.registeredPlayers);
                            DebugTextPtr(
                                "Registered Player List",
                                mainGame.registeredPlayersList);

                            ImGui::Text(
                                "Online raid: %s | Radar: %s | Tasks: %s",
                                mainGame.onlineRaid ? "YES" : "NO",
                                appGlobals::runRadar.load(
                                    std::memory_order_acquire)
                                ? "RUNNING"
                                : "STOPPED",
                                schedulerRunning ? "RUNNING" : "STOPPED");
                        }

                        if (ImGui::CollapsingHeader(
                            "Connection and hardware details"))
                        {
                            ImGui::Text(
                                "VMM handle: 0x%016llX | %s",
                                static_cast<unsigned long long>(
                                    connection.vmmHandleAddress),
                                connection.vmmHandleValid
                                ? "VALID"
                                : "INVALID");

                            ImGui::Text(
                                "Libraries: VMM %s | LeechCore %s | FTD3XX %s",
                                connection.vmmLibraryLoaded ? "OK" : "MISSING",
                                connection.leechCoreLibraryLoaded
                                ? "OK"
                                : "MISSING",
                                connection.ftd3xxLibraryLoaded
                                ? "OK"
                                : "MISSING");

                            ImGui::Text(
                                "Target size: %llu bytes",
                                static_cast<unsigned long long>(
                                    connection.targetBaseSize));

                            if (connection.fpgaInfoAvailable)
                            {
                                ImGui::Text(
                                    "FPGA 0x%llX | Device 0x%llX | Firmware %llu.%llu",
                                    static_cast<unsigned long long>(
                                        connection.fpgaId),
                                    static_cast<unsigned long long>(
                                        connection.deviceId),
                                    static_cast<unsigned long long>(
                                        connection.firmwareMajor),
                                    static_cast<unsigned long long>(
                                        connection.firmwareMinor));
                            }
                            else
                            {
                                ImGui::TextDisabled(
                                    "FPGA details unavailable.");
                            }

                            if (connection.cacheInfoAvailable)
                            {
                                ImGui::Text(
                                    "Cache ticks: process %llu/%llu | read %llu | TLB %llu",
                                    static_cast<unsigned long long>(
                                        connection.processCachePartialTicks),
                                    static_cast<unsigned long long>(
                                        connection.processCacheTotalTicks),
                                    static_cast<unsigned long long>(
                                        connection.readCacheTicks),
                                    static_cast<unsigned long long>(
                                        connection.tlbCacheTicks));
                            }
                        }

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Local"))
                    {
                        const PlayerSnapshot snapshot =
                            registeredPlayers.getCacheSnapshot();
                        const Player* localPlayer = nullptr;

                        for (const Player& player : *snapshot)
                        {
                            if (player.isLocal ||
                                (Utils::valid_pointer(
                                    mainGame.localPlayerPtr) &&
                                    player.instance ==
                                        mainGame.localPlayerPtr))
                            {
                                localPlayer = &player;
                                break;
                            }
                        }

                        const bool localPointerReady =
                            Utils::valid_pointer(mainGame.localPlayerPtr);
                        const bool handsReady =
                            Utils::valid_pointer(mainGame.localPlayerHands);

                        ImGui::SeparatorText("Status");

                        ImGui::TextColored(
                            localPointerReady
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                            "Player pointer: %s",
                            localPointerReady ? "READY" : "MISSING");

                        ImGui::SameLine();
                        ImGui::TextColored(
                            handsReady
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                            "| Hands: %s",
                            handsReady ? "READY" : "WAITING");

                        ImGui::SameLine();
                        ImGui::Text(
                            "| Scoped: %s",
                            mainGame.localIsScoped ? "YES" : "NO");

                        if (localPlayer)
                        {
                            ImGui::SeparatorText("Published Player State");

                            ImGui::Text(
                                "%s | %s | Group %s",
                                localPlayer->name.empty()
                                ? "Local Player"
                                : localPlayer->name.c_str(),
                                localPlayer->side.empty()
                                ? "unknown side"
                                : localPlayer->side.c_str(),
                                localPlayer->groupId.empty()
                                ? "-"
                                : localPlayer->groupId.c_str());

                            ImGui::Text(
                                "Position: %.2f, %.2f, %.2f",
                                localPlayer->location.x,
                                localPlayer->location.y,
                                localPlayer->location.z);

                            ImGui::Text(
                                "Rotation: %.2f, %.2f | Health tag: %d",
                                localPlayer->rotation.x,
                                localPlayer->rotation.y,
                                localPlayer->healthETAG);

                            ImGui::Text(
                                "Hands: %s | Ammo: %s (%d/%d)",
                                localPlayer->observedHandsInfo.itemName.empty()
                                ? "-"
                                : localPlayer->observedHandsInfo.itemName.c_str(),
                                localPlayer->observedHandsInfo.ammoName.empty()
                                ? "-"
                                : localPlayer->observedHandsInfo.ammoName.c_str(),
                                localPlayer->observedHandsInfo.chamberCount,
                                localPlayer->observedHandsInfo.magazineCount);
                        }
                        else
                        {
                            ImGui::TextDisabled(
                                "The local player has not been published to the player snapshot yet.");
                        }

                        if (ImGui::CollapsingHeader(
                            "MainGame local values",
                            ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            ImGui::Text(
                                "Position: %.3f, %.3f, %.3f",
                                mainGame.localLocation.x,
                                mainGame.localLocation.y,
                                mainGame.localLocation.z);

                            ImGui::Text(
                                "Rotation: %.3f, %.3f",
                                mainGame.localRotation.x,
                                mainGame.localRotation.y);

                            ImGui::Text(
                                "Group: %s | Savage: %s | Scoped: %s",
                                mainGame.localGroupId.empty()
                                ? "-"
                                : mainGame.localGroupId.c_str(),
                                mainGame.localIsSavage ? "YES" : "NO",
                                mainGame.localIsScoped ? "YES" : "NO");
                        }

                        if (ImGui::CollapsingHeader("Pointers"))
                        {
                            DebugTextPtr(
                                "Local Player",
                                mainGame.localPlayerPtr);
                            DebugTextPtr(
                                "Local Hands",
                                mainGame.localPlayerHands);
                            DebugTextPtr(
                                "Local PWA",
                                mainGame.localPlayerPWA);
                            DebugTextPtr(
                                "Local Profile",
                                mainGame.localplayerProfile);

                            if (localPlayer)
                            {
                                ImGui::Separator();
                                DebugTextPtr(
                                    "Movement Context",
                                    localPlayer->P_MovementContext);
                                DebugTextPtr(
                                    "Rotation Address",
                                    localPlayer->P_RotationAddress);
                                DebugTextPtr(
                                    "Hands Controller Address",
                                    localPlayer->P_HandsControllerAddr);
                                DebugTextPtr(
                                    "Bone Matrix",
                                    localPlayer->playerBoneMatrixPtr);
                            }
                        }

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Players"))
                    {
                        const PlayerSnapshot cacheSnapshot =
                            registeredPlayers.getCacheSnapshot();
                        const PlayerCollection& cache = *cacheSnapshot;

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

                        auto GetPlayerType = [](const Player& player) -> const char*
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

                        auto GetStateText = [](const Player& player) -> const char*
                            {
                                if (!Utils::valid_pointer(player.instance))
                                    return "INVALID";

                                if (player.hasExfiled)
                                    return "EXFIL";

                                if (player.isDead)
                                    return "DEAD";

                                return "LIVE";
                            };

                        auto GetStateColour = [](const Player& player) -> ImVec4
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

                        auto BonePtrAt = [&](const Player& player,
                            boneListIndexes index) -> uint64_t
                            {
                                const size_t slot =
                                    static_cast<size_t>(index);

                                if (slot >= player.bonePtrs.size())
                                    return 0;

                                return player.bonePtrs[slot];
                            };

                        auto BonePositionAt = [&](const Player& player,
                            boneListIndexes index) -> glm::vec3
                            {
                                const size_t slot =
                                    static_cast<size_t>(index);

                                if (slot >= player.bonePositions.size())
                                    return glm::vec3(0.0f);

                                return player.bonePositions[slot];
                            };

                        auto CountValidBonePtrs = [&](const Player& player) -> size_t
                            {
                                size_t count = 0;

                                for (const uint64_t ptr : player.bonePtrs)
                                {
                                    if (IsValidPtr(ptr))
                                        ++count;
                                }

                                return count;
                            };

                        auto HasMinimalBones = [&](const Player& player) -> bool
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

                        auto DrawPlayerTooltip = [&](const Player& player)
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

                        for (const Player& player : cache)
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
                            "##PlayerTable",
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
                                    const Player& player =
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

                        const Player* selectedPlayer = nullptr;

                        for (const Player& player : cache)
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
                            ImVec2(0.0f, 220.0f),
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
                            const Player& player =
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
                                "Equipment Cache"))
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
                                "Pointers"))
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
                                "Bones"))
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
                    if (ImGui::BeginTabItem("Camera"))
                    {
                        const CameraProjectionSnapshot cameraSnapshot =
                            camera.getProjectionSnapshot();
                        const CameraProjectionState& cameraState =
                            *cameraSnapshot;
                        const auto& matrixDebug = cameraState.matrixDebug;
                        const bool fpsReady = cameraState.fpsPointersReady;
                        const bool opticReady = cameraState.opticPointersReady;
                        const bool fovValid =
                            std::isfinite(cameraState.gameFOV) &&
                            cameraState.gameFOV > 1.0f &&
                            cameraState.gameFOV < 180.0f;
                        const bool aspectValid =
                            std::isfinite(cameraState.gameAspect) &&
                            cameraState.gameAspect > 0.1f &&
                            cameraState.gameAspect < 10.0f;
                        const bool activeMatrixValid =
                            cameraState.usingOptic
                            ? matrixDebug.opticMatrixValid
                            : matrixDebug.fpsMatrixValid;
                        const bool cameraHealthy =
                            cameraState.valid && fpsReady && fovValid &&
                            aspectValid && activeMatrixValid;

                        ImGui::SeparatorText("Status");
                        ImGui::TextColored(
                            cameraHealthy
                            ? ImVec4(0.35f, 0.90f, 0.45f, 1.0f)
                            : ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
                            "Camera %s",
                            cameraHealthy ? "READY" : "NEEDS ATTENTION");
                        ImGui::SameLine();
                        ImGui::Text(
                            "| Active %s | FOV %.2f | Aspect %.3f",
                            cameraState.usingOptic ? "OPTIC" : "FPS",
                            cameraState.gameFOV,
                            cameraState.gameAspect);
                        ImGui::Text(
                            "FPS path: %s | Optic path: %s | Scoped: %s",
                            fpsReady ? "READY" : "MISSING",
                            opticReady ? "READY" : "MISSING",
                            mainGame.localIsScoped ? "YES" : "NO");

                        if (mainGame.localIsScoped && !opticReady)
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.65f, 0.20f, 1.0f),
                                "Warning: scoped, but the optic camera path is unavailable.");
                        }
                        if (!activeMatrixValid)
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                "Warning: the active view matrix is invalid.");
                        }
                        if (!fovValid || !aspectValid)
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                "Warning: lens values are outside their expected range.");
                        }
                        if (matrixDebug.localScoped &&
                            matrixDebug.opticMatrixActive &&
                            !cameraState.usingOptic)
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.65f, 0.20f, 1.0f),
                                "Warning: optic activity was detected, but FPS is selected.");
                        }
                        if (!matrixDebug.opticMatrixActive &&
                            cameraState.usingOptic)
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.65f, 0.20f, 1.0f),
                                "Warning: optic is selected while its matrix is inactive.");
                        }

                        if (ImGui::Button("Refresh pointers"))
                        {
                            camera.getCameraPtrs();
                            camera.getMatrixPtrs();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear camera cache"))
                            camera.clearCache();

                        if (ImGui::CollapsingHeader("Optic selection details"))
                        {
                            DebugTextBool("Local scoped", matrixDebug.localScoped);
                            DebugTextBool("FPS matrix valid", matrixDebug.fpsMatrixValid);
                            DebugTextBool("Optic matrix valid", matrixDebug.opticMatrixValid);
                            DebugTextBool("Optic matrix changed", matrixDebug.opticMatrixChanged);
                            DebugTextBool("Optic matrix active", matrixDebug.opticMatrixActive);
                            DebugTextBool("Using optic matrix", matrixDebug.usingOpticMatrix);
                            ImGui::Text(
                                "Tick: %d | Static samples: %d | Diff: %.8f",
                                matrixDebug.activityTick,
                                matrixDebug.noChangeSamples,
                                matrixDebug.opticMatrixDiff);
                            ImGui::TextDisabled(
                                "%s",
                                matrixDebug.localScoped &&
                                    matrixDebug.opticMatrixActive
                                ? "Decision: scoped with active optic matrix."
                                : matrixDebug.localScoped
                                ? "Decision: scoped, optic static; using FPS."
                                : "Decision: not scoped; using FPS.");
                        }

                        if (ImGui::CollapsingHeader("Pointer details"))
                        {
                            DebugTextPtr("FPS camera", cameraState.fpsCamera);
                            DebugTextPtr("FPS matrix address", cameraState.fpsMatrixAddress);
                            DebugTextPtr("Optic camera", cameraState.opticCamera);
                            DebugTextPtr("Optic matrix address", cameraState.opticMatrixAddress);
                            DebugTextPtr("Camera entity", cameraState.cameraEntity);
                            DebugTextPtr("Optic camera matrix", cameraState.opticCameraMatrix);
                        }

                        if (ImGui::CollapsingHeader("Matrix validation"))
                        {
                            DebugMatrixSummary("FPS raw", cameraState.fpsRawMatrix);
                            DebugMatrixSummary("FPS transposed", cameraState.fpsViewMatrix);
                            DebugMatrixSummary("Optic raw", cameraState.opticRawMatrix);
                            DebugMatrixSummary("Optic transposed", cameraState.opticViewMatrix);
                        }

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Aim"))
                    {
                        const std::optional<TargetResult> liveTarget =
                            readOnlyAim.getLiveTarget();
                        const std::optional<TargetResult> activeTarget =
                            readOnlyAim.getActiveTarget();
                        const AimReferencePoint aimReference =
                            readOnlyAim.resolveAimReference();
                        const bool cameraReady =
                            camera.cameraPointersReady();
                        const bool deviceReady = makcu.IsConnected();
                        const bool referenceReady =
                            aimGlobals::aimReference !=
                                AimReference::Fireport ||
                            aimReference.valid;

                        ImGui::SeparatorText("Pipeline");
                        ImGui::Text(
                            "Enabled: %s | Worker: %s | Camera: %s | Device: %s",
                            aimGlobals::aimEnabled ? "YES" : "NO",
                            appGlobals::runThreads.load(
                                std::memory_order_acquire)
                            ? "RUNNING"
                            : "IDLE",
                            cameraReady ? "READY" : "MISSING",
                            deviceReady ? "CONNECTED" : "DISCONNECTED");
                        ImGui::Text(
                            "Reference: %s at %.1f, %.1f | %s",
                            aimGlobals::aimReference ==
                                AimReference::Fireport
                            ? aimReference.fallbackToCrosshair
                                ? "FIREPORT -> CROSSHAIR"
                                : "FIREPORT"
                            : "CROSSHAIR",
                            aimReference.pos.x,
                            aimReference.pos.y,
                            referenceReady ? "VALID" : "INVALID");

                        if (aimGlobals::aimEnabled &&
                            aimReference.fallbackToCrosshair)
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.70f, 0.25f, 1.0f),
                                "Fireport unavailable for this weapon; using screen centre fallback.");
                        }

                        if (aimGlobals::aimEnabled &&
                            (!cameraReady ||
                                !deviceReady ||
                                !referenceReady))
                        {
                            ImGui::TextColored(
                                ImVec4(1.0f, 0.55f, 0.20f, 1.0f),
                                "Aim is enabled, but its input pipeline is not ready.");
                        }

                        ImGui::SeparatorText("Selection");
                        ImGui::Text(
                            "Mode: %s | Lock: %s | FOV: %.0f px | Range: %d m",
                            aimGlobals::targetMode == TargetMode::CQB
                            ? "CQB"
                            : "FOV",
                            aimGlobals::targetLock ? "ON" : "OFF",
                            aimGlobals::aimFOV,
                            aimGlobals::aimDistance);
                        ImGui::Text(
                            "Smoothing: %.2f | AI bone: %d | PMC bone: %d",
                            aimGlobals::aimSmooth,
                            static_cast<int>(aimGlobals::aiBone),
                            static_cast<int>(aimGlobals::pmcBone));
                        ImGui::Text(
                            "Speed: %.0f px/s | Deadzone: %.1f px | Offset: %.1f, %.1f",
                            aimGlobals::aimSpeedPixelsPerSecond,
                            aimGlobals::aimDeadzonePixels,
                            aimGlobals::aimOffsetX,
                            aimGlobals::aimOffsetY);

                        auto drawTarget = [&](const char* label,
                            const std::optional<TargetResult>& target)
                            {
                                ImGui::SeparatorText(label);
                                if (!target)
                                {
                                    ImGui::TextDisabled("No valid target");
                                    return;
                                }

                                const float errorX =
                                    target->screenPos.x - aimReference.pos.x;
                                const float errorY =
                                    target->screenPos.y - aimReference.pos.y;
                                ImGui::Text(
                                    "%s | %s | %.1f m | %.1f px",
                                    target->player.name.empty()
                                    ? "<unnamed>"
                                    : target->player.name.c_str(),
                                    target->player.isAi ? "AI" : "PLAYER",
                                    std::sqrt(target->worldDistanceSq),
                                    std::sqrt(target->screenDistanceSq));
                                ImGui::Text(
                                    "Bone %d | Screen %.1f, %.1f | Error %.1f, %.1f px",
                                    static_cast<int>(target->selectedBone),
                                    target->screenPos.x,
                                    target->screenPos.y,
                                    errorX,
                                    errorY);

                                ImGui::PushID(label);
                                if (ImGui::CollapsingHeader("Details"))
                                {
                                    ImGui::Text(
                                        "Instance: 0x%016llX",
                                        static_cast<unsigned long long>(
                                            target->player.instance));
                                    ImGui::Text(
                                        "World bone: %.2f, %.2f, %.2f",
                                        target->boneWorldPos.x,
                                        target->boneWorldPos.y,
                                        target->boneWorldPos.z);
                                }
                                ImGui::PopID();
                            };

                        drawTarget("Best candidate", liveTarget);
                        drawTarget("Locked / active target", activeTarget);
                        ImGui::TextDisabled(
                            "Configure aim in the MAKCU Aim tab; this page is runtime diagnostics.");

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Features"))
                    {
                        const PlayerSnapshot featurePlayers =
                            registeredPlayers.getCacheSnapshot();
                        const std::vector<LootEntity> lootCache =
                            Loot.getCacheLoot();
                        const std::size_t grenadeCount =
                            explosiveManager.getGrenadeCount();
                        const std::vector<QuestData> activeQuests =
                            GetQuestDataActiveSnapshot();
                        const std::size_t equipmentReady =
                            static_cast<std::size_t>(std::count_if(
                                featurePlayers->begin(),
                                featurePlayers->end(),
                                [](const Player& player)
                                {
                                    return player.equipInited;
                                }));

                        ImGui::SeparatorText("Cache overview");
                        if (ImGui::BeginTable(
                            "FeatureCacheOverview",
                            5,
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_SizingStretchProp))
                        {
                            ImGui::TableSetupColumn("Feature");
                            ImGui::TableSetupColumn("Requested");
                            ImGui::TableSetupColumn("Cached");
                            ImGui::TableSetupColumn("Interval");
                            ImGui::TableSetupColumn("Lane");
                            ImGui::TableHeadersRow();

                            auto drawFeatureRow = [](
                                const char* name,
                                const char* requested,
                                std::size_t cached,
                                double intervalMs,
                                const char* lane)
                                {
                                    ImGui::TableNextRow();
                                    ImGui::TableSetColumnIndex(0);
                                    ImGui::TextUnformatted(name);
                                    ImGui::TableSetColumnIndex(1);
                                    ImGui::TextUnformatted(requested);
                                    ImGui::TableSetColumnIndex(2);
                                    ImGui::Text("%zu", cached);
                                    ImGui::TableSetColumnIndex(3);
                                    ImGui::Text("%.0f ms", intervalMs);
                                    ImGui::TableSetColumnIndex(4);
                                    ImGui::TextUnformatted(lane);
                                };

                            drawFeatureRow(
                                "Equipment",
                                radarGlobals::getPlayerEquip ? "RADAR" : "OFF",
                                equipmentReady,
                                globals::taskPlayersEquipment,
                                "BACKGROUND");
                            drawFeatureRow(
                                "Loot",
                                radarGlobals::drawLoot && espGlobals::drawLoot
                                ? "RADAR + ESP"
                                : radarGlobals::drawLoot
                                ? "RADAR"
                                : espGlobals::drawLoot ? "ESP" : "OFF",
                                lootCache.size(),
                                globals::taskLoot,
                                "BACKGROUND");
                            drawFeatureRow(
                                "Grenades",
                                radarGlobals::drawGrenades &&
                                    espGlobals::drawGrenades
                                ? "RADAR + ESP"
                                : radarGlobals::drawGrenades
                                ? "RADAR"
                                : espGlobals::drawGrenades ? "ESP" : "OFF",
                                grenadeCount,
                                globals::taskGrenades,
                                "HIGH");
                            drawFeatureRow(
                                "Quests",
                                radarGlobals::drawQuestHelper &&
                                    espGlobals::drawQuestHelper
                                ? "RADAR + ESP"
                                : radarGlobals::drawQuestHelper
                                ? "RADAR"
                                : espGlobals::drawQuestHelper ? "ESP" : "OFF",
                                activeQuests.size(),
                                globals::taskQuest,
                                "BACKGROUND");
                            drawFeatureRow(
                                "Exfils",
                                radarGlobals::drawExfils &&
                                    espGlobals::drawExfil
                                ? "RADAR + ESP"
                                : radarGlobals::drawExfils
                                ? "RADAR"
                                : espGlobals::drawExfil ? "ESP" : "OFF",
                                0,
                                globals::taskExfil,
                                "NORMAL");
                            ImGui::EndTable();
                        }

                        ImGui::TextDisabled(
                            "Exfil count is omitted until its legacy cache exposes a safe snapshot.");

                        if (ImGui::CollapsingHeader("Feature switches"))
                        {
                            ImGui::Text(
                                "Radar: players %s | loot %s | grenades %s | quests %s | exfils %s",
                                radarGlobals::drawPlayers ? "ON" : "OFF",
                                radarGlobals::drawLoot ? "ON" : "OFF",
                                radarGlobals::drawGrenades ? "ON" : "OFF",
                                radarGlobals::drawQuestHelper ? "ON" : "OFF",
                                radarGlobals::drawExfils ? "ON" : "OFF");
                            ImGui::Text(
                                "ESP: %s | players %s | loot %s | grenades %s | quests %s | exfils %s",
                                espGlobals::espEnabled ? "ON" : "OFF",
                                espGlobals::drawPlayers ? "ON" : "OFF",
                                espGlobals::drawLoot ? "ON" : "OFF",
                                espGlobals::drawGrenades ? "ON" : "OFF",
                                espGlobals::drawQuestHelper ? "ON" : "OFF",
                                espGlobals::drawExfil ? "ON" : "OFF");
                        }

                        if (ImGui::CollapsingHeader("Grenade source details"))
                        {
                            DebugTextPtr(
                                "Local game world",
                                explosiveManager.getLocalGameWorld());
                            DebugTextPtr(
                                "Controller",
                                explosiveManager.getGrenadesController());
                            DebugTextPtr(
                                "Unity list",
                                explosiveManager.getGrenadesListPointer());
                            ImGui::Text(
                                "Last list count: %zu | Read: %s",
                                explosiveManager.getLastUnityListCount(),
                                explosiveManager.lastUnityListReadSucceeded()
                                ? "OK"
                                : "FAILED / NOT RUN");
                        }

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Loot"))
                    {
                        const std::vector<LootEntity> cacheLoot = Loot.getCacheLoot();

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

                            if (item.isItem())
                                ++itemCount;

                            if (item.isQuestItem())
                                ++questItemCount;

                            if (item.isContainer())
                                ++containerCount;

                            if (item.isCorpse())
                                ++corpseCount;

                            if (item.isAirdrop())
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

                                    if (!item.isAirdrop())
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
        }
        if (activePage == DebugPage::Map)
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

        }
        menuLayout::PopContentInset();
        ImGui::EndChild();
    }
    ImGui::End();

}

static void renderLeftIcons()
{
    constexpr float buttonSize = menuLayout::WidgetToolbarButtonSize;
    constexpr float buttonSpacing = menuLayout::WidgetToolbarGap;
    constexpr float leftMargin = menuLayout::WidgetToolbarX;
    constexpr float topMargin = menuLayout::WidgetToolbarY;

    const auto leftButton = [&](const char* icon, int index)
    {
        ImGui::SetCursorPos(ImVec2(
            leftMargin + (index * (buttonSize + buttonSpacing)),
            topMargin));
        return ImGui::ButtonMenu(
            icon,
            ImVec2(buttonSize, buttonSize),
            ImVec2(0.0f, 2.5f));
    };

    const auto showTooltip = [](const char* tooltip)
    {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
            return;

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    };

    const auto toggleWidget = [](bool& widget)
    {
        const bool shouldOpen = !widget;
        appMenu::widgetLoot = false;
        appMenu::widgetExfil = false;
        appMenu::widgetTopLoot = false;
        appMenu::widgetPlayers = false;
        widget = shouldOpen;
    };

    if (leftButton(ICON_FK_STREET_VIEW, 0))
    {
        mapGlobals::followLocal = !mapGlobals::followLocal;
        mapGlobals::focusPoint = { 0.f, 0.f, 0.f };
    }
    showTooltip("Follow local");

    if (leftButton(ICON_FA_BOX_OPEN, 1))
        toggleWidget(appMenu::widgetLoot);
    showTooltip("Loot");

    if (leftButton(ICON_FK_SIGN_OUT, 2))
        toggleWidget(appMenu::widgetExfil);
    showTooltip("Raid extracts");

    if (leftButton(ICON_FK_CUBES, 3))
        toggleWidget(appMenu::widgetTopLoot);
    showTooltip("Top loot");

    if (leftButton(ICON_FK_USERS, 4))
        toggleWidget(appMenu::widgetPlayers);
    showTooltip("Active players");

    if (!appGlobals::runRadar.load(std::memory_order_acquire))
        return;

    bool hasBlackDivision = false;
    bool hasCultist = false;
    bool hasBoss = false;
    const PlayerSnapshot playerSnapshot = registeredPlayers.getCacheSnapshot();

    for (const Player& player : *playerSnapshot)
    {
        if (!Utils::valid_pointer(player.instance) ||
            player.isDead ||
            player.hasExfiled)
        {
            continue;
        }

        if (player.isBlackDivision)
        {
            hasBlackDivision = true;
            continue;
        }

        if (player.isCultist)
        {
            hasCultist = true;
            continue;
        }

        if (player.isBoss)
            hasBoss = true;
    }

    constexpr float noticeFontSize = 12.0f;
    constexpr float noticeHeight = 16.0f;
    constexpr float noticeSpacing = 2.0f;
    float noticeX = leftMargin + (5.0f * (buttonSize + buttonSpacing));
    float noticeY = 2.0f;
    const auto renderRaidNotice = [&](const char* label, const glm::vec4& colour)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const ImVec2 textSize = font->CalcTextSizeA(
            noticeFontSize,
            std::numeric_limits<float>::max(),
            0.0f,
            label);
        const ImVec2 windowPosition = ImGui::GetWindowPos();
        const ImVec2 position(
            windowPosition.x + noticeX,
            windowPosition.y + noticeY);
        const ImVec2 size(textSize.x + 12.0f, noticeHeight);
        const ImVec2 bottomRight(
            position.x + size.x,
            position.y + size.y);
        const ImVec2 textPosition(
            position.x + 6.0f,
            position.y + ((noticeHeight - textSize.y) * 0.5f));
        const ImU32 textColour = IM_COL32(
            static_cast<int>(colour.r * 255.0f),
            static_cast<int>(colour.g * 255.0f),
            static_cast<int>(colour.b * 255.0f),
            static_cast<int>(colour.a * 255.0f));

        drawList->AddRectFilled(
            position,
            bottomRight,
            IM_COL32(35, 37, 40, 245),
            2.0f);
        drawList->AddRect(position, bottomRight, textColour, 2.0f);
        drawList->AddText(font, noticeFontSize, textPosition, textColour, label);

        if (ImGui::IsMouseHoveringRect(position, bottomRight, true))
            ImGui::SetTooltip("Detected in this raid");

        noticeY += noticeHeight + noticeSpacing;
    };

    if (hasBlackDivision)
        renderRaidNotice("Black Division", coloursGlobals::playerBlackDiv);

    if (hasCultist)
        renderRaidNotice("Cultist", coloursGlobals::playerBoss);

    if (hasBoss)
        renderRaidNotice("Boss", coloursGlobals::playerBoss);
}

static void renderMenuIcons()
{
    // Icons Menu

    std::string settingIcon = ICON_FK_COGS;
    std::string fuserIcon = ICON_FK_TELEVISION;
    std::string filterIcon = ICON_FK_FILTER;
    std::string makcuIcon = ICON_FK_CROSSHAIRS;
    std::string questsIcon = ICON_FK_FILES_O;
    std::string watchlistIcon = ICON_FK_USER;
    std::string widgetDebugIcon = ICON_FK_STETHOSCOPE;

    // view port
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float sidebarButtonSize = 42.0f;
    constexpr float sidebarRightMargin = 10.0f;
    constexpr float sidebarTop = 10.0f;
    constexpr float sidebarStep = 48.0f;
    const float sidebarX = viewport->Size.x - sidebarButtonSize - sidebarRightMargin;
    const ImVec4 sidebarIconColour(0.94f, 0.22f, 0.25f, 1.0f);

    const auto sidebarButton = [&](const char* icon, float y)
    {
        ImGui::SetCursorPos(ImVec2(sidebarX, y));
        ImGui::PushStyleColor(ImGuiCol_Text, sidebarIconColour);
        const bool pressed = ImGui::ButtonMenu(
            icon,
            ImVec2(sidebarButtonSize, sidebarButtonSize),
            ImVec2(0.0f, 0.0f));
        ImGui::PopStyleColor();
        return pressed;
    };

    const auto topMenuButton = [&](const char* icon, float x)
    {
        ImGui::SetCursorPos(ImVec2(x, sidebarTop));
        ImGui::PushStyleColor(ImGuiCol_Text, sidebarIconColour);
        const bool pressed = ImGui::ButtonMenu(
            icon,
            ImVec2(sidebarButtonSize, sidebarButtonSize),
            ImVec2(0.0f, 0.0f));
        ImGui::PopStyleColor();
        return pressed;
    };

    // Settings Icon
    if (sidebarButton(settingIcon.c_str(), sidebarTop))
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

    if (sidebarButton(fuserIcon.c_str(), sidebarTop + sidebarStep)) {
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


    if (sidebarButton(makcuIcon.c_str(), sidebarTop + (sidebarStep * 2.0f))) {
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

    if (sidebarButton(filterIcon.c_str(), sidebarTop + (sidebarStep * 3.0f))) {
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

    if (sidebarButton(questsIcon.c_str(), sidebarTop + (sidebarStep * 4.0f))) {
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

    if (sidebarButton(watchlistIcon.c_str(), sidebarTop + (sidebarStep * 5.0f))) {
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

    if (topMenuButton(widgetDebugIcon.c_str(), sidebarX - sidebarStep))
    {
        appMenu::widgetDebug = !appMenu::widgetDebug;
    }
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Debug");
        ImGui::EndTooltip();
    }

    if (appMenu::appSettings)
        renderMenuSettings(); // display settings menu screen
    if (appMenu::appLootFilters)
        uiWidgets::renderLootFiltersMenu(); // display loot filters screen

    if (appMenu::widgetLoot)
        uiWidgets::renderRaidLootWidget();

    if (appMenu::widgetDebug)
        renderDebugWindow();

    if (appMenu::appFuser)
        uiWidgets::renderFuserWindow();

    if (appMenu::appQuests)
        uiWidgets::renderQuestsWindow();

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

            ImFont* radarFont = GetSelectedRadarFont();
            if (radarFont != nullptr)
                ImGui::PushFont(radarFont);

            DrawRadarMainText(centerScreen.x, centerScreen.y, { 1,0,0,1 }, Text);

            const bool showDmaConnectionHint =
                !dmaConnected && !working && !stopping;
            const float statusTextY = showDmaConnectionHint
                ? centerScreen.y + 72.0f
                : centerScreen.y + 45.0f;

            if (showDmaConnectionHint)
            {
                DrawRadarSubText(
                    centerScreen.x,
                    centerScreen.y + 42.0f,
                    { 1,1,1,1 },
                    "Connect from Settings menu -->");
            }

            DrawRadarSubText(
                centerScreen.x,
                statusTextY,
                { 1,1,1,1 },
                globals::radarSubText.c_str());

            setCurrentMapSpecs = false;

            if (radarFont != nullptr)
                ImGui::PopFont();


        }
        else
        {
            // consider in raid? render what we only have access to in raid!
            uiWidgets::ensureSelectedMapLoaded();
            uiWidgets::renderMapDetails();

            ImFont* radarFont = GetSelectedRadarFont();
            if (radarFont != nullptr)
                ImGui::PushFont(radarFont);

            //render what we want on map as runRadar is true
            drawLocalPlayer();
            drawPlayers();
            drawExfils();
            drawGrenades();
            drawTripwires();
            drawLoot();

            drawQuests();

            if (radarFont != nullptr)
                ImGui::PopFont();

            renderRadarPlayerCounts();

            if (!radarGlobals::minimalView)
                g_AimViewWidget.Render((ImVec2&)espGlobals::gameRes);


        }

        drawWidgetPlayers();
        drawWidgetExfils();
        drawWidgetTopLoot();

        renderLeftIcons();
        renderMenuIcons();
        renderBottomInfo();
    }
    ImGui::End();

}

static void renderVersionMismatchNotice()
{
    if (!globals::showVersionMismatchWarning)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        viewport->GetCenter(),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f)
    );
    ImGui::SetNextWindowSize(
        ImVec2(460.0f, 0.0f),
        ImGuiCond_Appearing
    );

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Version notice###VersionMismatchNotice", &globals::showVersionMismatchWarning, flags))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetWindowFocus();

        ImGui::TextWrapped(
            "Application version outdated"
        );
        ImGui::Spacing();
        ImGui::Text("Installed version: %s", globals::appVersion.c_str());
        ImGui::Text("Latest version:  %s", globals::latestAppVersion.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped(
            "You can continue using this version. Updating is recommended."
        );
        ImGui::Spacing();

        if (ImGui::Button("Continue", ImVec2(120.0f, 0.0f)))
            globals::showVersionMismatchWarning = false;
    }
    ImGui::End();
}

static void load_styles()
{
    ImVec4* colors = ImGui::GetStyle().Colors;
    // Compact charcoal theme: neutral input boxes, red accents, and no green.
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.91f, 0.89f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.48f, 0.46f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.045f, 0.050f, 0.055f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.065f, 0.070f, 0.075f, 0.96f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.090f, 0.095f, 0.100f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.28f, 0.29f, 0.29f, 0.82f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.26f, 0.27f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.34f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.40f, 0.40f, 0.39f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.070f, 0.075f, 0.080f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.105f, 0.045f, 0.050f, 1.00f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.090f, 0.095f, 0.100f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.035f, 0.040f, 0.045f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.31f, 0.31f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.43f, 0.43f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.19f, 0.23f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.73f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(1.00f, 0.30f, 0.33f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.43f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.72f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.36f, 0.08f, 0.11f, 0.90f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.53f, 0.12f, 0.15f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.72f, 0.14f, 0.17f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.35f, 0.12f, 0.14f, 0.72f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.74f, 0.16f, 0.19f, 1.00f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.94f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.48f, 0.12f, 0.15f, 0.58f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.70f, 0.16f, 0.19f, 0.82f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.94f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.13f, 0.14f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.48f, 0.11f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.62f, 0.13f, 0.16f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.38f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.88f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.88f, 0.20f, 0.23f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.66f, 0.13f, 0.16f, 0.68f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.94f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.16f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30f, 0.31f, 0.31f, 0.82f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.20f, 0.21f, 0.22f, 0.82f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.11f, 0.12f, 0.13f, 0.52f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.64f);

    ImGuiStyle* style = &ImGui::GetStyle();
    {
        style->WindowPadding = ImVec2(8, 8);
        style->WindowBorderSize = 1.f;
        style->WindowRounding = 2.f;
        style->ChildRounding = 1.f;
        style->ChildBorderSize = 1.f;
        style->PopupRounding = 1.f;

        style->FramePadding = ImVec2(6, 3);
        style->FrameRounding = 1.f;
        style->FrameBorderSize = 1.f;
        style->ItemSpacing = ImVec2(8, 4);
        style->ItemInnerSpacing = ImVec2(6, 4);
        style->CellPadding = ImVec2(6, 4);
        style->ScrollbarSize = 10.f;
        style->ScrollbarRounding = 1.f;
        style->GrabMinSize = 9.f;
        style->GrabRounding = 1.f;
        style->TabRounding = 1.f;
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
    wc.hIcon = static_cast<HICON>(::LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(::LoadImageW(wc.hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
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
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 17.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());

    // Font Awesome 7 Free (solid). Legacy ICON_FK_* names are mapped in
    // IconsFontAwesomeCompat.h so radar markers retain their established glyphs.
    float iconFontSize = 17.f;
    static const ImWchar icons_ranges[] = {
        ICON_MIN_FA,
        ICON_MAX_16_FA,
        0
    };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    if (!io.Fonts->AddFontFromFileTTF(
        FONT_ICON_FILE_NAME_FAS,
        iconFontSize,
        &icons_config,
        icons_ranges))
    {
        LOGS.logError(
            "Unable to load Font Awesome 7 from "
            FONT_ICON_FILE_NAME_FAS);
    }

    const char* const radarFontPaths[RadarFontFamilyCount][RadarFontWeightCount] =
    {
        { "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\segoeuib.ttf" },
        { "C:\\Windows\\Fonts\\arial.ttf", "C:\\Windows\\Fonts\\arialbd.ttf" },
        { "C:\\Windows\\Fonts\\tahoma.ttf", "C:\\Windows\\Fonts\\tahomabd.ttf" }
    };

    for (int familyIndex = 0; familyIndex < RadarFontFamilyCount; ++familyIndex)
    {
        for (int weightIndex = 0; weightIndex < RadarFontWeightCount; ++weightIndex)
        {
            ImFont* const radarFont = io.Fonts->AddFontFromFileTTF(
                radarFontPaths[familyIndex][weightIndex],
                RadarFontSize,
                NULL,
                io.Fonts->GetGlyphRangesCyrillic()
            );
            radarFonts[familyIndex][weightIndex] = radarFont;

            if (radarFont == nullptr)
            {
                LOGS.logError("Unable to load radar font from " + std::string(radarFontPaths[familyIndex][weightIndex]));
                continue;
            }

            ImFontConfig radarIconsConfig;
            radarIconsConfig.MergeMode = true;
            radarIconsConfig.PixelSnapH = true;
            radarIconsConfig.DstFont = radarFont;
            if (!io.Fonts->AddFontFromFileTTF(
                FONT_ICON_FILE_NAME_FAS,
                RadarFontSize,
                &radarIconsConfig,
                icons_ranges))
            {
                LOGS.logError("Unable to merge Font Awesome 7 into radar font " + std::string(RadarFontNames[familyIndex]));
            }
        }
    }




    //IM_ASSERT(font != nullptr);

    // Our state
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    // Main loop

    while (!done)
    {
        const auto radarFrameStart =
            std::chrono::steady_clock::now();

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
        renderVersionMismatchNotice();

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

        const double radarFpsLimit = std::clamp(
            static_cast<double>(globals::appRadarMaxFPS),
            15.0,
            240.0);
        const auto radarFrameDuration =
            std::chrono::duration<double>(1.0 / radarFpsLimit);

        std::this_thread::sleep_until(
            radarFrameStart +
            std::chrono::duration_cast<
                std::chrono::steady_clock::duration>(
                    radarFrameDuration));
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
    if (g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &g_d3dpp, &g_pd3dDevice) < 0)
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
