
#include "app/includes.h"
#include "app/globals.h"
#include "app/maps.h"
#include "game/headers/maingame.h"


// init values

// CurrentMap values
float currentMap::configX = 0.f;
float currentMap::configY = 0.f;
float currentMap::configScale = 0.f;

// Map values
int customs_mapSizeW = 0;
int customs_mapSizeH = 0;

int fact_base_mapSizeW = 0;
int fact_base_mapSizeH = 0;
int fact_0f_mapSizeW = 0;
int fact_0f_mapSizeH = 0;

int inter_0f_mapSizeW = 0;
int inter_0f_mapSizeH = 0;
int inter_1f_mapSizeW = 0;
int inter_1f_mapSizeH = 0;
int inter_2f_mapSizeW = 0;
int inter_2f_mapSizeH = 0;

int labs_0f_mapSizeW = 0;
int labs_0f_mapSizeH = 0;
int labs_1f_mapSizeW = 0;
int labs_1f_mapSizeH = 0;
int labs_2f_mapSizeW = 0;
int labs_2f_mapSizeH = 0;

int lighthouse_mapSizeW = 0;
int lighthouse_mapSizeH = 0;

int reserve_base_mapSizeW = 0;
int reserve_base_mapSizeH = 0;
int reserve_0f_mapSizeW = 0;
int reserve_0f_mapSizeH = 0;

int shoreline_0f_mapSizeW = 0;
int shoreline_0f_mapSizeH = 0;

int streets_0f_mapSizeW = 0;
int streets_0f_mapSizeH = 0;

int woods_0f_mapSizeW = 0;
int woods_0f_mapSizeH = 0;

int gz_0f_mapSizeW = 0;
int gz_0f_mapSizeH = 0;

int ib_0f_mapSizeW = 0;
int ib_0f_mapSizeH = 0;
int ib_1f_mapSizeW = 0;
int ib_1f_mapSizeH = 0;
int ib_2f_mapSizeW = 0;
int ib_2f_mapSizeH = 0;
int ib_3f_mapSizeW = 0;
int ib_3f_mapSizeH = 0;
int ib_4f_mapSizeW = 0;
int ib_4f_mapSizeH = 0;
int ib_5f_mapSizeW = 0;
int ib_5f_mapSizeH = 0;
int ib_6f_mapSizeW = 0;
int ib_6f_mapSizeH = 0;
int ib_7f_mapSizeW = 0;
int ib_7f_mapSizeH = 0;
int ib_8f_mapSizeW = 0;
int ib_8f_mapSizeH = 0;
int ib_9f_mapSizeW = 0;
int ib_9f_mapSizeH = 0;
int ib_10f_mapSizeW = 0;
int ib_10f_mapSizeH = 0;
int ib_11f_mapSizeW = 0;
int ib_11f_mapSizeH = 0;

// map settings

// Customs
PDIRECT3DTEXTURE9 customs_texture = NULL;
float customs_texture0_MinHeight = -0.8f;

int customs_orgW = 6371;
int customs_orgH = 3205;

float customs_configX = 2270.f;
float customs_configY = 1467.f;
float customs_configScale = 5.4f;

// Factory
PDIRECT3DTEXTURE9 factory_textureBase = NULL;
float factory_textureBase_MinHeight = -100.f;

PDIRECT3DTEXTURE9 factory_texture0 = NULL;
float factory_texture0_MinHeight = -0.8f;

int factory_orgW = 2489;
int factory_orgH = 2551;

float factory_configX = 1160.f;
float factory_configY = 1211.f;
float factory_configScale = 17.80f;

// Interchange
PDIRECT3DTEXTURE9 interchange_texture0 = NULL;
float interchange_texture0_MinHeight = -100.f;

PDIRECT3DTEXTURE9 interchange_texture1 = NULL;
float interchange_texture1_MinHeight = 26.f;

PDIRECT3DTEXTURE9 interchange_texture2 = NULL;
float interchange_texture2_MinHeight = 31.f;

