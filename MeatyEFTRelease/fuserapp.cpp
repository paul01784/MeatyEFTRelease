#include "app/fuserapp.h"
#include "game/headers/maingame.h"
#include <iostream>
#include "game/headers/utils.h"
#include "game/headers/players.h"
#include "game/headers/loot.h"
#include "game/headers/explosives.h"
#include "game/headers/questManager.h"

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    size_t end = str.find_last_not_of(" \t\n\r");

    return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
}

std::string toLower(const std::string& str) {
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);
    return lowerStr;
}

TestApp testApp;

int TestApp::fuser_fps = 0;

std::vector<MonitorInfo> TestApp::monitors = {};
int TestApp::selectedMonitorIndex = -1;


std::vector<MonitorInfo> TestApp::EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) -> BOOL {
            MONITORINFOEX monitorInfo = {};
            monitorInfo.cbSize = sizeof(MONITORINFOEX);
            if (GetMonitorInfo(hMonitor, &monitorInfo)) {
                auto& monitorList = *reinterpret_cast<std::vector<MonitorInfo>*>(dwData);
                monitorList.push_back({ monitorInfo.szDevice, monitorInfo.rcMonitor });
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors));

    return monitors;
}


D3DCOLOR Vec4ToD3DColor(const glm::vec4& color)
{
    // Scale and clamp each component to [0, 255]
    unsigned char r = static_cast<unsigned char>(glm::clamp(color.r * 255.0f, 0.0f, 255.0f));
    unsigned char g = static_cast<unsigned char>(glm::clamp(color.g * 255.0f, 0.0f, 255.0f));
    unsigned char b = static_cast<unsigned char>(glm::clamp(color.b * 255.0f, 0.0f, 255.0f));
    unsigned char a = static_cast<unsigned char>(glm::clamp(color.a * 255.0f, 0.0f, 255.0f));

    // Combine components into a single DWORD (32-bit color)
    return D3DCOLOR_ARGB(a, r, g, b);
}

//Calls the base class (DXApp) constructor
TestApp::TestApp() :DXApp()
{

}

//Destructor
TestApp::~TestApp()
{

}

//Calls the based class (DXApp) Init()
bool TestApp::Init()
{
    //If it fails return false;
    if (!DXApp::Init())
        return false;

    //Else return true
    return true;
}

//Update test app
void TestApp::Update(float dt)
{

}

void TestApp::RenderHUD(const D3DVIEWPORT9& viewport) {
    RenderText("MeatyEFT", viewport.Width - 100, 20, D3DCOLOR_XRGB(255, 0, 0));

    std::string fpsText = "FPS: " + std::to_string(static_cast<int>(m_FPS));
    fuser_fps = static_cast<int>(m_FPS);  // Update radar view FPS
    RenderText(fpsText.c_str(), viewport.Width - 100, 40, D3DCOLOR_XRGB(255, 0, 0));
}

void TestApp::RenderTasks(const D3DVIEWPORT9& viewport)
{

    if (!espGlobals::drawQuestHelper)
        return;


    const std::string currentMapId = TrimEFT(mainGame.selectedLocation);

    for (auto& loc : masterLocations)
    {
        // Map filter
        if (!(Utils::Text::containsIgnoreCase(loc.mapNameId, currentMapId) ||
            Utils::Text::containsIgnoreCase(currentMapId, loc.mapNameId)))
            continue;

        int distance = static_cast<int>(glm::distance(mainGame.localLocation, loc.pos));
        if (distance > espGlobals::drawLootDist) continue;

        if (loc.objectiveType == "visit" ||
            loc.objectiveType == "plantItem" ||
            loc.objectiveType == "mark")
        {
            glm::vec2 screenPos{};
            if (!Utils::Camera::world_to_screenQuests(loc.pos, &screenPos))
                continue;

            D3DCOLOR taskColor = Vec4ToD3DColor(coloursGlobals::questMarker);

            std::string questText = loc.questName;

            if (loc.objectiveType == "plantItem")
                questText += " (PLANT)";
            else if (loc.objectiveType == "mark")
                questText += " (MARK)";

            AddCircle(screenPos.x, screenPos.y, 2, taskColor, 18, 1);
            RenderText(questText.c_str(), screenPos.x + 6, screenPos.y - 3, taskColor, false);
        }

    }


}

color_t glmVec4ToColorT(const glm::vec4& vec) {
    return color_t(vec.x, vec.y, vec.z, vec.w);
}

void TestApp::RenderNades(const D3DVIEWPORT9& viewport)
{
    std::vector<GrenadeList> nadeCache = explosiveManager.getGrenades();

    for (const auto& nades : nadeCache)
    {
        int distance = static_cast<int>(glm::distance(mainGame.localLocation, nades.worldLocation));

        if (distance > espGlobals::drawGrenadesDist)
            continue;

        glm::vec2 screenPos = {};
        if (!Utils::Camera::world_to_screen(nades.worldLocation, &screenPos)) continue;

        RenderText("NADE", screenPos.x, screenPos.y + 3, Vec4ToD3DColor(coloursGlobals::grenades), TRUE);

        if (distance < 15)
        {
            //warning of close grenade
            RenderText("!!WARNING GRENADE NEAR YOU!!", viewport.Width / 2, viewport.Height - 60, D3DCOLOR_XRGB(255, 0, 0), TRUE);
        }
    }
}

