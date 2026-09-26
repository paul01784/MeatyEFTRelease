#include "Appearance.h"

#include "globals.h"
#include "../external/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace
{
namespace fs = std::filesystem;

constexpr size_t CategoryIndex(MarkerCategory category)
{
    return static_cast<size_t>(category);
}

MarkerStyle MakeStyle(MarkerShape radarShape, MarkerShape fuserShape, float radarSize, float fuserSize, float radarFontSize, float fuserFontSize)
{
    MarkerStyle style{};
    style.radar = {radarShape, radarSize, radarFontSize};
    style.fuser = {fuserShape, fuserSize, fuserFontSize};
    return style;
}

glm::vec4 ReadColour(const nlohmann::json& object, const char* key, const glm::vec4& fallback)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() != 4)
        return fallback;

    try
    {
        return glm::vec4((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), (*it)[3].get<float>());
    }
    catch (const nlohmann::json::exception&)
    {
        return fallback;
    }
}

nlohmann::json ColourJson(const glm::vec4& colour)
{
    return nlohmann::json::array({colour.r, colour.g, colour.b, colour.a});
}

void WritePalette(nlohmann::json& colours)
{
    colours = {
        {"playerPMC", ColourJson(coloursGlobals::playerPMC)},
        {"playerScav", ColourJson(coloursGlobals::playerScav)},
        {"playerAI", ColourJson(coloursGlobals::playerAI)},
        {"playerBoss", ColourJson(coloursGlobals::playerBoss)},
        {"playerBlackDiv", ColourJson(coloursGlobals::playerBlackDiv)},
        {"aiBTR", ColourJson(coloursGlobals::aiBTR)},
        {"playerWatched", ColourJson(coloursGlobals::playerWatched)},
        {"playerFriendly", ColourJson(coloursGlobals::playerFriendly)},
        {"playerLocal", ColourJson(coloursGlobals::playerLocal)},
        {"playerCorpse", ColourJson(coloursGlobals::playerCorpse)},
        {"playerGroupLine", ColourJson(coloursGlobals::playerGroupLine)},
        {"grenades", ColourJson(coloursGlobals::grenades)},
        {"tripwires", ColourJson(coloursGlobals::tripwires)},
        {"exfils", ColourJson(coloursGlobals::exfils)},
        {"questMarker", ColourJson(coloursGlobals::questMarker)},
        {"crosshair", ColourJson(coloursGlobals::crosshair)},
        {"fovCircle", ColourJson(coloursGlobals::fovCircle)},
        {"questColour", ColourJson(coloursGlobals::questColour)},
        {"wishListColour", ColourJson(coloursGlobals::wishListColour)},
        {"valueLootColour", ColourJson(coloursGlobals::valueLootColour)},
        {"containerColour", ColourJson(coloursGlobals::containerColour)}
    };
}

void ReadPalette(const nlohmann::json& colours)
{
    coloursGlobals::playerPMC = ReadColour(colours, "playerPMC", coloursGlobals::playerPMC);
    coloursGlobals::playerScav = ReadColour(colours, "playerScav", coloursGlobals::playerScav);
    coloursGlobals::playerAI = ReadColour(colours, "playerAI", coloursGlobals::playerAI);
    coloursGlobals::playerBoss = ReadColour(colours, "playerBoss", coloursGlobals::playerBoss);
    coloursGlobals::playerBlackDiv = ReadColour(colours, "playerBlackDiv", coloursGlobals::playerBlackDiv);
    coloursGlobals::aiBTR = ReadColour(colours, "aiBTR", coloursGlobals::aiBTR);
    coloursGlobals::playerWatched = ReadColour(colours, "playerWatched", coloursGlobals::playerWatched);
    coloursGlobals::playerFriendly = ReadColour(colours, "playerFriendly", coloursGlobals::playerFriendly);
    coloursGlobals::playerLocal = ReadColour(colours, "playerLocal", coloursGlobals::playerLocal);
    coloursGlobals::playerCorpse = ReadColour(colours, "playerCorpse", coloursGlobals::playerCorpse);
    coloursGlobals::playerGroupLine = ReadColour(colours, "playerGroupLine", coloursGlobals::playerGroupLine);
    coloursGlobals::grenades = ReadColour(colours, "grenades", coloursGlobals::grenades);
    coloursGlobals::tripwires = ReadColour(colours, "tripwires", coloursGlobals::tripwires);
    coloursGlobals::exfils = ReadColour(colours, "exfils", coloursGlobals::exfils);
    coloursGlobals::questMarker = ReadColour(colours, "questMarker", coloursGlobals::questMarker);
    coloursGlobals::crosshair = ReadColour(colours, "crosshair", coloursGlobals::crosshair);
    coloursGlobals::fovCircle = ReadColour(colours, "fovCircle", coloursGlobals::fovCircle);
    coloursGlobals::questColour = ReadColour(colours, "questColour", coloursGlobals::questColour);
    coloursGlobals::wishListColour = ReadColour(colours, "wishListColour", coloursGlobals::wishListColour);
    coloursGlobals::valueLootColour = ReadColour(colours, "valueLootColour", coloursGlobals::valueLootColour);
    coloursGlobals::containerColour = ReadColour(colours, "containerColour", coloursGlobals::containerColour);
}