int interchange_orgW = 3348;
int interchange_orgH = 2637;

float interchange_configX = 1566.f;
float interchange_configY = 1285.2f;
float interchange_configScale = 2.78f;

// Labs
PDIRECT3DTEXTURE9 labs_texture0 = NULL;
float labs_texture0_MinHeight = -100.f;

PDIRECT3DTEXTURE9 labs_texture1 = NULL;
float labs_texture1_MinHeight = -1.f;

PDIRECT3DTEXTURE9 labs_texture2 = NULL;
float labs_texture2_MinHeight = -3.f;

int labs_orgW = 2912;
int labs_orgH = 3184;

float labs_configX = 3107.f;
float labs_configY = -1062.2f;
float labs_configScale = 8.22f;

// Lighthouse
PDIRECT3DTEXTURE9 lighthouse_texture = NULL;
float lighthouse_texture0_MinHeight = -100.f;

int lighthouse_orgW = 2020;
int lighthouse_orgH = 3240;

float lighthouse_configX = 1068.f;
float lighthouse_configY = 1245.f;
float lighthouse_configScale = 1.933f;

// Reserve
PDIRECT3DTEXTURE9 reserve_texture_base = NULL;
float reserve_texture_base_MinHeight = -100.f;

PDIRECT3DTEXTURE9 reserve_texture0 = NULL;
float reserve_texture0_MinHeight = -8.f;


int reserve_orgW = 3648;
int reserve_orgH = 3240;

float reserve_configX = 1905.f;
float reserve_configY = 1577.f;
float reserve_configScale = 5.99f;

// Shoreline

PDIRECT3DTEXTURE9 shoreline_texture0 = NULL;
float shoreline_texture0_MinHeight = -100.f;

int shoreline_orgW = 6500;
int shoreline_orgH = 4885;

float shoreline_configX = 4230.f;
float shoreline_configY = 3014.f;
float shoreline_configScale = 3.73f;

// Streets

PDIRECT3DTEXTURE9 streets_texture0 = NULL;
float streets_texture0_MinHeight = -100.f;

int streets_orgW = 3017;
int streets_orgH = 3763;

float streets_configX = 1402.7f;
float streets_configY = 2742.f;
float streets_configScale = 5.032f;

// Woods

PDIRECT3DTEXTURE9 woods_texture0 = NULL;
float woods_texture0_MinHeight = -100.f;


int woods_orgW = 2307;
int woods_orgH = 2500;

float woods_configX = 1255.f;
float woods_configY = 801.f;
float woods_configScale = 1.776f;


// GroundZero

PDIRECT3DTEXTURE9 gz_texture0 = NULL;
float gz_texture0_MinHeight = -100.f;

int gz_orgW = 4000;
int gz_orgH = 4817;

float gz_configX = 1322.f;
float gz_configY = 3424.f;
float gz_configScale = 10.41f;

// Icebreaker

PDIRECT3DTEXTURE9 ib_texture0 = NULL;
float ib_texture0_MinHeight = 0.f;

PDIRECT3DTEXTURE9 ib_texture1 = NULL;
float ib_texture1_MinHeight = 12.f;

PDIRECT3DTEXTURE9 ib_texture2 = NULL;
float ib_texture2_MinHeight = 21.f;

PDIRECT3DTEXTURE9 ib_texture3 = NULL;
float ib_texture3_MinHeight = 24.f;

PDIRECT3DTEXTURE9 ib_texture4 = NULL;
float ib_texture4_MinHeight = 27.f;

PDIRECT3DTEXTURE9 ib_texture5 = NULL;
float ib_texture5_MinHeight = 30.f;

PDIRECT3DTEXTURE9 ib_texture6 = NULL;
float ib_texture6_MinHeight = 33.f;

PDIRECT3DTEXTURE9 ib_texture7 = NULL;
float ib_texture7_MinHeight = 36.f;