void TestApp::RenderPlayers(const D3DVIEWPORT9& viewport) {

    try
    {
        
        //std::shared_lock<std::shared_mutex> lock(players.getMutex());
        std::vector<PlayerCache>& cache = players.getCache();

        if (cache.empty()) return;

        for (const auto& player : cache) {
            if (!Utils::valid_pointer(player.instance) ||
                player.isZombie || player.isLocal || player.hasExfiled) continue;

            if (!player.isDead)
            {
                if (player.distance > espGlobals::drawPlayerDist)
                    continue;

                glm::vec2 screenPos = {};
                if (!Utils::Camera::world_to_screen(player.location, &screenPos)) continue;

                D3DCOLOR playerColor = Vec4ToD3DColor(player.colour);

                //friend
                bool friendly = false;
                if (player.groupId == mainGame.localGroupId && mainGame.localGroupId != "")
                    friendly = true;

                // Render name and distance
                std::string cleanName = player.name;
                cleanName.erase(std::remove(cleanName.begin(), cleanName.end(), '\0'), cleanName.end());

                std::string info = cleanName + " [" + std::to_string(player.distance) + "m]";
                RenderText(info.c_str(), screenPos.x, screenPos.y + 5, playerColor, true);

                RenderText(player.itemInHand.c_str(),
                    screenPos.x, screenPos.y + 20, playerColor, true);

                // Render head dot
                if (espGlobals::drawHeadDot) {
                    glm::vec2 headPos = {};
                    if (Utils::Camera::world_to_screen(player.bonePositions[boneListIndexes::Head], &headPos)) {
                        AddCircle(headPos.x, headPos.y, 1.5f, playerColor, 18, 1);
                    }
                }

                // Render box
                if (espGlobals::drawBoxPlayers) {
                    float lFoot = (float&)player.bonePositions[7];
                    float rFoot = (float&)player.bonePositions[9];
                    float head = (float&)player.bonePositions[1];


                    glm::vec2 screenHead{};
                    if (!Utils::Camera::world_to_screen(player.bonePositions[1], &screenHead))
                        break;



                    if (screenPos.x == 0.f)
                        break;

                    float padding = 2.f;
                    float height = (float)(screenPos.y - screenHead.y + padding);
                    float width = (height / 2.0f) + padding;
                    glm::vec2 boxStart = { screenPos.x - width / 2 - padding , screenPos.y - height }; // top left

                    DrawCornerBox(boxStart.x, boxStart.y,
                        width, height, 1, playerColor);


                }

                //render skels
                if (espGlobals::drawSkeletons) {

                    //if (espGlobals::skeletonsOnlyClosest && player.instance != camera.closestPlayer)
                    //    continue;

                    std::vector<glm::vec2> boneScreenPositions(player.bonePositions.size());



                    bool offScreen = false;

                    for (size_t i = 0; i < player.bonePositions.size(); i++) {
                        glm::vec2 screenPos = {};
                        Utils::Camera::world_to_screen(player.bonePositions[i], &screenPos);

                        // Print the world and screen positions for debugging
                        //std::cout << "Bone index: " << i
                        //    << ", World Position: (" << player.bonePositions[i].x
                        //    << ", " << player.bonePositions[i].y
                        //    << ", " << player.bonePositions[i].z << ")"
                        //    << ", Screen Position: (" << screenPos.x
                        //    << ", " << screenPos.y << ")\n";

                        // Check for off-screen or invalid positions
                        if (screenPos.x == 0 && screenPos.y == 0) {

                            offScreen = true;
                            break;
                        }

                        boneScreenPositions[i] = glm::vec2(screenPos.x, screenPos.y);
                    }

                    if (offScreen) {

                        continue;
                    }


                    D3DCOLOR colour = Vec4ToD3DColor(player.colour);

                    // Draw skeleton lines
                    DrawLine(boneScreenPositions[boneListIndexes::Head].x, boneScreenPositions[boneListIndexes::Head].y,
                        boneScreenPositions[boneListIndexes::Pelvis].x, boneScreenPositions[boneListIndexes::Pelvis].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::Head].x, boneScreenPositions[boneListIndexes::Head].y,
                        boneScreenPositions[boneListIndexes::LForearm].x, boneScreenPositions[boneListIndexes::LForearm].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::LForearm].x, boneScreenPositions[boneListIndexes::LForearm].y,
                        boneScreenPositions[boneListIndexes::LPalm].x, boneScreenPositions[boneListIndexes::LPalm].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::Head].x, boneScreenPositions[boneListIndexes::Head].y,
                        boneScreenPositions[boneListIndexes::RForearm].x, boneScreenPositions[boneListIndexes::RForearm].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::RForearm].x, boneScreenPositions[boneListIndexes::RForearm].y,
                        boneScreenPositions[boneListIndexes::RPalm].x, boneScreenPositions[boneListIndexes::RPalm].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::Pelvis].x, boneScreenPositions[boneListIndexes::Pelvis].y,
                        boneScreenPositions[boneListIndexes::LThigh].x, boneScreenPositions[boneListIndexes::LThigh].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::LThigh].x, boneScreenPositions[boneListIndexes::LThigh].y,
                        boneScreenPositions[boneListIndexes::LFoot].x, boneScreenPositions[boneListIndexes::LFoot].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::Pelvis].x, boneScreenPositions[boneListIndexes::Pelvis].y,
                        boneScreenPositions[boneListIndexes::RThigh].x, boneScreenPositions[boneListIndexes::RThigh].y,
                        colour, 1);

                    DrawLine(boneScreenPositions[boneListIndexes::RThigh].x, boneScreenPositions[boneListIndexes::RThigh].y,
                        boneScreenPositions[boneListIndexes::RFoot].x, boneScreenPositions[boneListIndexes::RFoot].y,
                        colour, 1);
                }

            }
            else
            {
                /*
                //corpse esp
                glm::vec2 screenPos = {};
                if (!Utils::Camera::world_to_screen(player.bonePositions[boneListIndexes::Base], &screenPos)) continue;

                if (player.distance > espGlobals::drawCorpseDist || espGlobals::drawCorpse == false)
                    continue;

                D3DCOLOR playerColor = Vec4ToD3DColor(coloursGlobals::playerCorpse);

                // Render name and distance
                std::string cleanName = "";
                if (player.isPlayer || player.isBoss)
                    cleanName = player.name;
                else
                    cleanName = "Corpse";

                cleanName.erase(std::remove(cleanName.begin(), cleanName.end(), '\0'), cleanName.end());

                std::string info = cleanName + " [" + std::to_string(player.distance) + "m]";
                RenderText(info.c_str(), screenPos.x, screenPos.y + 5, playerColor, true);
                */
            }
        }
        
    }
    catch (...)
    {
        return;
    }
    
}