nlohmann::json ViewStyleJson(const MarkerViewStyle& style)
{
    return {{"shape", static_cast<int>(style.shape)}, {"size", style.size}, {"fontSize", style.fontSize}};
}

void ReadViewStyle(const nlohmann::json& json, MarkerViewStyle& style)
{
    style.shape = static_cast<MarkerShape>(std::clamp(json.value("shape", static_cast<int>(style.shape)), 0, 5));
    style.size = std::clamp(json.value("size", style.size), 3.0f, 30.0f);
    style.fontSize = std::clamp(json.value("fontSize", style.fontSize), 8.0f, 32.0f);
}

nlohmann::json PlayerMarkerStyleJson(const PlayerMarkerStyle& style)
{
    return {{"shape", static_cast<int>(style.shape)}, {"size", style.size}};
}

void ReadPlayerMarkerStyle(const nlohmann::json& json, PlayerMarkerStyle& style, bool allowTank)
{
    style.shape = static_cast<MarkerShape>(std::clamp(json.value("shape", static_cast<int>(style.shape)), 0, allowTank ? 6 : 5));
    style.size = std::clamp(json.value("size", style.size), 3.0f, 30.0f);
}

nlohmann::json MarkerStyleJson(const MarkerStyle& style)
{
    return {
        {"radar", ViewStyleJson(style.radar)},
        {"fuser", ViewStyleJson(style.fuser)},
        {"fillOpacity", style.fillOpacity},
        {"markerOutline", style.markerOutline},
        {"markerOutlineThickness", style.markerOutlineThickness},
        {"markerOutlineColour", ColourJson(style.markerOutlineColour)},
        {"useMarkerColourForText", style.useMarkerColourForText},
        {"textColour", ColourJson(style.textColour)},
        {"textOutline", style.textOutline},
        {"textOutlineColour", ColourJson(style.textOutlineColour)},
        {"textShadow", style.textShadow},
        {"shadowOffset", style.shadowOffset}
    };
}

void ReadMarkerStyle(const nlohmann::json& json, MarkerStyle& style)
{
    if (const auto it = json.find("radar"); it != json.end() && it->is_object())
        ReadViewStyle(*it, style.radar);
    if (const auto it = json.find("fuser"); it != json.end() && it->is_object())
        ReadViewStyle(*it, style.fuser);
    style.fillOpacity = std::clamp(json.value("fillOpacity", style.fillOpacity), 0.0f, 1.0f);
    style.markerOutline = json.value("markerOutline", style.markerOutline);
    style.markerOutlineThickness = std::clamp(json.value("markerOutlineThickness", style.markerOutlineThickness), 0.5f, 5.0f);
    style.markerOutlineColour = ReadColour(json, "markerOutlineColour", style.markerOutlineColour);
    style.useMarkerColourForText = json.value("useMarkerColourForText", style.useMarkerColourForText);
    style.textColour = ReadColour(json, "textColour", style.textColour);
    style.textOutline = json.value("textOutline", style.textOutline);
    style.textOutlineColour = ReadColour(json, "textOutlineColour", style.textOutlineColour);
    style.textShadow = json.value("textShadow", style.textShadow);
    style.shadowOffset = std::clamp(json.value("shadowOffset", style.shadowOffset), 1.0f, 6.0f);
}