PDIRECT3DTEXTURE9 ib_texture8 = NULL;
float ib_texture8_MinHeight = 39.f;

PDIRECT3DTEXTURE9 ib_texture9 = NULL;
float ib_texture9_MinHeight = 42.f;

PDIRECT3DTEXTURE9 ib_texture10 = NULL;
float ib_texture10_MinHeight = 45.f;

PDIRECT3DTEXTURE9 ib_texture11 = NULL;
float ib_texture11_MinHeight = 48.f;

int ib_orgW = 640;
int ib_orgH = 3196;

float ib_configX = 320.10f;
float ib_configY = 1580.10f;
float ib_configScale = 18.3f;

bool loadMaps(std::string mapToLoad)
{
    std::filesystem::path cwd = std::filesystem::current_path();

    if (mapToLoad == "bigmap")
    {
        // customs
        std::filesystem::path file_path = cwd / "Maps" / "Customs.png";
        std::string customsFilePathStr = file_path.string();
        const char* customsFilePathCStr = customsFilePathStr.c_str();

        bool customs_ret = LoadTextureFromFile(customsFilePathCStr, &customs_texture, &customs_mapSizeW, &customs_mapSizeH);
        IM_ASSERT(customs_ret);
    }

    if (mapToLoad == "factory4_day" || mapToLoad == "factory4_night")
    {
        //factory

        //basement
        std::filesystem::path file_pathfactorybasement = cwd / "Maps" / "Factory_basement.png";
        std::string factoryFilePathStr = file_pathfactorybasement.string();
        const char* factoryFilePathCStr = factoryFilePathStr.c_str();

        bool factory_basement = LoadTextureFromFile(factoryFilePathCStr, &factory_textureBase, &fact_base_mapSizeW, &fact_base_mapSizeH);
        IM_ASSERT(factory_basement);

        //ground floor
        std::filesystem::path file_pathfactory0f = cwd / "Maps" / "Factory_0f.png";
        std::string factoryFilePathStr0f = file_pathfactory0f.string();
        const char* factoryFilePathCStr0f = factoryFilePathStr0f.c_str();

        bool factory_0f = LoadTextureFromFile(factoryFilePathCStr0f, &factory_texture0, &fact_0f_mapSizeW, &fact_0f_mapSizeH);
        IM_ASSERT(factory_0f);
    }

    if (mapToLoad == "Interchange")
    {
        //interchange

        //ground floor
        std::filesystem::path file_pathinterchangeground = cwd / "Maps" / "Interchange_0f.png";
        std::string file_pathinterchangegroundStr = file_pathinterchangeground.string();
        const char* file_pathinterchangegroundCStr = file_pathinterchangegroundStr.c_str();

        bool interchange_ground = LoadTextureFromFile(file_pathinterchangegroundCStr, &interchange_texture0, &inter_0f_mapSizeW, &inter_0f_mapSizeH);
        IM_ASSERT(interchange_ground);

        //1st floor
        std::filesystem::path file_pathinterchange1 = cwd / "Maps" / "Interchange_1f.png";
        std::string file_pathinterchange1Str = file_pathinterchange1.string();
        const char* file_pathinterchange1CStr = file_pathinterchange1Str.c_str();

        bool interchange_1 = LoadTextureFromFile(file_pathinterchange1CStr, &interchange_texture1, &inter_1f_mapSizeW, &inter_1f_mapSizeH);
        IM_ASSERT(interchange_1);

        //2st floor
        std::filesystem::path file_pathinterchange2 = cwd / "Maps" / "Interchange_2f.png";
        std::string file_pathinterchange2Str = file_pathinterchange2.string();
        const char* file_pathinterchange2CStr = file_pathinterchange2Str.c_str();

        bool interchange_2 = LoadTextureFromFile(file_pathinterchange2CStr, &interchange_texture2, &inter_2f_mapSizeW, &inter_2f_mapSizeH);
        IM_ASSERT(interchange_2);
    }

    if (mapToLoad == "laboratory")
    {
        //labs
        //ground floor
        std::filesystem::path file_pathlabsground = cwd / "Maps" / "Labs_0f.png";
        std::string file_pathlabsgroundStr = file_pathlabsground.string();
        const char* file_pathlabsgroundStrCStr = file_pathlabsgroundStr.c_str();

        bool labs_ground = LoadTextureFromFile(file_pathlabsgroundStrCStr, &labs_texture0, &labs_0f_mapSizeW, &labs_0f_mapSizeH);
        IM_ASSERT(labs_ground);

        //1st floor
        std::filesystem::path file_pathlabs1 = cwd / "Maps" / "Labs_1f.png";
        std::string file_pathlabs1Str = file_pathlabs1.string();
        const char* file_pathlabs1CStr = file_pathlabs1Str.c_str();

        bool labs_1 = LoadTextureFromFile(file_pathlabs1CStr, &labs_texture1, &labs_1f_mapSizeW, &labs_1f_mapSizeH);
        IM_ASSERT(labs_1);

        //2st floor
        std::filesystem::path file_pathlabs2 = cwd / "Maps" / "Labs_2f.png";
        std::string file_pathlabs2Str = file_pathlabs2.string();
        const char* file_pathlabs2CStr = file_pathlabs2Str.c_str();

        bool labs_2 = LoadTextureFromFile(file_pathlabs2CStr, &labs_texture2, &labs_2f_mapSizeW, &labs_2f_mapSizeH);
        IM_ASSERT(labs_2);
    }

    if (mapToLoad == "Lighthouse")
    {
        //lighthouse
        //ground floor
        std::filesystem::path file_pathlighthouseground = cwd / "Maps" / "Lighthouse.png";
        std::string file_pathlighthousegroundStr = file_pathlighthouseground.string();
        const char* file_pathlighthousegroundCStr = file_pathlighthousegroundStr.c_str();

        bool lighthouse_ground = LoadTextureFromFile(file_pathlighthousegroundCStr, &lighthouse_texture, &lighthouse_mapSizeW, &lighthouse_mapSizeH);
        IM_ASSERT(lighthouse_ground);
    }

    if (mapToLoad == "RezervBase")
    {
        //reserve
        //underground floor
        std::filesystem::path file_pathreservebase = cwd / "Maps" / "Reserve_base.png";
        std::string file_pathreservebaseStr = file_pathreservebase.string();
        const char* file_pathreservebaseCStr = file_pathreservebaseStr.c_str();

        bool reserve_base = LoadTextureFromFile(file_pathreservebaseCStr, &reserve_texture_base, &reserve_base_mapSizeW, &reserve_base_mapSizeH);
        IM_ASSERT(reserve_base);

        //1st floor
        std::filesystem::path file_pathreserve1 = cwd / "Maps" / "Reserve_0f.png";
        std::string file_pathreserve1Str = file_pathreserve1.string();
        const char* file_pathreserve1CStr = file_pathreserve1Str.c_str();

        bool reserve_0 = LoadTextureFromFile(file_pathreserve1CStr, &reserve_texture0, &reserve_0f_mapSizeW, &reserve_0f_mapSizeH);
        IM_ASSERT(reserve_0);
    }

    if (mapToLoad == "Shoreline")
    {
        //Shoreline
        //ground floor
        std::filesystem::path file_pathshorelineground = cwd / "Maps" / "Shoreline.png";
        std::string file_pathshorelinegroundStr = file_pathshorelineground.string();
        const char* file_pathshorelinegroundCStr = file_pathshorelinegroundStr.c_str();

        bool shoreline_ground = LoadTextureFromFile(file_pathshorelinegroundCStr, &shoreline_texture0, &shoreline_0f_mapSizeW, &shoreline_0f_mapSizeH);
        IM_ASSERT(shoreline_ground);
    }

    if (mapToLoad == "TarkovStreets")
    {
        //Streets
        //ground floor
        std::filesystem::path file_pathstreets0 = cwd / "Maps" / "Streets.png";
        std::string file_pathstreets0Str = file_pathstreets0.string();
        const char* file_pathstreets0CStr = file_pathstreets0Str.c_str();

        bool streets_ground = LoadTextureFromFile(file_pathstreets0CStr, &streets_texture0, &streets_0f_mapSizeW, &streets_0f_mapSizeH);
        IM_ASSERT(streets_ground);
    }

    if (mapToLoad == "Woods")
    {
        //Woods
        //ground floor
        std::filesystem::path file_pathwoods0 = cwd / "Maps" / "Woods.png";
        std::string file_pathwoods0Str = file_pathwoods0.string();
        const char* file_pathwoods0CStr = file_pathwoods0Str.c_str();

        bool woods_ground = LoadTextureFromFile(file_pathwoods0CStr, &woods_texture0, &woods_0f_mapSizeW, &woods_0f_mapSizeH);
        IM_ASSERT(woods_ground);
    }


    if (mapToLoad == "Sandbox" || mapToLoad == "Sandbox_high")
    {
        //groundzero
        //ground floor
        std::filesystem::path file_pathgz0 = cwd / "Maps" / "GroundZero.png";
        std::string file_pathgz0Str = file_pathgz0.string();
        const char* file_pathgz0CStr = file_pathgz0Str.c_str();

        bool gz_ground = LoadTextureFromFile(file_pathgz0CStr, &gz_texture0, &gz_0f_mapSizeW, &gz_0f_mapSizeH);
        IM_ASSERT(gz_ground);
    }

    if (mapToLoad == "Icebreaker")
    {
        // Icebreaker

// Ground floor / 1F
        std::filesystem::path file_pathib0 = cwd / "Maps" / "ib_01.png";
        std::string file_pathib0Str = file_pathib0.string();
        const char* file_pathib0CStr = file_pathib0Str.c_str();

        bool ib_ground = LoadTextureFromFile(
            file_pathib0CStr,
            &ib_texture0,
            &ib_1f_mapSizeW,
            &ib_1f_mapSizeH
        );
        IM_ASSERT(ib_ground);


        // 2F
        std::filesystem::path file_pathib1 = cwd / "Maps" / "ib_02.png";
        std::string file_pathib1Str = file_pathib1.string();
        const char* file_pathib1CStr = file_pathib1Str.c_str();

        bool ib_2f = LoadTextureFromFile(
            file_pathib1CStr,
            &ib_texture1,
            &ib_2f_mapSizeW,
            &ib_2f_mapSizeH
        );
        IM_ASSERT(ib_2f);


        // 3F
        std::filesystem::path file_pathib2 = cwd / "Maps" / "ib_03.png";
        std::string file_pathib2Str = file_pathib2.string();
        const char* file_pathib2CStr = file_pathib2Str.c_str();

        bool ib_3f = LoadTextureFromFile(
            file_pathib2CStr,
            &ib_texture2,
            &ib_3f_mapSizeW,
            &ib_3f_mapSizeH
        );
        IM_ASSERT(ib_3f);


        // 4F
        std::filesystem::path file_pathib3 = cwd / "Maps" / "ib_04.png";
        std::string file_pathib3Str = file_pathib3.string();
        const char* file_pathib3CStr = file_pathib3Str.c_str();

        bool ib_4f = LoadTextureFromFile(
            file_pathib3CStr,
            &ib_texture3,
            &ib_4f_mapSizeW,
            &ib_4f_mapSizeH
        );
        IM_ASSERT(ib_4f);


        // 5F
        std::filesystem::path file_pathib4 = cwd / "Maps" / "ib_05.png";
        std::string file_pathib4Str = file_pathib4.string();
        const char* file_pathib4CStr = file_pathib4Str.c_str();

        bool ib_5f = LoadTextureFromFile(
            file_pathib4CStr,
            &ib_texture4,
            &ib_5f_mapSizeW,
            &ib_5f_mapSizeH
        );
        IM_ASSERT(ib_5f);


        // 6F
        std::filesystem::path file_pathib5 = cwd / "Maps" / "ib_06.png";
        std::string file_pathib5Str = file_pathib5.string();
        const char* file_pathib5CStr = file_pathib5Str.c_str();

        bool ib_6f = LoadTextureFromFile(
            file_pathib5CStr,
            &ib_texture5,
            &ib_6f_mapSizeW,
            &ib_6f_mapSizeH
        );
        IM_ASSERT(ib_6f);


        // 7F
        std::filesystem::path file_pathib6 = cwd / "Maps" / "ib_07.png";
        std::string file_pathib6Str = file_pathib6.string();
        const char* file_pathib6CStr = file_pathib6Str.c_str();

        bool ib_7f = LoadTextureFromFile(
            file_pathib6CStr,
            &ib_texture6,
            &ib_7f_mapSizeW,
            &ib_7f_mapSizeH
        );
        IM_ASSERT(ib_7f);


        // 8F
        std::filesystem::path file_pathib7 = cwd / "Maps" / "ib_08.png";
        std::string file_pathib7Str = file_pathib7.string();
        const char* file_pathib7CStr = file_pathib7Str.c_str();

        bool ib_8f = LoadTextureFromFile(
            file_pathib7CStr,
            &ib_texture7,
            &ib_8f_mapSizeW,
            &ib_8f_mapSizeH
        );
        IM_ASSERT(ib_8f);


        // 9F
        std::filesystem::path file_pathib8 = cwd / "Maps" / "ib_09.png";
        std::string file_pathib8Str = file_pathib8.string();
        const char* file_pathib8CStr = file_pathib8Str.c_str();

        bool ib_9f = LoadTextureFromFile(
            file_pathib8CStr,
            &ib_texture8,
            &ib_9f_mapSizeW,
            &ib_9f_mapSizeH
        );
        IM_ASSERT(ib_9f);


        // 10F
        std::filesystem::path file_pathib9 = cwd / "Maps" / "ib_10.png";
        std::string file_pathib9Str = file_pathib9.string();
        const char* file_pathib9CStr = file_pathib9Str.c_str();

        bool ib_10f = LoadTextureFromFile(
            file_pathib9CStr,
            &ib_texture9,
            &ib_10f_mapSizeW,
            &ib_10f_mapSizeH
        );
        IM_ASSERT(ib_10f);


        // 11F
        std::filesystem::path file_pathib10 = cwd / "Maps" / "ib_11.png";
        std::string file_pathib10Str = file_pathib10.string();
        const char* file_pathib10CStr = file_pathib10Str.c_str();

        bool ib_11f = LoadTextureFromFile(
            file_pathib10CStr,
            &ib_texture10,
            &ib_11f_mapSizeW,
            &ib_11f_mapSizeH
        );
        IM_ASSERT(ib_11f);
    }

    return true;
}

