#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

#include <glm/glm.hpp>

#include "../external/imgui/imgui.h"

enum class MarkerShape : int
{
    Circle,
    Square,
    Triangle,
    Diamond,
    Cross,
    X,
    Tank
};

enum class MarkerCategory : int
{
    Player,
    Loot,
    Quest,
    Exfil,
    Grenade,
    Tripwire,
    Corpse,
    Container,
    GroupLine,
    Crosshair,
    FovCircle,
    Count
};

enum class MarkerView : int
{
    Radar,
    Fuser
};

enum class PlayerMarkerType : int
{
    Pmc,
    PlayerScav,
    Ai,
    Boss,
    BlackDivision,
    Local,
    Friendly,
    Watched,
    Btr,
    Count
};

struct MarkerViewStyle
{
    MarkerShape shape = MarkerShape::Circle;
    float size = 9.0f;
    float fontSize = 14.0f;
};

struct MarkerStyle
{
    MarkerViewStyle radar;
    MarkerViewStyle fuser;
    float fillOpacity = 0.85f;
    bool markerOutline = true;
    float markerOutlineThickness = 1.25f;
    glm::vec4 markerOutlineColour = glm::vec4(0.0f, 0.0f, 0.0f, 0.92f);
    bool useMarkerColourForText = true;
    glm::vec4 textColour = glm::vec4(1.0f);
    bool textOutline = true;
    glm::vec4 textOutlineColour = glm::vec4(0.0f, 0.0f, 0.0f, 0.95f);
    bool textShadow = false;
    float shadowOffset = 2.0f;
};

struct PlayerMarkerStyle
{
    MarkerShape shape = MarkerShape::Triangle;
    float size = 9.0f;
};

struct AppearanceConfig
{
    static constexpr int CurrentVersion = 2;
    int version = CurrentVersion;
    std::array<MarkerStyle, static_cast<size_t>(MarkerCategory::Count)> markers{};
    std::array<PlayerMarkerStyle, static_cast<size_t>(PlayerMarkerType::Count)> playerMarkers{};
};

class AppearanceManager
{
public:
    AppearanceManager();

    bool LoadOrCreateFromLegacy();
    bool Save() const;
    AppearanceConfig GetConfig() const;
    MarkerStyle GetStyle(MarkerCategory category) const;
    PlayerMarkerStyle GetPlayerMarkerStyle(PlayerMarkerType type) const;
    void Apply(const AppearanceConfig& config);
    void ResetStyle(AppearanceConfig& config, MarkerCategory category) const;
    void ResetPlayerMarkerStyle(AppearanceConfig& config, PlayerMarkerType type) const;
    AppearanceConfig Defaults() const;

private:
    std::atomic<std::shared_ptr<const AppearanceConfig>> snapshot_;
    mutable std::mutex fileMutex_;
};

extern AppearanceManager appearanceManager;

const char* MarkerCategoryName(MarkerCategory category);
const char* MarkerShapeName(MarkerShape shape);
const char* PlayerMarkerTypeName(PlayerMarkerType type);
const MarkerViewStyle& GetMarkerViewStyle(const MarkerStyle& style, MarkerView view);
MarkerViewStyle& GetMarkerViewStyle(MarkerStyle& style, MarkerView view);
glm::vec4 GetMarkerTextColour(const MarkerStyle& style, const glm::vec4& markerColour);

void DrawRadarMarkerShape(ImDrawList* drawList, const ImVec2& centre, float rotationRadians, MarkerCategory category, const glm::vec4& colour, float sizeScale = 1.0f);
void DrawRadarMarkerShape(ImDrawList* drawList, const ImVec2& centre, float rotationRadians, const MarkerStyle& style, MarkerShape shape, float markerSize,
    const glm::vec4& colour, float sizeScale = 1.0f);
void DrawRadarPlayerMarkerShape(ImDrawList* drawList, const ImVec2& centre, float rotationRadians, PlayerMarkerType type, const glm::vec4& colour, float sizeScale = 1.0f);
void DrawCrosshairShape(ImDrawList* drawList, const ImVec2& centre, const MarkerViewStyle& view, const MarkerStyle& style, const glm::vec4& colour,
    float sizeScale = 1.0f);
void DrawRadarStyledText(ImDrawList* drawList, ImFont* font, const ImVec2& position, MarkerCategory category, const glm::vec4& markerColour, const char* text, float sizeScale = 1.0f, bool centered = false);
