#pragma once

#include "DxRenderWindow.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <atomic>
#include <sstream>
#include <iostream>

#include "game/headers/maingame.h"
#include "game/headers/utils.h"
#include "game/headers/players.h"
#include "game/headers/loot.h"
#include "game/headers/explosives.h"
#include "game/headers/questManager.h"
#include "game/headers/camera.h"
#include "game/headers/fireport.h"
#include "game/headers/readOnlyAim.h"
#include "aimLineTargeting.h"
#include "../app/makcu.h"
#include "../app/globals.h"
#include "../game/headers/exfil.h"

namespace MapNamesRender
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

namespace fuserRender
{
    inline thread_local CameraProjectionSnapshot g_frameCameraSnapshot;
    inline thread_local PlayerCacheSnapshot g_framePlayerSnapshot;
    inline thread_local LootCacheSnapshot g_frameLootSnapshot;
    inline thread_local GrenadeCacheSnapshot g_frameGrenadeSnapshot;
    inline thread_local QuestPublishedSnapshot g_frameQuestSnapshot;
    inline thread_local ExfilCacheSnapshot g_frameExfilSnapshot;
    inline thread_local FireportPoseSnapshot g_frameFireportSnapshot;
    inline thread_local glm::vec3 g_frameLocalLocation{};

    static inline bool ProjectWorldToScreen(
        const glm::vec3& world,
        glm::vec2* screen)
    {
        return g_frameCameraSnapshot &&
            Utils::Camera::world_to_screen(
                world,
                screen,
                *g_frameCameraSnapshot);
    }

    static inline void CaptureFrameSnapshots()
    {
        g_frameCameraSnapshot = camera.getProjectionSnapshot();
        g_framePlayerSnapshot = players.getCacheSnapshot();
        g_frameLootSnapshot = Loot.getCacheSnapshot();
        g_frameGrenadeSnapshot = explosiveManager.getGrenadesSnapshot();
        g_frameQuestSnapshot = GetQuestPublishedSnapshot();
        g_frameExfilSnapshot = exfil.getCacheExfilSnapshot();
        g_frameFireportSnapshot = g_fireport.getSnapshot();
        g_frameLocalLocation = mainGame.localLocation;

        for (const PlayerCache& player : *g_framePlayerSnapshot)
        {
            if (player.isLocal)
            {
                g_frameLocalLocation = player.location;
                break;
            }
        }
    }

    static inline void ReleaseFrameSnapshots()
    {
        g_frameCameraSnapshot.reset();
        g_framePlayerSnapshot.reset();
        g_frameLootSnapshot.reset();
        g_frameGrenadeSnapshot.reset();
        g_frameQuestSnapshot.reset();
        g_frameExfilSnapshot.reset();
        g_frameFireportSnapshot.reset();
    }

    //exception tracking

    inline std::atomic<const char*> g_currentStage = "Idle";

    static inline const char* GetCurrentStage()
    {
        const char* stage = g_currentStage.load(std::memory_order_relaxed);
        return stage ? stage : "Unknown";
    }

    class RenderStageScope
    {
    public:
        explicit RenderStageScope(const char* stage)
        {
            m_previousStage = g_currentStage.load(std::memory_order_relaxed);
            g_currentStage.store(stage, std::memory_order_relaxed);
        }

        ~RenderStageScope()
        {
            g_currentStage.store(m_previousStage, std::memory_order_relaxed);
        }

    private:
        const char* m_previousStage = "Idle";
    };

    static inline void LogStageError(const std::string& message)
    {
        LOGS.logError("[FUSER][RENDER] " + message);
    }

    template <typename Fn>
    static inline bool SafeRenderStage(const char* stageName, Fn&& fn)
    {
        RenderStageScope scope(stageName);

        try
        {
            fn();
            return true;
        }
        catch (const std::exception& e)
        {
            LogStageError(
                std::string("Stage failed: ") +
                stageName +
                " Exception: " +
                e.what()
            );

            return false;
        }
        catch (...)
        {
            LogStageError(
                std::string("Stage failed: ") +
                stageName +
                " Unknown exception"
            );

            return false;
        }
    }

    // end exception tracking

    inline std::atomic_bool g_testSceneEnabled = false;

    static inline bool IsTestSceneEnabled()
    {
        return g_testSceneEnabled.load(std::memory_order_relaxed);
    }

    static inline void SetTestSceneEnabled(bool enabled)
    {
        g_testSceneEnabled.store(enabled, std::memory_order_relaxed);
    }