MapControl::MapControl() : panOffset(0, 0), zoomLevel(0.20f), isDragging(false), orginal_image_size((float)2255, (float)1916), config_size(2255, 1916), scale(3.731f) {}

void MapControl::Update(ImVec2 loadedImageSize) {
    if (ImGui::IsMouseReleased(0)) {
        isDragging = false;
    }

    const bool hasFocusPoint =
        mapGlobals::focusPoint.x != 0.f ||
        mapGlobals::focusPoint.y != 0.f ||
        mapGlobals::focusPoint.z != 0.f;

    if (ImGui::IsMouseDragging(0) && ImGui::IsWindowHovered() && !isDragging)
    {
        if (mapGlobals::followLocal || hasFocusPoint)
        {
            mapGlobals::followLocal = false;
            mapGlobals::focusPoint = { 0.f, 0.f, 0.f };
        }

        isDragging = true;
        lastMousePos = ImGui::GetMousePos();
        panImageStartPosition = imagePos;
    }

    if (isDragging) {
        ImVec2 mouseDelta = {
            ImGui::GetMousePos().x - lastMousePos.x,
            ImGui::GetMousePos().y - lastMousePos.y
        };

        imagePos.x = panImageStartPosition.x + mouseDelta.x;
        imagePos.y = panImageStartPosition.y + mouseDelta.y;
    }

    // Zoom in/out using mouse wheel
    if (ImGui::IsWindowHovered()) {
        ImVec2 cursorPosImageSpaceBeforeZoom = {
            (ImGui::GetMousePos().x - imagePos.x) / zoomLevel,
            (ImGui::GetMousePos().y - imagePos.y) / zoomLevel
        };

        float zoomDelta = ImGui::GetIO().MouseWheel;
        float oldZoomLevel = zoomLevel;
        zoomLevel = ImClamp(zoomLevel + zoomDelta * 0.05f, 0.1f, 2.9f);

        if (zoomDelta > 0.f || zoomDelta < 0.f) {
            imagePos.x -= cursorPosImageSpaceBeforeZoom.x * (zoomLevel - oldZoomLevel);
            imagePos.y -= cursorPosImageSpaceBeforeZoom.y * (zoomLevel - oldZoomLevel);
        }
    }

    // Update the image size based on the zoom level
    imageSize = {
        loadedImageSize.x * zoomLevel,
        loadedImageSize.y * zoomLevel
    };

    // Limit panning based on bounds
    imagePos.x = ImClamp(imagePos.x, ImGui::GetWindowContentRegionMin().x - imageSize.x, ImGui::GetWindowContentRegionMax().x);
    imagePos.y = ImClamp(imagePos.y, ImGui::GetWindowContentRegionMin().y - imageSize.y, ImGui::GetWindowContentRegionMax().y);
}

