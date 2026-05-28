#pragma once
#include "../app/render.h"

// forwar-declare functions
bool loadMaps(std::string mapToLoad);

class MapControl {
public:
    MapControl();

    void Update(ImVec2 loadedImageSize);

    glm::vec3 getMapPosition(glm::vec3 worldPosition, float configX, float configY, float configScale);

    void RenderImage(ImTextureID imageTexture, glm::vec3 centerPoint, bool followLocal);

public:
    float zoomLevel;
    ImVec2 orginal_image_size;
    ImVec2 config_size;
    float scale;
    ImVec2 imagePos;
    ImVec2 panOffset;
    ImVec2 lastMousePos;
    ImVec2 initialImageSize;
    ImVec2 imageSize;
    bool isDragging;
    ImVec2 panImageStartPosition;
    ImVec2 cursorPosImageSpace;
    ImVec2 minBounds_;
    ImVec2 maxBounds_;
};

// Define object to MapControl storage
extern MapControl mapControl;

struct currentMap {
    static float configX;
    static float configY;
    static float configScale;
}extern;

// Map details
extern int customs_mapSizeW;
extern int customs_mapSizeH;

extern int fact_base_mapSizeW;
extern int fact_base_mapSizeH;
extern int fact_0f_mapSizeW;
extern int fact_0f_mapSizeH;

extern int inter_0f_mapSizeW;
extern int inter_0f_mapSizeH;
extern int inter_1f_mapSizeW;
extern int inter_1f_mapSizeH;
extern int inter_2f_mapSizeW;
extern int inter_2f_mapSizeH;

extern int labs_0f_mapSizeW;
extern int labs_0f_mapSizeH;
extern int labs_1f_mapSizeW;
extern int labs_1f_mapSizeH;
extern int labs_2f_mapSizeW;
extern int labs_2f_mapSizeH;

extern int lighthouse_mapSizeW;
extern int lighthouse_mapSizeH;

extern int reserve_base_mapSizeW;
extern int reserve_base_mapSizeH;
extern int reserve_0f_mapSizeW;
extern int reserve_0f_mapSizeH;

extern int shoreline_0f_mapSizeW;
extern int shoreline_0f_mapSizeH;

extern int streets_0f_mapSizeW;
extern int streets_0f_mapSizeH;

extern int woods_0f_mapSizeW;
extern int woods_0f_mapSizeH;

extern int gz_0f_mapSizeW;
extern int gz_0f_mapSizeH;

extern int ib_1f_mapSizeW;
extern int ib_1f_mapSizeH;
extern int ib_2f_mapSizeW;
extern int ib_2f_mapSizeH;
extern int ib_3f_mapSizeW;
extern int ib_3f_mapSizeH;
extern int ib_4f_mapSizeW;
extern int ib_4f_mapSizeH;
extern int ib_5f_mapSizeW;
extern int ib_5f_mapSizeH;
extern int ib_6f_mapSizeW;
extern int ib_6f_mapSizeH;
extern int ib_7f_mapSizeW;
extern int ib_7f_mapSizeH;
extern int ib_8f_mapSizeW;
extern int ib_8f_mapSizeH;
extern int ib_9f_mapSizeW;
extern int ib_9f_mapSizeH;
extern int ib_10f_mapSizeW;
extern int ib_10f_mapSizeH;
extern int ib_11f_mapSizeW;
extern int ib_11f_mapSizeH;

// map settings

// Customs
extern PDIRECT3DTEXTURE9 customs_texture;
extern float customs_texture0_MinHeight;
extern int customs_orgW;
extern int customs_orgH;

extern float customs_configX;
extern float customs_configY;
extern float customs_configScale;


// Factory
extern PDIRECT3DTEXTURE9 factory_textureBase;
extern float factory_textureBase_MinHeight;

extern PDIRECT3DTEXTURE9 factory_texture0;
extern float factory_texture0_MinHeight;

extern int factory_orgW;
extern int factory_orgH;

extern float factory_configX;
extern float factory_configY;
extern float factory_configScale;

// Interchange
extern PDIRECT3DTEXTURE9 interchange_texture0;
extern float interchange_texture0_MinHeight;

extern PDIRECT3DTEXTURE9 interchange_texture1;
extern float interchange_texture1_MinHeight;

extern PDIRECT3DTEXTURE9 interchange_texture2;
extern float interchange_texture2_MinHeight;

extern int interchange_orgW;
extern int interchange_orgH;

extern float interchange_configX;
extern float interchange_configY;
extern float interchange_configScale;

// Labs
extern PDIRECT3DTEXTURE9 labs_texture0;
extern float labs_texture0_MinHeight;

extern PDIRECT3DTEXTURE9 labs_texture1;
extern float labs_texture1_MinHeight;

extern PDIRECT3DTEXTURE9 labs_texture2;
extern float labs_texture2_MinHeight;

extern int labs_orgW;
extern int labs_orgH;

extern float labs_configX;
extern float labs_configY;
extern float labs_configScale;

// Lighthouse
extern PDIRECT3DTEXTURE9 lighthouse_texture;
extern float lighthouse_texture0_MinHeight;

extern int lighthouse_orgW;
extern int lighthouse_orgH;

extern float lighthouse_configX;
extern float lighthouse_configY;
extern float lighthouse_configScale;

// Reserve
extern PDIRECT3DTEXTURE9 reserve_texture_base;
extern float reserve_texture_base_MinHeight;

extern PDIRECT3DTEXTURE9 reserve_texture0;
extern float reserve_texture0_MinHeight;


extern int reserve_orgW;
extern int reserve_orgH;

extern float reserve_configX;
extern float reserve_configY;
extern float reserve_configScale;

// Shoreline

extern PDIRECT3DTEXTURE9 shoreline_texture0;
extern float shoreline_texture0_MinHeight;


extern int shoreline_orgW;
extern int shoreline_orgH;

extern float shoreline_configX;
extern float shoreline_configY;
extern float shoreline_configScale;

// Streets

extern PDIRECT3DTEXTURE9 streets_texture0;
extern float streets_texture0_MinHeight;


extern int streets_orgW;
extern int streets_orgH;

extern float streets_configX;
extern float streets_configY;
extern float streets_configScale;

// Woods

extern PDIRECT3DTEXTURE9 woods_texture0;
extern float woods_texture0_MinHeight;


extern int woods_orgW;
extern int woods_orgH;

extern float woods_configX;
extern float woods_configY;
extern float woods_configScale;


// GroundZero

extern PDIRECT3DTEXTURE9 gz_texture0;
extern float gz_texture0_MinHeight;
extern int gz_orgW;
extern int gz_orgH;

extern float gz_configX;
extern float gz_configY;
extern float gz_configScale;

// Icebreaker

extern PDIRECT3DTEXTURE9 ib_texture0;
extern float ib_texture0_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture1;
extern float ib_texture1_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture2;
extern float ib_texture2_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture3;
extern float ib_texture3_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture4;
extern float ib_texture4_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture5;
extern float ib_texture5_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture6;
extern float ib_texture6_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture7;
extern float ib_texture7_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture8;
extern float ib_texture8_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture9;
extern float ib_texture9_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture10;
extern float ib_texture10_MinHeight;
extern PDIRECT3DTEXTURE9 ib_texture11;
extern float ib_texture11_MinHeight;
extern int ib_orgW;
extern int ib_orgH;

extern float ib_configX;
extern float ib_configY;
extern float ib_configScale;