    static inline float GetFrameFPS()
    {
        using namespace std::chrono;

        static steady_clock::time_point lastTime = steady_clock::now();
        static float fps = 0.0f;
        static int frameCount = 0;
        static float accumulator = 0.0f;

        const steady_clock::time_point now = steady_clock::now();
        const float delta = duration<float>(now - lastTime).count();
        lastTime = now;

        accumulator += delta;
        frameCount++;

        if (accumulator >= 0.50f)
        {
            fps = static_cast<float>(frameCount) / accumulator;
            frameCount = 0;
            accumulator = 0.0f;
        }

        return fps;
    }

    static inline float NowSeconds()
    {
        using namespace std::chrono;

        static const steady_clock::time_point startTime = steady_clock::now();
        const steady_clock::time_point now = steady_clock::now();

        return duration<float>(now - startTime).count();
    }

    static inline float Wave01(float value)
    {
        return (std::sin(value) * 0.5f) + 0.5f;
    }

    static inline glm::vec4 WithAlpha(const glm::vec4& colour, float alpha)
    {
        return glm::vec4(colour.r, colour.g, colour.b, alpha);
    }

    static inline void RenderMovingBox(float time, float screenW, float screenH)
    {
        const glm::vec4 outlineColour = glm::vec4(0.15f, 0.65f, 1.0f, 1.0f);
        const glm::vec4 fillColour = glm::vec4(0.15f, 0.65f, 1.0f, 0.18f);
        const glm::vec4 textColour = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        const float boxW = 180.0f;
        const float boxH = 70.0f;

        const float rangeX = std::max(1.0f, screenW - boxW - 80.0f);

        const float x = 40.0f + (Wave01(time * 1.2f) * rangeX);
        const float y = 70.0f;

        g_DxWindow.DrawBox(
            x,
            y,
            boxW,
            boxH,
            outlineColour,
            2.0f,
            fillColour,
            true
        );

        g_DxWindow.DrawString(
            "Moving Box",
            x + (boxW * 0.5f),
            y + boxH + 8.0f,
            15.0f,
            textColour,
            true,
            true
        );
    }

    static inline void RenderOrbitCircle(float time, float screenW, float screenH)
    {
        const glm::vec4 lineColour = glm::vec4(0.45f, 0.45f, 0.45f, 0.65f);
        const glm::vec4 circleFill = glm::vec4(1.0f, 0.35f, 0.15f, 0.35f);
        const glm::vec4 circleOutline = glm::vec4(1.0f, 0.35f, 0.15f, 1.0f);
        const glm::vec4 textColour = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        const float centerX = screenW * 0.5f;
        const float centerY = screenH * 0.5f;

        const float orbitX = std::min(220.0f, screenW * 0.25f);
        const float orbitY = std::min(130.0f, screenH * 0.22f);

        const float x = centerX + (std::cos(time * 1.4f) * orbitX);
        const float y = centerY + (std::sin(time * 1.1f) * orbitY);

        g_DxWindow.DrawLine(
            centerX,
            centerY,
            x,
            y,
            lineColour,
            1.0f
        );

        g_DxWindow.DrawFilledCircle(
            x,
            y,
            22.0f,
            circleFill
        );

        g_DxWindow.DrawCircle(
            x,
            y,
            22.0f,
            circleOutline,
            2.0f
        );

        g_DxWindow.DrawString(
            "Orbit",
            x,
            y + 28.0f,
            14.0f,
            textColour,
            true,
            true
        );
    }

    static inline void RenderRotatingLine(float time, float screenW, float screenH)
    {
        const glm::vec4 lineColour = glm::vec4(0.25f, 1.0f, 0.35f, 1.0f);
        const glm::vec4 centerColour = glm::vec4(0.25f, 1.0f, 0.35f, 0.35f);

        const float centerX = screenW * 0.5f;
        const float centerY = screenH * 0.5f;

        const float length = std::min(screenW, screenH) * 0.18f;

        const float angle = time * 2.0f;

        const float x2 = centerX + std::cos(angle) * length;
        const float y2 = centerY + std::sin(angle) * length;

        g_DxWindow.DrawFilledCircle(
            centerX,
            centerY,
            5.0f,
            centerColour
        );

        g_DxWindow.DrawLine(
            centerX,
            centerY,
            x2,
            y2,
            lineColour,
            3.0f
        );
    }

