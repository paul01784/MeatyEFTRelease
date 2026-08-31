#include "MapWidget.h"

#include "../includes.h"
#include "../debug.h"
#include "../globals.h"
#include "../maps.h"
#include "../../Tarkov/GameWorld/MainGame.h"

#include <chrono>

namespace uiWidgets
{
void ensureSelectedMapLoaded()
{
    static std::string loadedMapId;
    static std::string failedMapId;
    static std::chrono::steady_clock::time_point lastFailureAt{};
    static constexpr std::chrono::seconds retryDelay{ 5 };

    if (!appGlobals::runRadar.load(std::memory_order_acquire))
    {
        loadedMapId.clear();
        failedMapId.clear();
        return;
    }

    const std::string selectedMap = mainGame.selectedLocation;

    if (selectedMap.empty() || selectedMap == loadedMapId)
        return;

    // Bound retry attempts so a broken map cannot hammer disk/driver every frame.
    if (selectedMap == failedMapId &&
        std::chrono::steady_clock::now() - lastFailureAt < retryDelay)
        return;

    setCurrentMapSpecs = false;

    if (!loadMaps(selectedMap))
    {
        failedMapId = selectedMap;
        lastFailureAt = std::chrono::steady_clock::now();
        LOGS.logError("[MAP] Failed to load map textures for: " + selectedMap);
        return;
    }

    loadedMapId = selectedMap;
    failedMapId.clear();

    LOGS.logInfo("[MAP] Loaded map textures for: " + selectedMap);
}

void renderMapDetails()
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
    else if (mainGame.selectedLocation == "Labyrinth")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = labyrinth_configX;
            currentMap::configY = labyrinth_configY;
            currentMap::configScale = labyrinth_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = labyrinth_orgW;
        map_orgH = labyrinth_orgH;
        texture = labyrinth_texture;
    }
    else if (mainGame.selectedLocation == "Terminal")
    {
        if (!setCurrentMapSpecs)
        {
            currentMap::configX = terminal_configX;
            currentMap::configY = terminal_configY;
            currentMap::configScale = terminal_configScale;
            setCurrentMapSpecs = true;
        }

        map_orgW = terminal_orgW;
        map_orgH = terminal_orgH;
        texture = terminal_texture;
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
}