fs::path AppearancePath()
{
    return fs::current_path() / "configs" / "config_appearance.json";
}
}

AppearanceManager appearanceManager;

AppearanceManager::AppearanceManager()
    : snapshot_(std::make_shared<const AppearanceConfig>(Defaults()))
{
}

AppearanceConfig AppearanceManager::Defaults() const
{
    AppearanceConfig config{};
    config.markers[CategoryIndex(MarkerCategory::Player)] = MakeStyle(MarkerShape::Triangle, MarkerShape::Diamond, 9.0f, 8.0f, 16.0f, 13.0f);
    config.markers[CategoryIndex(MarkerCategory::Loot)] = MakeStyle(MarkerShape::Diamond, MarkerShape::Diamond, 6.0f, 6.0f, 14.0f, 13.0f);
    config.markers[CategoryIndex(MarkerCategory::Quest)] = MakeStyle(MarkerShape::Square, MarkerShape::Square, 7.0f, 7.0f, 15.0f, 13.0f);
    config.markers[CategoryIndex(MarkerCategory::Exfil)] = MakeStyle(MarkerShape::Circle, MarkerShape::Circle, 8.0f, 8.0f, 14.0f, 14.0f);
    config.markers[CategoryIndex(MarkerCategory::Grenade)] = MakeStyle(MarkerShape::Circle, MarkerShape::Circle, 7.0f, 7.0f, 14.0f, 14.0f);
    config.markers[CategoryIndex(MarkerCategory::Tripwire)] = MakeStyle(MarkerShape::Triangle, MarkerShape::Triangle, 7.0f, 7.0f, 14.0f, 14.0f);
    config.markers[CategoryIndex(MarkerCategory::Corpse)] = MakeStyle(MarkerShape::Square, MarkerShape::Square, 7.0f, 7.0f, 14.0f, 13.0f);
    config.markers[CategoryIndex(MarkerCategory::Container)] = MakeStyle(MarkerShape::Square, MarkerShape::Square, 7.0f, 7.0f, 14.0f, 13.0f);
    config.markers[CategoryIndex(MarkerCategory::GroupLine)] = MakeStyle(MarkerShape::Circle, MarkerShape::Circle, 7.0f, 7.0f, 14.0f, 13.0f);
    config.markers[CategoryIndex(MarkerCategory::Crosshair)] = MakeStyle(MarkerShape::Circle, MarkerShape::Circle, 8.0f, 8.0f, 14.0f, 14.0f);
    config.markers[CategoryIndex(MarkerCategory::Crosshair)].fillOpacity = 1.0f;
    config.markers[CategoryIndex(MarkerCategory::FovCircle)] = MakeStyle(MarkerShape::Circle, MarkerShape::Circle, 16.0f, 16.0f, 14.0f, 14.0f);
    for (PlayerMarkerStyle& playerMarker : config.playerMarkers)
    {
        playerMarker.shape = config.markers[CategoryIndex(MarkerCategory::Player)].radar.shape;
        playerMarker.size = config.markers[CategoryIndex(MarkerCategory::Player)].radar.size;
    }
    return config;
}