glm::vec3 MapControl::getMapPosition(glm::vec3 worldPosition, float configX, float configY, float configScale) {
    glm::vec3 position = {
        configX + (worldPosition.x * configScale), // X
        configY - (worldPosition.z * configScale), // Y
        worldPosition.y // Z (Height no conversion as we need raw)
    };

    glm::vec3 zoomedPos = {
        (position.x * zoomLevel) + imagePos.x,
        (position.y * zoomLevel) + imagePos.y,
        worldPosition.y
    };

    return zoomedPos;
}

void MapControl::RenderImage(ImTextureID imageTexture, glm::vec3 centerPoint, bool followLocal)
{
    if (followLocal)
    {
        // Set to 0 or it causes ghost on follow
        imagePos = { 0,0 };

        // Get local map position
        glm::vec3 localpointMap = getMapPosition(mainGame.localLocation, currentMap::configX, currentMap::configY, currentMap::configScale);

        // Get window size
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 windowSizeCenter = { ImGui::GetWindowSize().x / 2, ImGui::GetWindowSize().y / 2 };

        // Work out center point to center of window
        imagePos = {
            (windowSizeCenter.x - localpointMap.x) + viewport->WorkPos.x,
            (windowSizeCenter.y - localpointMap.y) + viewport->WorkPos.y
        };
    }

    if (centerPoint.x != 0.f || centerPoint.y != 0.f || centerPoint.z != 0.f)
    {
        // Set to 0 or it causes ghost on follow
        imagePos = { 0,0 };

        // Get local map position
        glm::vec3 localpointMap = getMapPosition(centerPoint, currentMap::configX, currentMap::configY, currentMap::configScale);

        // Get window size
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 windowSizeCenter = { ImGui::GetWindowSize().x / 2, ImGui::GetWindowSize().y / 2 };

        // Work out center point to center of window
        imagePos = {
            (windowSizeCenter.x - localpointMap.x) + viewport->WorkPos.x,
            (windowSizeCenter.y - localpointMap.y) + viewport->WorkPos.y
        };
    }

    ImGui::SetCursorScreenPos(imagePos);
    ImGui::Image(imageTexture, imageSize);
}

MapControl mapControl;