    static inline void RenderMarkers(float time, float screenW, float screenH)
    {
        const glm::vec4 markerOutline = glm::vec4(1.0f, 0.85f, 0.1f, 1.0f);
        const glm::vec4 markerFill = glm::vec4(1.0f, 0.85f, 0.1f, 0.25f);
        const glm::vec4 textColour = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

        const float pulse = 6.0f + (Wave01(time * 4.0f) * 5.0f);

        g_DxWindow.DrawMarkerWithText(
            screenW * 0.25f,
            screenH * 0.72f,
            pulse,
            "Marker A",
            markerOutline,
            markerFill,
            textColour,
            14.0f,
            6.0f,
            1.0f
        );

        g_DxWindow.DrawMarkerWithText(
            screenW * 0.50f,
            screenH * 0.78f,
            8.0f,
            "Marker B",
            markerOutline,
            markerFill,
            textColour,
            14.0f,
            6.0f,
            1.0f
        );

        g_DxWindow.DrawMarkerWithText(
            screenW * 0.75f,
            screenH * 0.72f,
            pulse,
            "Marker C",
            markerOutline,
            markerFill,
            textColour,
            14.0f,
            6.0f,
            1.0f
        );
    }

    static inline void RenderCornerBoxes(float screenW, float screenH)
    {
        const glm::vec4 colour = glm::vec4(0.8f, 0.2f, 1.0f, 1.0f);
        const glm::vec4 fill = glm::vec4(0.8f, 0.2f, 1.0f, 0.10f);

        g_DxWindow.DrawBox(
            20.0f,
            20.0f,
            120.0f,
            45.0f,
            colour,
            1.0f,
            fill,
            true
        );

        g_DxWindow.DrawBox(
            screenW - 140.0f,
            20.0f,
            120.0f,
            45.0f,
            colour,
            1.0f,
            fill,
            true
        );

        g_DxWindow.DrawBox(
            20.0f,
            screenH - 65.0f,
            120.0f,
            45.0f,
            colour,
            1.0f,
            fill,
            true
        );

        g_DxWindow.DrawBox(
            screenW - 140.0f,
            screenH - 65.0f,
            120.0f,
            45.0f,
            colour,
            1.0f,
            fill,
            true
        );
    }

    static inline void RenderHeaderText(float time, float fps)
    {
        const glm::vec4 titleColour = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        const glm::vec4 subColour = glm::vec4(0.65f, 0.85f, 1.0f, 1.0f);
        const glm::vec4 fpsColour = glm::vec4(0.25f, 1.0f, 0.35f, 1.0f);

        g_DxWindow.DrawString(
            "Fuser Render Test",
            20.0f,
            20.0f,
            22.0f,
            titleColour,
            false,
            true
        );

        g_DxWindow.DrawString(
            "DX window draw queue is active",
            20.0f,
            48.0f,
            14.0f,
            subColour,
            false,
            true
        );

        g_DxWindow.DrawString(
            "Time: " + std::to_string(static_cast<int>(time)) + "s",
            20.0f,
            70.0f,
            13.0f,
            subColour,
            false,
            true
        );

        std::ostringstream fpsStream;
        fpsStream.precision(0);
        fpsStream << std::fixed << fps;

        g_DxWindow.DrawString(
            "FPS: " + fpsStream.str(),
            20.0f,
            90.0f,
            13.0f,
            fpsColour,
            false,
            true
        );
    }

    static inline void RenderTestScene()
    {
        const float time = NowSeconds();
        const float fps = GetFrameFPS();

        float screenW = static_cast<float>(g_DxWindow.GetWindowWidth());
        float screenH = static_cast<float>(g_DxWindow.GetWindowHeight());

        if (screenW <= 0.0f)
            screenW = 1280.0f;

        if (screenH <= 0.0f)
            screenH = 720.0f;

        RenderHeaderText(time, fps);
        RenderCornerBoxes(screenW, screenH);
        RenderMovingBox(time, screenW, screenH);
        RenderRotatingLine(time, screenW, screenH);
        RenderOrbitCircle(time, screenW, screenH);
        RenderMarkers(time, screenW, screenH);
    }

    // Client main render functions

    static inline std::string CleanText(std::string text)
    {
        text.erase(
            std::remove(text.begin(), text.end(), '\0'),
            text.end()
        );

        return text;
    }

    static inline float ScreenWidth()
    {
        const int w = g_DxWindow.GetWindowWidth();
        return w > 0 ? static_cast<float>(w) : 1280.0f;
    }

    static inline float ScreenHeight()
    {
        const int h = g_DxWindow.GetWindowHeight();
        return h > 0 ? static_cast<float>(h) : 720.0f;
    }

