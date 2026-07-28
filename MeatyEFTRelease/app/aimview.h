#pragma once

#include <imgui/imgui.h>

struct PlayerCache;

struct AimViewConfig {
    bool enabled = true;
    bool showCrosshair = true;
    bool showPlayers = true;
    bool showLoot = true;
    bool showContainers = true;
    bool showQuests = true;

    float zoom = 2.5f;
    float minimumZoom = 1.0f;
    float maximumZoom = 6.0f;

    float maximumDistancePlayers = 350.0f;
    float maximumDistanceLoot = 350.0f;
    float maximumDistanceContainers = 350.0f;
    float maximumDistanceQuest = 350.0f;

    ImVec2 defaultWindowSize = ImVec2(420.0f, 240.0f);
};

class AimViewWidget {
public:
    void Render(const ImVec2& sourceResolution);

    AimViewConfig& GetConfig();
    const AimViewConfig& GetConfig() const;

    bool MapScreenPoint(const ImVec2& sourcePoint, ImVec2& widgetPoint, float padding = 0.0f) const;

    float MapScreenLength(float sourceLength) const;

private:
    void UpdateTransform(const ImVec2& sourceResolution);

    void DrawBackground(ImDrawList* drawList) const;
    void DrawCrosshair(ImDrawList* drawList) const;
    void DrawPlayers(ImDrawList* drawList);
    void DrawLoot(ImDrawList* drawList);
    void DrawContainers(ImDrawList* drawList);

    bool DrawPlayerSkeleton(ImDrawList* drawList, const PlayerCache& player, ImU32 colour, ImVec2& outHeadPosition);

private:
    AimViewConfig config_{};

    ImVec2 sourceResolution_{};
    ImVec2 sourceCentre_{};

    ImVec2 canvasPosition_{};
    ImVec2 canvasSize_{};
    ImVec2 canvasCentre_{};

    float transformScale_ = 1.0f;
    bool transformValid_ = false;

};

extern AimViewWidget g_AimViewWidget;