void TestApp::RenderLoot() {
    
    if (!espGlobals::drawLoot)
        return;

    std::vector<LootList> lootList;
    {
        lootList = Loot.getCacheLoot();
    }

    for (const auto& loot : lootList) {
        if (!loot.wanted) 
            continue;

        //skip questitems if off
        if (loot.isQuestItem && !espGlobals::drawQuestHelper)
            continue;

        //skip corpse if off
        if (loot.isCorpse && !espGlobals::drawCorpse)
            continue;

        glm::vec2 screenPos = {};
        if (!Utils::Camera::world_to_screen(loot.worldLocation, &screenPos)) continue;

        int distance = static_cast<int>(glm::distance(mainGame.localLocation, loot.worldLocation));
        if (distance > espGlobals::drawLootDist) continue;

        D3DCOLOR lootColor = loot.isQuestItem
            ? Vec4ToD3DColor(coloursGlobals::questMarker)
            : Vec4ToD3DColor(loot.color);

        std::string lootText = loot.shortName + " " + std::to_string(distance) + "m";

        AddCircle(screenPos.x, screenPos.y, 2, lootColor, 18, 1);
        RenderText(lootText.c_str(), screenPos.x + 6, screenPos.y - 3, lootColor, false);
    }
}

// Render
void TestApp::Render() {
    if (!espGlobals::runEsp) return;

    // Clear back buffer
    m_pDevice3D->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);

    if (m_pDevice3D->BeginScene() >= 0) {
        BeginDraw();
        PushRenderState(D3DRS_ANTIALIASEDLINEENABLE, FALSE);

        // Get viewport size
        D3DVIEWPORT9 viewport;
        m_pDevice3D->GetViewport(&viewport);

        // Render basic HUD info
        RenderHUD(viewport);

        
            // Render crosshair
            if (camera.fpsCamera && espGlobals::drawCrosshair && !mainGame.localIsScoped) {
                AddCircle(viewport.Width / 2.0f, viewport.Height / 2.0f, 2,
                    Vec4ToD3DColor(coloursGlobals::crosshair), 10, 1);
            }

            // Render players
            RenderPlayers(viewport);

            // Render grenades
            RenderNades(viewport);

            // Render loot
            if (espGlobals::drawLoot) RenderLoot();

            // Render Tasks
            RenderTasks(viewport);
       

        EndDraw();
        m_pDevice3D->EndScene();
    }

    // Present the backbuffer
    m_pDevice3D->Present(0, 0, 0, 0);
}

void TestApp::OnResetDevice()
{


}

void TestApp::OnLostDevice()
{

}


void TestApp::fuserMain()
{
    //Create instance of test app object
    TestApp* tApp = new TestApp();
    Sleep(1000);


    //Initialize our test app
    if (!tApp->Init())
        return; //exit application

    //Otherwise, call our application loop
    tApp->Run();

    //cleanup directx9
    tApp->CleanUp();
}