    static inline void DrawCornerBox(
        float x,
        float y,
        float w,
        float h,
        const glm::vec4& colour,
        float thickness = 1.0f)
    {
        const float lineW = w * 0.25f;
        const float lineH = h * 0.25f;

        // Top left
        g_DxWindow.DrawLine(x, y, x + lineW, y, colour, thickness);
        g_DxWindow.DrawLine(x, y, x, y + lineH, colour, thickness);

        // Top right
        g_DxWindow.DrawLine(x + w, y, x + w - lineW, y, colour, thickness);
        g_DxWindow.DrawLine(x + w, y, x + w, y + lineH, colour, thickness);

        // Bottom left
        g_DxWindow.DrawLine(x, y + h, x + lineW, y + h, colour, thickness);
        g_DxWindow.DrawLine(x, y + h, x, y + h - lineH, colour, thickness);

        // Bottom right
        g_DxWindow.DrawLine(x + w, y + h, x + w - lineW, y + h, colour, thickness);
        g_DxWindow.DrawLine(x + w, y + h, x + w, y + h - lineH, colour, thickness);
    }

    static inline void RenderHUD()
    {
        const float screenW = ScreenWidth();

        const float fps = GetFrameFPS();

        g_DxWindow.DrawString(
            "MeatyEFT",
            screenW - 100.0f,
            20.0f,
            14.0f,
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            false,
            true
        );

        std::string fpsText = "FPS: " + std::to_string(static_cast<int>(fps));


        g_DxWindow.DrawString(
            fpsText,
            screenW - 100.0f,
            40.0f,
            14.0f,
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            false,
            true
        );
    }

    static inline void RenderCrosshair()
    {
        if (!g_frameCameraSnapshot ||
            !g_frameCameraSnapshot->valid)
            return;

        if (!espGlobals::drawCrosshair)
            return;

        if (g_frameCameraSnapshot && g_frameCameraSnapshot->scoped)
            return;

        const float screenW = ScreenWidth();
        const float screenH = ScreenHeight();

        g_DxWindow.DrawCircle(
            screenW * 0.5f,
            screenH * 0.5f,
            2.0f,
            coloursGlobals::crosshair,
            1.0f
        );
    }

    static inline void RenderFireportVisual()
    {
        if (!g_frameCameraSnapshot ||
            !g_frameCameraSnapshot->valid ||
            !makcu.IsConnected() ||
            !aimGlobals::aimEnabled ||
            !aimGlobals::drawFireportLine)
            return;


        const FireportPose& pose = *g_frameFireportSnapshot;
        if (!pose.valid)
            return;

        glm::vec2 screenStart{};

        if (!ProjectWorldToScreen(pose.worldOrigin, &screenStart))
            return;

        glm::vec2 screenEnd{
            ScreenWidth() * 0.5f,
            ScreenHeight() * 0.5f
        };

        if (aimGlobals::aimReference == AimReference::Fireport)
        {
            if (pose.aimRefOk)
                screenEnd = pose.screenEnd;
        }

        static const glm::vec4 kFireportLine{1.0f, 0.86f, 0.24f, 0.95f};
        g_DxWindow.DrawLine(
            screenStart.x,
            screenStart.y,
            screenEnd.x,
            screenEnd.y,
            kFireportLine,
            2.0f
        );
    }

    static inline void RenderAimFovRing()
    {
        if (!aimGlobals::showAimFovRing ||
            !aimGlobals::aimEnabled ||
            !makcu.IsConnected())
            return;
        if (aimGlobals::aimFOV <= 0.f)
            return;

        glm::vec2 ringCenter{
            ScreenWidth() * 0.5f,
            ScreenHeight() * 0.5f
        };

        if (aimGlobals::aimReference == AimReference::Fireport)
            ringCenter = readOnlyAim.resolveAimReference().pos;

        g_DxWindow.DrawCircle(
            ringCenter.x,
            ringCenter.y,
            aimGlobals::aimFOV,
            coloursGlobals::fovCircle,
            1.5f
        );
    }

    static inline void RenderTasks()
    {
        if (!espGlobals::drawQuestHelper)
            return;

        const std::string currentMapId = TrimEFT(mainGame.selectedLocation);

        

        const std::vector<QuestLocation>& locations = g_frameQuestSnapshot->masterLocations;

        for (const auto& loc : locations)
        {
            // Convert id to name
            std::string mapName(MapNamesRender::GetNameFromId(loc.mapNameId));

            if (!(Utils::Text::containsIgnoreCase(mapName, currentMapId) ||
                Utils::Text::containsIgnoreCase(currentMapId, mapName)))
            {
                continue;
            }

            const int distance = static_cast<int>(glm::distance(g_frameLocalLocation, loc.pos));
            if (distance > espGlobals::drawLootDist)
                continue;

            if (loc.objectiveType != "visit" &&
                loc.objectiveType != "plantItem" &&
                loc.objectiveType != "plantQuestItem" &&
                loc.objectiveType != "mark")
            {
                continue;
            }

            glm::vec2 screenPos{};
            if (!ProjectWorldToScreen(loc.pos, &screenPos))
                continue;

            std::string questText = loc.questName;

            if (loc.objectiveType == "plantItem" ||
                loc.objectiveType == "plantQuestItem")
                questText += " (PLANT)";
            else if (loc.objectiveType == "mark")
                questText += " (MARK)";

            g_DxWindow.DrawFilledCircle(
                screenPos.x,
                screenPos.y,
                2.0f,
                coloursGlobals::questMarker
            );

            g_DxWindow.DrawString(
                questText,
                screenPos.x + 6.0f,
                screenPos.y - 3.0f,
                13.0f,
                coloursGlobals::questMarker,
                false,
                true
            );
        }
    }

   