bool AppearanceManager::LoadOrCreateFromLegacy()
{
    const fs::path path = AppearancePath();
    if (!fs::exists(path))
    {
        Apply(Defaults());
        return Save();
    }

    try
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        nlohmann::json root;
        file >> root;
        AppearanceConfig config = Defaults();
        config.version = root.value("version", AppearanceConfig::CurrentVersion);

        if (const auto colours = root.find("colours"); colours != root.end() && colours->is_object())
            ReadPalette(*colours);

        bool hasCrosshairStyle = false;
        if (const auto markers = root.find("markers"); markers != root.end() && markers->is_object())
        {
            for (int index = 0; index < static_cast<int>(MarkerCategory::Count); ++index)
            {
                const MarkerCategory category = static_cast<MarkerCategory>(index);
                if (const auto item = markers->find(MarkerCategoryName(category)); item != markers->end() && item->is_object())
                {
                    ReadMarkerStyle(*item, config.markers[static_cast<size_t>(index)]);
                    hasCrosshairStyle |= category == MarkerCategory::Crosshair;
                }
            }
        }

        const MarkerViewStyle& legacyPlayerStyle = config.markers[CategoryIndex(MarkerCategory::Player)].radar;
        for (PlayerMarkerStyle& playerMarker : config.playerMarkers)
        {
            playerMarker.shape = legacyPlayerStyle.shape;
            playerMarker.size = legacyPlayerStyle.size;
        }
        if (const auto playerMarkers = root.find("playerMarkers"); playerMarkers != root.end() && playerMarkers->is_object())
        {
            for (int index = 0; index < static_cast<int>(PlayerMarkerType::Count); ++index)
            {
                const PlayerMarkerType type = static_cast<PlayerMarkerType>(index);
                if (const auto item = playerMarkers->find(PlayerMarkerTypeName(type)); item != playerMarkers->end() && item->is_object())
                    ReadPlayerMarkerStyle(*item, config.playerMarkers[static_cast<size_t>(index)], type == PlayerMarkerType::Btr);
            }
        }

        if (!hasCrosshairStyle)
        {
            MarkerStyle& crosshair = config.markers[CategoryIndex(MarkerCategory::Crosshair)];
            const MarkerShape shape = espGlobals::crosshairType == 1 ? MarkerShape::Cross : MarkerShape::Circle;
            crosshair.radar.shape = shape;
            crosshair.fuser.shape = shape;
            crosshair.radar.size = std::clamp(static_cast<float>(espGlobals::crosshairSize), 3.0f, 30.0f);
            crosshair.fuser.size = crosshair.radar.size;
        }

        Apply(config);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool AppearanceManager::Save() const
{
    std::scoped_lock lock(fileMutex_);
    try
    {
        const fs::path path = AppearancePath();
        fs::create_directories(path.parent_path());
        const AppearanceConfig config = GetConfig();
        nlohmann::json root;
        root["version"] = AppearanceConfig::CurrentVersion;
        WritePalette(root["colours"]);
        for (int index = 0; index < static_cast<int>(MarkerCategory::Count); ++index)
        {
            const MarkerCategory category = static_cast<MarkerCategory>(index);
            root["markers"][MarkerCategoryName(category)] = MarkerStyleJson(config.markers[static_cast<size_t>(index)]);
        }
        for (int index = 0; index < static_cast<int>(PlayerMarkerType::Count); ++index)
        {
            const PlayerMarkerType type = static_cast<PlayerMarkerType>(index);
            root["playerMarkers"][PlayerMarkerTypeName(type)] = PlayerMarkerStyleJson(config.playerMarkers[static_cast<size_t>(index)]);
        }

        std::ofstream file(path);
        if (!file.is_open())
            return false;
        file << root.dump(4);
        return true;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

AppearanceConfig AppearanceManager::GetConfig() const
{
    const std::shared_ptr<const AppearanceConfig> snapshot = snapshot_.load(std::memory_order_acquire);
    return snapshot ? *snapshot : Defaults();
}

MarkerStyle AppearanceManager::GetStyle(MarkerCategory category) const
{
    const AppearanceConfig config = GetConfig();
    const size_t index = CategoryIndex(category);
    return index < config.markers.size() ? config.markers[index] : MarkerStyle{};
}

PlayerMarkerStyle AppearanceManager::GetPlayerMarkerStyle(PlayerMarkerType type) const
{
    const AppearanceConfig config = GetConfig();
    const size_t index = static_cast<size_t>(type);
    return index < config.playerMarkers.size() ? config.playerMarkers[index] : PlayerMarkerStyle{};
}

void AppearanceManager::Apply(const AppearanceConfig& config)
{
    snapshot_.store(std::make_shared<const AppearanceConfig>(config), std::memory_order_release);
}

void AppearanceManager::ResetStyle(AppearanceConfig& config, MarkerCategory category) const
{
    const size_t index = CategoryIndex(category);
    if (index < config.markers.size())
        config.markers[index] = Defaults().markers[index];
}

void AppearanceManager::ResetPlayerMarkerStyle(AppearanceConfig& config, PlayerMarkerType type) const
{
    const size_t index = static_cast<size_t>(type);
    if (index < config.playerMarkers.size())
        config.playerMarkers[index] = Defaults().playerMarkers[index];
}

const char* MarkerCategoryName(MarkerCategory category)
{
    static constexpr const char* Names[] = {"Player", "Loot", "Quest", "Exfil", "Grenade", "Tripwire", "Corpse", "Container", "Group Line", "Crosshair", "FOV Circle"};
    const int index = static_cast<int>(category);
    return index >= 0 && index < IM_ARRAYSIZE(Names) ? Names[index] : "Unknown";
}

const char* MarkerShapeName(MarkerShape shape)
{
    static constexpr const char* Names[] = {"Circle", "Square", "Triangle", "Diamond", "Cross", "X", "Tank"};
    const int index = static_cast<int>(shape);
    return index >= 0 && index < IM_ARRAYSIZE(Names) ? Names[index] : "Circle";
}

const char* PlayerMarkerTypeName(PlayerMarkerType type)
{
    static constexpr const char* Names[] = {"PMC", "Player scav", "AI", "Boss", "Black Division", "Local player", "Friendly", "Watched", "BTR"};
    const int index = static_cast<int>(type);
    return index >= 0 && index < IM_ARRAYSIZE(Names) ? Names[index] : "PMC";
}

const MarkerViewStyle& GetMarkerViewStyle(const MarkerStyle& style, MarkerView view)
{
    return view == MarkerView::Radar ? style.radar : style.fuser;
}

MarkerViewStyle& GetMarkerViewStyle(MarkerStyle& style, MarkerView view)
{
    return view == MarkerView::Radar ? style.radar : style.fuser;
}

glm::vec4 GetMarkerTextColour(const MarkerStyle& style, const glm::vec4& markerColour)
{
    glm::vec4 result = style.useMarkerColourForText ? markerColour : style.textColour;
    result.a = std::clamp(result.a * markerColour.a, 0.0f, 1.0f);
    return result;
}

void DrawRadarMarkerShape(ImDrawList* drawList, const ImVec2& centre, float rotationRadians, const MarkerStyle& style, MarkerShape shape, float markerSize,
    const glm::vec4& colour, float sizeScale)
{
    if (!drawList)
        return;

    const float radius = (std::max)(1.0f, markerSize * sizeScale);
    glm::vec4 fill = colour;
    fill.a *= style.fillOpacity;
    const ImU32 fillColour = ImGui::ColorConvertFloat4ToU32(ImVec4(fill.r, fill.g, fill.b, fill.a));
    const ImU32 outlineColour = ImGui::ColorConvertFloat4ToU32(ImVec4(style.markerOutlineColour.r, style.markerOutlineColour.g, style.markerOutlineColour.b, style.markerOutlineColour.a * colour.a));
    const float thickness = style.markerOutlineThickness;

    if (shape == MarkerShape::Tank)
    {
        const ImVec2 forward(std::cos(rotationRadians), std::sin(rotationRadians));
        const ImVec2 sideways(-forward.y, forward.x);
        const float tankScale = radius / 9.0f;
        const auto point = [&centre, &forward, &sideways, tankScale](float along, float across)
        {
            return ImVec2(centre.x + ((forward.x * along) + (sideways.x * across)) * tankScale,
                centre.y + ((forward.y * along) + (sideways.y * across)) * tankScale);
        };
        const std::array<ImVec2, 7> bodyPoints = {
            point(13.0f, 0.0f), point(8.0f, 6.0f), point(-8.0f, 6.0f), point(-12.0f, 3.0f),
            point(-12.0f, -3.0f), point(-8.0f, -6.0f), point(8.0f, -6.0f)
        };
        const ImU32 markerColour = ImGui::ColorConvertFloat4ToU32(ImVec4(colour.r, colour.g, colour.b, colour.a));
        const ImU32 bodyColour = ImGui::ColorConvertFloat4ToU32(ImVec4(12.0f / 255.0f, 15.0f / 255.0f, 17.0f / 255.0f,
            colour.a * style.fillOpacity * (225.0f / 255.0f)));

        drawList->AddConvexPolyFilled(bodyPoints.data(), static_cast<int>(bodyPoints.size()), bodyColour);
        drawList->AddPolyline(bodyPoints.data(), static_cast<int>(bodyPoints.size()), markerColour, ImDrawFlags_Closed, (std::max)(1.0f, thickness));
        for (const float side : {-8.0f, 8.0f})
        {
            const ImVec2 trackStart = point(-8.0f, side);
            const ImVec2 trackEnd = point(7.0f, side);
            if (style.markerOutline)
                drawList->AddLine(trackStart, trackEnd, outlineColour, thickness + 1.75f);
            drawList->AddLine(trackStart, trackEnd, markerColour, (std::max)(1.0f, thickness));
        }
        return;
    }

    if (shape == MarkerShape::Cross || shape == MarkerShape::X)
    {
        const bool diagonal = shape == MarkerShape::X;
        const ImVec2 firstStart = diagonal ? ImVec2(centre.x - radius, centre.y - radius) : ImVec2(centre.x - radius, centre.y);
        const ImVec2 firstEnd = diagonal ? ImVec2(centre.x + radius, centre.y + radius) : ImVec2(centre.x + radius, centre.y);
        const ImVec2 secondStart = diagonal ? ImVec2(centre.x + radius, centre.y - radius) : ImVec2(centre.x, centre.y - radius);
        const ImVec2 secondEnd = diagonal ? ImVec2(centre.x - radius, centre.y + radius) : ImVec2(centre.x, centre.y + radius);
        if (style.markerOutline)
        {
            drawList->AddLine(firstStart, firstEnd, outlineColour, thickness + 2.0f);
            drawList->AddLine(secondStart, secondEnd, outlineColour, thickness + 2.0f);
        }
        drawList->AddLine(firstStart, firstEnd, fillColour, thickness);
        drawList->AddLine(secondStart, secondEnd, fillColour, thickness);
        return;
    }

    if (shape == MarkerShape::Circle)
    {
        drawList->AddCircleFilled(centre, radius, fillColour, 24);
        if (style.markerOutline)
            drawList->AddCircle(centre, radius, outlineColour, 24, thickness);
        return;
    }

    std::array<ImVec2, 4> points{};
    int pointCount = 4;
    if (shape == MarkerShape::Triangle)
    {
        pointCount = 3;
        for (int index = 0; index < pointCount; ++index)
        {
            const float angle = rotationRadians + (static_cast<float>(index) * 2.0943951f);
            points[index] = ImVec2(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius);
        }
    }
    else
    {
        const float base = shape == MarkerShape::Diamond ? 0.0f : 0.78539816f;
        for (int index = 0; index < pointCount; ++index)
        {
            const float angle = base + (static_cast<float>(index) * 1.5707963f);
            points[index] = ImVec2(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius);
        }
    }

    drawList->AddConvexPolyFilled(points.data(), pointCount, fillColour);
    if (style.markerOutline)
        drawList->AddPolyline(points.data(), pointCount, outlineColour, ImDrawFlags_Closed, thickness);
}

void DrawRadarMarkerShape(ImDrawList* drawList, const ImVec2& centre, float rotationRadians, MarkerCategory category, const glm::vec4& colour, float sizeScale)
{
    const MarkerStyle style = appearanceManager.GetStyle(category);
    DrawRadarMarkerShape(drawList, centre, rotationRadians, style, style.radar.shape, style.radar.size, colour, sizeScale);
}

void DrawRadarPlayerMarkerShape(ImDrawList* drawList, const ImVec2& centre, float rotationRadians, PlayerMarkerType type, const glm::vec4& colour, float sizeScale)
{
    const MarkerStyle style = appearanceManager.GetStyle(MarkerCategory::Player);
    const PlayerMarkerStyle playerMarker = appearanceManager.GetPlayerMarkerStyle(type);
    DrawRadarMarkerShape(drawList, centre, rotationRadians, style, playerMarker.shape, playerMarker.size, colour, sizeScale);
}

void DrawCrosshairShape(ImDrawList* drawList, const ImVec2& centre, const MarkerViewStyle& view, const MarkerStyle& style, const glm::vec4& colour,
    float sizeScale)
{
    if (!drawList)
        return;

    const ImU32 lineColour = ImGui::ColorConvertFloat4ToU32(ImVec4(colour.r, colour.g, colour.b, colour.a));
    const float radius = (std::max)(1.0f, view.size * sizeScale);
    const float thickness = style.markerOutlineThickness;

    if (view.shape == MarkerShape::Circle)
    {
        drawList->AddCircle(centre, radius, lineColour, 32, thickness);
        return;
    }
    if (view.shape == MarkerShape::Square)
    {
        drawList->AddRect(ImVec2(centre.x - radius, centre.y - radius), ImVec2(centre.x + radius, centre.y + radius), lineColour, 0.0f, 0, thickness);
        return;
    }
    if (view.shape == MarkerShape::Triangle || view.shape == MarkerShape::Diamond)
    {
        std::array<ImVec2, 4> points{};
        const int pointCount = view.shape == MarkerShape::Triangle ? 3 : 4;
        const float step = (2.0f * IM_PI) / static_cast<float>(pointCount);
        for (int index = 0; index < pointCount; ++index)
        {
            const float angle = -IM_PI * 0.5f + step * static_cast<float>(index);
            points[index] = ImVec2(centre.x + std::cos(angle) * radius, centre.y + std::sin(angle) * radius);
        }
        drawList->AddPolyline(points.data(), pointCount, lineColour, ImDrawFlags_Closed, thickness);
        return;
    }

    const bool diagonal = view.shape == MarkerShape::X;
    const ImVec2 firstStart = diagonal ? ImVec2(centre.x - radius, centre.y - radius) : ImVec2(centre.x - radius, centre.y);
    const ImVec2 firstEnd = diagonal ? ImVec2(centre.x + radius, centre.y + radius) : ImVec2(centre.x + radius, centre.y);
    const ImVec2 secondStart = diagonal ? ImVec2(centre.x + radius, centre.y - radius) : ImVec2(centre.x, centre.y - radius);
    const ImVec2 secondEnd = diagonal ? ImVec2(centre.x - radius, centre.y + radius) : ImVec2(centre.x, centre.y + radius);
    drawList->AddLine(firstStart, firstEnd, lineColour, thickness);
    drawList->AddLine(secondStart, secondEnd, lineColour, thickness);
}

void DrawRadarStyledText(ImDrawList* drawList, ImFont* font, const ImVec2& position, MarkerCategory category, const glm::vec4& markerColour, const char* text, float sizeScale, bool centered)
{
    if (!drawList || !font || !text || text[0] == '\0')
        return;

    const MarkerStyle style = appearanceManager.GetStyle(category);
    const float fontSize = style.radar.fontSize * sizeScale;
    ImVec2 drawPosition = position;
    if (centered)
        drawPosition.x -= font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text).x * 0.5f;

    const glm::vec4 textColour = GetMarkerTextColour(style, markerColour);
    const ImU32 textDrawColour = ImGui::ColorConvertFloat4ToU32(ImVec4(textColour.r, textColour.g, textColour.b, textColour.a));
    const ImU32 effect = ImGui::ColorConvertFloat4ToU32(ImVec4(style.textOutlineColour.r, style.textOutlineColour.g, style.textOutlineColour.b, style.textOutlineColour.a * markerColour.a));

    if (style.textShadow)
        drawList->AddText(font, fontSize, ImVec2(drawPosition.x + style.shadowOffset, drawPosition.y + style.shadowOffset), effect, text);
    if (style.textOutline)
    {
        drawList->AddText(font, fontSize, ImVec2(drawPosition.x - 1.0f, drawPosition.y), effect, text);
        drawList->AddText(font, fontSize, ImVec2(drawPosition.x + 1.0f, drawPosition.y), effect, text);
        drawList->AddText(font, fontSize, ImVec2(drawPosition.x, drawPosition.y - 1.0f), effect, text);
        drawList->AddText(font, fontSize, ImVec2(drawPosition.x, drawPosition.y + 1.0f), effect, text);
    }
    drawList->AddText(font, fontSize, drawPosition, textDrawColour, text);
}