    static inline void RenderExfil()
    {
        const ExfilCacheCollection& exfilCache = *g_frameExfilSnapshot;

        if (exfilCache.empty() || !espGlobals::drawExfil)
            return;

        for (const exfilsMemory& currentExfil : exfilCache)
        {
            if (!Utils::valid_pointer(currentExfil.instance))
                continue;

            if (!Utils::isGoodVec3(currentExfil.locationWorld))
                continue;

            const int distance = static_cast<int>(
                glm::distance(
                    g_frameLocalLocation,
                    currentExfil.locationWorld
                )
                );

            if (distance > espGlobals::drawExfilDist)
                continue;

            glm::vec2 screenPos{};

            if (!ProjectWorldToScreen(
                currentExfil.locationWorld,
                &screenPos))
            {
                continue;
            }

            const std::string statusText = exfil.getExfilStatusText(currentExfil.statusRaw);

            glm::vec4 statusColour = coloursGlobals::exfils;

            switch (currentExfil.statusRaw)
            {
            case 1: // Closed
                statusColour = glm::vec4(
                    1.0f,
                    0.2f,
                    0.2f,
                    1.0f
                );
                break;

            case 4: // Open
                statusColour = glm::vec4(
                    0.2f,
                    1.0f,
                    0.2f,
                    1.0f
                );
                break;

            case 2: // Req
            case 3: // Countdown
            case 5: // Pending
            case 6: // Await. Manual
                statusColour = glm::vec4(
                    1.0f,
                    0.65f,
                    0.0f,
                    1.0f
                );
                break;

            default:
                break;
            }

            const std::string exfilText = currentExfil.extractName + " [" + statusText + "]" + " " + std::to_string(distance) + "m";

            g_DxWindow.DrawString(
                exfilText.c_str(),
                screenPos.x,
                screenPos.y + 3.0f,
                14.0f,
                statusColour,
                true,
                true
            );
        }
    }

    static inline void RenderNades()
    {
        if (!espGlobals::drawGrenades)
            return;

        const GrenadeCacheCollection& nadeCache = *g_frameGrenadeSnapshot;

        if (nadeCache.empty())
            return;

        const float screenW = ScreenWidth();
        const float screenH = ScreenHeight();

        bool closeGrenade = false;

        for (const GrenadeList& nade : nadeCache)
        {
            if (nade.type != ExplosiveType::Grenade)
                continue;

            if (!Utils::valid_pointer(nade.instance))
                continue;

            if (!Utils::isGoodVec3(nade.worldLocation))
                continue;

            const float distance =
                glm::distance(
                    g_frameLocalLocation,
                    nade.worldLocation);

            if (distance > espGlobals::drawGrenadesDist)
                continue;

            if (distance < 15.0f)
                closeGrenade = true;

            glm::vec2 screenPos{};

            if (!ProjectWorldToScreen(
                nade.worldLocation,
                &screenPos))
            {
                continue;
            }

            g_DxWindow.DrawString(
                "NADE",
                screenPos.x,
                screenPos.y + 3.0f,
                14.0f,
                coloursGlobals::grenades,
                true,
                true);
        }

        if (closeGrenade)
        {
            g_DxWindow.DrawString(
                "!! WARNING: GRENADE NEAR YOU !!",
                screenW * 0.5f,
                screenH - 60.0f,
                18.0f,
                glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
                true,
                true);
        }
    }

    static inline void RenderTripwires()
    {
        if (!espGlobals::drawTripwires)
            return;

        const GrenadeCacheCollection& explosiveCache = *g_frameGrenadeSnapshot;

        if (explosiveCache.empty())
            return;

        for (const GrenadeList& tripwire : explosiveCache)
        {
            if (tripwire.type != ExplosiveType::Tripwire ||
                !tripwire.isActive ||
                !Utils::valid_pointer(tripwire.instance) ||
                !Utils::isGoodVec3(tripwire.worldLocation) ||
                !Utils::isGoodVec3(tripwire.fromWorldLocation))
            {
                continue;
            }

            const float distance = glm::distance(g_frameLocalLocation, tripwire.worldLocation);

            if (distance > espGlobals::drawTripwiresDist)
                continue;

            glm::vec2 toScreen{};

            if (!ProjectWorldToScreen(tripwire.worldLocation, &toScreen))
                continue;

            if (espGlobals::drawTripwireLine)
            {
                glm::vec2 fromScreen{};

                if (ProjectWorldToScreen(tripwire.fromWorldLocation, &fromScreen))
                {
                    g_DxWindow.DrawLine(
                        fromScreen.x,
                        fromScreen.y,
                        toScreen.x,
                        toScreen.y,
                        coloursGlobals::tripwires,
                        2.0f);
                }
            }

            const std::string label = "TRIPWIRE " +
                std::to_string(static_cast<int>(distance)) + "m";

            g_DxWindow.DrawString(
                label.c_str(),
                toScreen.x,
                toScreen.y + 3.0f,
                14.0f,
                coloursGlobals::tripwires,
                true,
                true);
        }
    }

    static inline void DrawPlayerSkeleton(
        const std::vector<glm::vec2>& bones,
        const glm::vec4& colour)
    {
        auto drawBoneLine = [&](int a, int b)
            {
                g_DxWindow.DrawLine(
                    bones[a].x,
                    bones[a].y,
                    bones[b].x,
                    bones[b].y,
                    colour,
                    1.0f
                );
            };

        drawBoneLine(boneListIndexes::Head, boneListIndexes::Pelvis);

        drawBoneLine(boneListIndexes::Head, boneListIndexes::LForearm);
        drawBoneLine(boneListIndexes::LForearm, boneListIndexes::LPalm);

        drawBoneLine(boneListIndexes::Head, boneListIndexes::RForearm);
        drawBoneLine(boneListIndexes::RForearm, boneListIndexes::RPalm);

        drawBoneLine(boneListIndexes::Pelvis, boneListIndexes::LThigh);
        drawBoneLine(boneListIndexes::LThigh, boneListIndexes::LFoot);

        drawBoneLine(boneListIndexes::Pelvis, boneListIndexes::RThigh);
        drawBoneLine(boneListIndexes::RThigh, boneListIndexes::RFoot);
    }

    static inline bool GetPlayerBoneScreenPositions(
        const PlayerCache& player,
        std::vector<glm::vec2>& outBones)
    {
        if (player.bonePositions.empty())
            return false;

        outBones.clear();
        outBones.resize(player.bonePositions.size());

        for (size_t i = 0; i < player.bonePositions.size(); ++i)
        {
            glm::vec2 screenPos{};

            if (!ProjectWorldToScreen(player.bonePositions[i], &screenPos))
                return false;

            if (screenPos.x == 0.0f && screenPos.y == 0.0f)
                return false;

            outBones[i] = screenPos;
        }

        return true;
    }

    static inline bool HasBones(
        const PlayerCache& player,
        std::initializer_list<int> indexes)
    {
        const size_t count = player.bonePositions.size();

        if (count == 0)
            return false;

        for (int index : indexes)
        {
            if (index < 0)
                return false;

            if (static_cast<size_t>(index) >= count)
                return false;

            const size_t boneIndex = static_cast<size_t>(index);
            if (boneIndex >= player.bonePtrs.size() ||
                !Utils::valid_pointer(player.bonePtrs[boneIndex]))
            {
                return false;
            }

            const glm::vec3& position = player.bonePositions[boneIndex];
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z) ||
                (std::fabs(position.x) < 0.001f &&
                    std::fabs(position.y) < 0.001f &&
                    std::fabs(position.z) < 0.001f))
            {
                return false;
            }
        }

        return true;
    }

    static inline void RenderAmmoHud()
    {
        const PlayerCacheCollection& cache = *g_framePlayerSnapshot;

        if (cache.empty())
            return;

        for (const auto& player : cache)
        {
            if (!player.isLocal)
                continue;

            const HandsInfo& handInfo = player.observedHandsInfo;

            std::string ammoName = CleanText(handInfo.ammoName);

            if (ammoName.empty())
                ammoName = "?";

            const int chamberCount = std::max(0, handInfo.chamberCount);
            const int magazineCount = std::max(0, handInfo.magazineCount);

            std::string ammoText =
                ammoName +
                " (" +
                std::to_string(chamberCount) +
                "/" +
                std::to_string(magazineCount) +
                ")";

            const float screenW = ScreenWidth();
            const float screenH = ScreenHeight();

            const float fontSize = 30.0f;
            const float marginX = 30.0f;
            const float marginY = 60.0f;

            const float approxTextWidth =
                static_cast<float>(ammoText.length()) * (fontSize * 0.55f);

            const float x = screenW - approxTextWidth - marginX;
            const float y = screenH - marginY;

            g_DxWindow.DrawString(
                ammoText,
                x,
                y,
                fontSize,
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                false,                              
                true,                               
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)  
            );

            return;
        }
    }

    static inline void RenderPlayers()
    {
        
            const PlayerCacheCollection& cache = *g_framePlayerSnapshot;

            if (cache.empty())
                return;

            for (const auto& player : cache)
            {
                if (!Utils::valid_pointer(player.instance) ||
                    player.isZombie ||
                    player.isLocal ||
                    player.hasExfiled ||
                    player.isDead)
                {
                    continue;
                }

                if (player.distance > espGlobals::drawPlayerDist)
                    continue;

                if (player.distance == 0)
                    continue;

                glm::vec2 screenPos{};
                if (!ProjectWorldToScreen(player.location, &screenPos))
                    continue;

                const glm::vec4 playerColour = player.colour;

                std::string cleanName = CleanText(player.name);

                std::string info =
                    cleanName +
                    " [" +
                    std::to_string(player.distance) +
                    "m]";

                g_DxWindow.DrawString(
                    info,
                    screenPos.x,
                    screenPos.y + 5.0f,
                    13.0f,
                    playerColour,
                    true,
                    true
                );

                if (player.isBTR)
                    continue;

                const std::string itemName = CleanText(player.observedHandsInfo.itemName);
                const std::string ammoName = CleanText(player.observedHandsInfo.ammoName);

                if (!itemName.empty() || !ammoName.empty())
                {
                    g_DxWindow.DrawString(
                        itemName + " (" + ammoName + "/" + std::to_string(player.observedHandsInfo.magazineCount) + ")",
                        screenPos.x,
                        screenPos.y + 20.0f,
                        13.0f,
                        playerColour,
                        true,
                        true
                    );
                }

                // ----------------------------
                // Head dot
                // ----------------------------
                if (espGlobals::drawHeadDot &&
                    HasBones(player, { boneListIndexes::Head }))
                {
                    glm::vec2 headPos{};

                    if (ProjectWorldToScreen(
                        player.bonePositions[boneListIndexes::Head],
                        &headPos))
                    {
                        if (screenPos.x != 0.0f && screenPos.y != 0.0f)
                        {
                            const float padding = 2.0f;
                            const float height = screenPos.y - headPos.y + padding;
                            float radius = (height / 12) * (espGlobals::headDotSize / 5);
                            if (radius < 1.0f) radius = 1.0f;
                            float verticalOffset = height / 20.0f;
                            glm::vec2 circleCenter = { headPos.x, headPos.y - verticalOffset };
                            g_DxWindow.DrawCircle(
                                circleCenter.x,
                                circleCenter.y,
                                radius,
                                playerColour,
                                1.0f
                            );

                        }

                    }
                }

                // ----------------------------
                // Player box
                // ----------------------------
                if (espGlobals::drawBoxPlayers &&
                    HasBones(player, { boneListIndexes::Head }))
                {
                    glm::vec2 screenHead{};

                    if (ProjectWorldToScreen(
                        player.bonePositions[boneListIndexes::Head],
                        &screenHead))
                    {
                        if (screenPos.x != 0.0f && screenPos.y != 0.0f)
                        {
                            const float padding = 2.0f;
                            const float height = screenPos.y - screenHead.y + padding;
                            const float width = (height / 2.0f) + padding;

                            if (height > 4.0f && width > 2.0f)
                            {
                                const glm::vec2 boxStart =
                                {
                                    screenPos.x - width * 0.5f - padding,
                                    screenPos.y - height
                                };

                                DrawCornerBox(
                                    boxStart.x,
                                    boxStart.y,
                                    width,
                                    height,
                                    playerColour,
                                    1.0f
                                );
                            }
                        }
                    }
                }

                // ----------------------------
                // Skeleton
                // ----------------------------
                if (espGlobals::drawSkeletons)
                {
                    if (!HasBones(player,
                        {
                            boneListIndexes::Head,
                            boneListIndexes::Pelvis,
                            boneListIndexes::LForearm,
                            boneListIndexes::LPalm,
                            boneListIndexes::RForearm,
                            boneListIndexes::RPalm,
                            boneListIndexes::LThigh,
                            boneListIndexes::LFoot,
                            boneListIndexes::RThigh,
                            boneListIndexes::RFoot
                        }))
                    {
                        continue;
                    }

                    std::vector<glm::vec2> boneScreenPositions;

                    if (!GetPlayerBoneScreenPositions(player, boneScreenPositions))
                        continue;

                    DrawPlayerSkeleton(boneScreenPositions, playerColour);
                }
            }
        
    }

    static inline void RenderLoot()
    {
        if (!espGlobals::drawLoot)
            return;

        const LootCacheCollection& lootList = *g_frameLootSnapshot;

        for (const auto& loot : lootList)
        {
            if (loot.pendingResolve || loot.failed)
                continue;

            if (!loot.hasValidPosition)
                continue;

            if (!loot.wanted)
                continue;

            if (loot.isQuestItem && !espGlobals::drawQuestHelper)
                continue;

            if (loot.isCorpse && !espGlobals::drawCorpse)
                continue;

            const int distance = static_cast<int>(glm::distance(g_frameLocalLocation, loot.worldLocation));

            if (distance > espGlobals::drawLootDist)
                continue;

            glm::vec2 screenPos{};
            if (!ProjectWorldToScreen(loot.worldLocation, &screenPos))
                continue;

            const glm::vec4 lootColour = loot.isQuestItem
                ? coloursGlobals::questMarker
                : loot.color;

            std::string lootText =
                GetLootDisplayName(loot) +
                " " +
                std::to_string(distance) +
                "m";

            g_DxWindow.DrawFilledCircle(
                screenPos.x,
                screenPos.y,
                2.0f,
                lootColour
            );

            g_DxWindow.DrawString(
                lootText,
                screenPos.x + 6.0f,
                screenPos.y - 3.0f,
                13.0f,
                lootColour,
                false,
                true
            );
        }
    }

    static inline void RenderLocalLookedAtAlert()
    {
        const PlayerCacheCollection& cache = *g_framePlayerSnapshot;

        if (!AimLineTargeting::IsLocalBeingLookedAt(cache))
        {
            return;
        }

        constexpr float padding = 8.0f;
        const float pulse = Wave01(NowSeconds() * 31.4159265359f);
        const glm::vec4 warningColour =
        {
            1.0f,
            0.38f,
            0.0f,
            0.30f + (pulse * 0.70f)
        };

        g_DxWindow.DrawRect(
            padding,
            padding,
            ScreenWidth() - (padding * 2.0f),
            ScreenHeight() - (padding * 2.0f),
            warningColour,
            2.0f);
    }

    static inline void RenderMainScene()
    {
        SafeRenderStage("RenderHUD", []()
            {
                RenderHUD();
            });

        SafeRenderStage("RenderAmmoHud", []()
            {
                RenderAmmoHud();
            });

        SafeRenderStage("RenderCrosshair", []()
            {
                RenderCrosshair();
            });

        SafeRenderStage("RenderFireportVisual", []()
            {
                RenderFireportVisual();
            });

        SafeRenderStage("RenderAimFovRing", []()
            {
                RenderAimFovRing();
            });

        SafeRenderStage("RenderPlayers", []()
            {
                RenderPlayers();
            });

        SafeRenderStage("RenderNades", []()
            {
                RenderNades();
            });

        SafeRenderStage("RenderTripwires", []()
            {
                RenderTripwires();
            });

        SafeRenderStage("RenderLoot", []()
            {
                RenderLoot();
            });

        SafeRenderStage("RenderTasks", []()
            {
                RenderTasks();
            });

        SafeRenderStage("RenderExfils", []()
            {
                RenderExfil();
            });

        SafeRenderStage("RenderLocalLookedAtAlert", []()
            {
                RenderLocalLookedAtAlert();
            });
    }

    static inline bool HasActiveScene()
    {
        return IsTestSceneEnabled() || appGlobals::runRadar;
    }

    static inline bool Render()
    {
        CaptureFrameSnapshots();
        g_DxWindow.BeginDrawList();

        bool activeScene = false;

        SafeRenderStage("RenderEntry", [&]()
            {
                if (IsTestSceneEnabled())
                {
                    SafeRenderStage("RenderTestScene", []()
                        {
                            RenderTestScene();
                        });

                    activeScene = true;
                }
                else if (appGlobals::runRadar.load(std::memory_order_acquire))
                {
                    RenderMainScene();
                    activeScene = true;
                }
            });

        g_DxWindow.SubmitDrawList();
        ReleaseFrameSnapshots();

        g_currentStage.store("Idle", std::memory_order_relaxed);

        return activeScene;
    }
}
