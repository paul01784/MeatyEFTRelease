#pragma once
#include "../Tarkov/GameWorld/RegisteredPlayers.h"
#include "../Tarkov/GameWorld/Exits/Exfil.h"
#include "../Tarkov/GameWorld/Explosives/ExplosiveManager.h"
#include "../Core/Utilities.h"
#include "../Tarkov/GameWorld/Loot/Loot.h"

#define PI 3.141592653589793

typedef struct
{
	DWORD R;
	DWORD G;
	DWORD B;
	DWORD A;
}RGBA;

class Color
{
public:
	RGBA red = { 255,0,0,255 };
	RGBA Magenta = { 255,0,255,255 };
	RGBA yellow = { 255,255,0,255 };
	RGBA grayblue = { 128,128,255,255 };
	RGBA green = { 128,224,0,255 };
	RGBA darkgreen = { 0,224,128,255 };
	RGBA brown = { 192,96,0,255 };
	RGBA pink = { 255,168,255,255 };
	RGBA DarkYellow = { 216,216,0,255 };
	RGBA SilverWhite = { 236,236,236,255 };
	RGBA purple = { 144,0,255,255 };
	RGBA Navy = { 88,48,224,255 };
	RGBA skyblue = { 0,136,255,255 };
	RGBA graygreen = { 128,160,128,255 };
	RGBA blue = { 0,96,192,255 };
	RGBA orange = { 255,128,0,255 };
	RGBA peachred = { 255,80,128,255 };
	RGBA reds = { 255,128,192,255 };
	RGBA darkgray = { 96,96,96,255 };
	RGBA Navys = { 0,0,128,255 };
	RGBA darkgreens = { 0,128,0,255 };
	RGBA darkblue = { 0,128,128,255 };
	RGBA redbrown = { 128,0,0,255 };
	RGBA purplered = { 128,0,128,255 };
	RGBA greens = { 0,255,0,255 };
	RGBA envy = { 0,255,255,255 };
	RGBA black = { 0,0,0,255 };
	RGBA gray = { 128,128,128,255 };
	RGBA white = { 255,255,255,255 };
	RGBA blues = { 30,144,255,255 };
	RGBA lightblue = { 135,206,250,160 };
	RGBA Scarlet = { 220, 20, 60, 160 };
	RGBA white_ = { 255,255,255,200 };
	RGBA gray_ = { 128,128,128,200 };
	RGBA black_ = { 0,0,0,200 };
	RGBA red_ = { 255,0,0,200 };
	RGBA Magenta_ = { 255,0,255,200 };
	RGBA yellow_ = { 255,255,0,200 };
	RGBA grayblue_ = { 128,128,255,200 };
	RGBA green_ = { 128,224,0,200 };
	RGBA darkgreen_ = { 0,224,128,200 };
	RGBA brown_ = { 192,96,0,200 };
	RGBA pink_ = { 255,168,255,200 };
	RGBA darkyellow_ = { 216,216,0,200 };
	RGBA silverwhite_ = { 236,236,236,200 };
	RGBA purple_ = { 144,0,255,200 };
	RGBA Blue_ = { 88,48,224,255 };
	RGBA skyblue_ = { 0,136,255,200 };
	RGBA graygreen_ = { 128,160,128,200 };
	RGBA blue_ = { 0,96,192,200 };
	RGBA orange_ = { 255,128,0,200 };
	RGBA pinks_ = { 255,80,128,200 };
	RGBA Fuhong_ = { 255,128,192,200 };
	RGBA darkgray_ = { 96,96,96,200 };
	RGBA Navy_ = { 0,0,128,200 };
	RGBA darkgreens_ = { 0,128,0,200 };
	RGBA darkblue_ = { 0,128,128,200 };
	RGBA redbrown_ = { 128,0,0,200 };
	RGBA purplered_ = { 128,0,128,200 };
	RGBA greens_ = { 0,255,0,200 };
	RGBA envy_ = { 0,255,255,200 };

	RGBA glassblack = { 0, 0, 0, 160 };
	RGBA GlassBlue = { 65,105,225,80 };
	RGBA glassyellow = { 255,255,0,160 };
	RGBA glass = { 200,200,200,60 };

	RGBA filled = { 0, 0, 0, 150 };

	RGBA Plum = { 221,160,221,160 };



};
Color Col;

void DrawLine(float x1, float y1, float x2, float y2, glm::vec4 color, float thickness)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), ImColor(color.x, color.y, color.z, color.w), thickness);
}

void DrawCircleFilled(float x, float y, float radius, ImVec4 color)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddCircleFilled(ImVec2(x, y), radius, ImColor(color.x, color.y, color.z, color.w));
	draw_list->AddCircle(ImVec2(x, y), radius, ImColor(0.f, 0.f, 0.f, 1.f), 20, 1.f);
}

ImVec2 MeasureRadarText(ImFont* font, float fontSize, const char* text)
{
	if (font == nullptr || text == nullptr || text[0] == '\0')
		return ImVec2(0.0f, 0.0f);

	return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
}

float ScaleRadarTextSize(float fontSize)
{
	return fontSize * std::clamp(radarGlobals::textScale, 0.75f, 2.0f);
}

float DrawCenteredRadarText(ImDrawList* drawList, ImFont* font, float fontSize, float centerX, float topY, ImU32 color, const char* text)
{
	const ImVec2 textSize = MeasureRadarText(font, fontSize, text);
	if (textSize.x <= 0.0f || textSize.y <= 0.0f)
		return 0.0f;

	drawList->AddText(
		font,
		fontSize,
		ImVec2(centerX - (textSize.x * 0.5f), topY),
		color,
		text);

	return textSize.y;
}

void DrawCenteredSquareMarker(float centerX, float centerY, float halfSize, ImU32 color)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 markerMin(centerX - halfSize, centerY - halfSize);
	const ImVec2 markerMax(centerX + halfSize, centerY + halfSize);

	drawList->AddRectFilled(markerMin, markerMax, color, 1.0f);
	drawList->AddRect(markerMin, markerMax, IM_COL32(0, 0, 0, 255), 1.0f, 0, 1.0f);
}

void DrawRect(int x, int y, int w, int h, RGBA* color, int thickness)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::ColorConvertFloat4ToU32(ImVec4(color->R / 255.0, color->G / 255.0, color->B / 255.0, color->A / 255.0)), 0, 0, thickness);
}

void DrawFilledRect(int x, int y, int w, int h, ImVec4 color)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImColor(color.x, color.y, color.z, color.w), 0, 0);
}

ImVec4 GetRadarHealthColor(int healthStatus)
{
	if (healthStatus == 8192)
		return ImVec4(1.0f, 0.12f, 0.08f, 1.0f);

	if (healthStatus == 2048 || healthStatus == 4096)
		return ImVec4(1.0f, 0.86f, 0.0f, 1.0f);

	return ImVec4(0.15f, 1.0f, 0.20f, 1.0f);
}

void DrawRadarHealthDot(float centerX, float centerY, int healthStatus)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec4 healthColor = GetRadarHealthColor(healthStatus);
	const ImVec2 center(centerX, centerY);

	drawList->AddCircleFilled(center, 3.5f, IM_COL32(0, 0, 0, 255), 16);
	drawList->AddCircleFilled(center, 2.5f, ImColor(healthColor), 16);
}

constexpr float kRadarPlayerTriangleRadius = 9.0f;

glm::vec2 GetRadarFacingDirection(const glm::vec2& rotation)
{
	const float radians = static_cast<float>((PI / 180.0) * rotation.x);
	return glm::vec2(glm::cos(radians), glm::sin(radians));
}

glm::vec2 GetRadarFacingPoint(const glm::vec2& point, const glm::vec2& rotation, float distance)
{
	return point + (GetRadarFacingDirection(rotation) * distance);
}

glm::vec4 GetRadarPlayerMarkerColour(const Player& player)
{
	if (!mainGame.localGroupId.empty() && player.groupId == mainGame.localGroupId)
		return coloursGlobals::playerFriendly;

	return player.colour;
}

void DrawRadarDirectionalTriangle(float centerX, float centerY, const glm::vec2& rotation, ImU32 color)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const glm::vec2 centre(centerX, centerY);
	const glm::vec2 forward = GetRadarFacingDirection(rotation);
	const glm::vec2 sideways(-forward.y, forward.x);
	const glm::vec2 tip = centre + (forward * kRadarPlayerTriangleRadius);
	const glm::vec2 rear = centre - (forward * (kRadarPlayerTriangleRadius * 0.72f));
	const glm::vec2 left = rear + (sideways * (kRadarPlayerTriangleRadius * 0.72f));
	const glm::vec2 right = rear - (sideways * (kRadarPlayerTriangleRadius * 0.72f));

	drawList->AddTriangleFilled(
		ImVec2(tip.x, tip.y),
		ImVec2(left.x, left.y),
		ImVec2(right.x, right.y),
		color);
	drawList->AddTriangle(
		ImVec2(tip.x, tip.y),
		ImVec2(left.x, left.y),
		ImVec2(right.x, right.y),
		IM_COL32(0, 0, 0, 235),
		1.25f);
}

void DrawRadarMarkerText(
	ImDrawList* drawList,
	ImFont* font,
	float fontSize,
	const ImVec2& position,
	ImU32 color,
	const char* text)
{
	if (text == nullptr || text[0] == '\0')
		return;

	drawList->AddText(
		font,
		fontSize,
		ImVec2(position.x + 1.0f, position.y + 1.0f),
		IM_COL32(0, 0, 0, 235),
		text);
	drawList->AddText(font, fontSize, position, color, text);
}

void DrawRadarBtrMarker(
	float centerX,
	float centerY,
	const glm::vec2& rotation,
	const glm::vec4& colour,
	const std::vector<glm::vec4>& passengerColours,
	float zoomLevel)
{
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();
	const ImU32 markerColour = ImColor(colour.x, colour.y, colour.z, colour.w);
	const glm::vec2 centre(centerX, centerY);
	const glm::vec2 forward = GetRadarFacingDirection(rotation);
	const glm::vec2 sideways(-forward.y, forward.x);

	const auto point = [&centre, &forward, &sideways](float along, float across)
		{
			const glm::vec2 value = centre + (forward * along) + (sideways * across);
			return ImVec2(value.x, value.y);
		};

	const ImVec2 bodyPoints[] =
	{
		point(13.0f, 0.0f),
		point(8.0f, 6.0f),
		point(-8.0f, 6.0f),
		point(-12.0f, 3.0f),
		point(-12.0f, -3.0f),
		point(-8.0f, -6.0f),
		point(8.0f, -6.0f)
	};

	drawList->AddConvexPolyFilled(
		bodyPoints,
		IM_ARRAYSIZE(bodyPoints),
		IM_COL32(12, 15, 17, 225));
	drawList->AddPolyline(
		bodyPoints,
		IM_ARRAYSIZE(bodyPoints),
		markerColour,
		ImDrawFlags_Closed,
		1.5f);

	// Slim track rails make the marker read as a vehicle without making it bulky.
	for (const float side : { -8.0f, 8.0f })
	{
		const ImVec2 trackStart = point(-8.0f, side);
		const ImVec2 trackEnd = point(7.0f, side);
		drawList->AddLine(trackStart, trackEnd, IM_COL32(0, 0, 0, 235), 3.0f);
		drawList->AddLine(trackStart, trackEnd, markerColour, 1.25f);
	}

	const float labelFontSize =
		ScaleRadarTextSize(std::clamp(21.0f / zoomLevel, 10.0f, 12.0f));
	const ImVec2 labelSize = MeasureRadarText(font, labelFontSize, "BTR");
	const float labelX = centerX + 18.0f;
	const float labelY = passengerColours.empty()
		? centerY - (labelSize.y * 0.5f)
		: centerY - labelSize.y + 1.0f;

	DrawRadarMarkerText(
		drawList,
		font,
		labelFontSize,
		ImVec2(labelX, labelY),
		markerColour,
		"BTR");

	const size_t passengerCount = std::min<size_t>(4, passengerColours.size());
	const float passengerY = centerY + 7.0f;
	for (size_t index = 0; index < passengerCount; ++index)
	{
		const glm::vec4& passengerColour = passengerColours[index];
		const ImVec2 passengerPosition(
			labelX + 2.5f + (static_cast<float>(index) * 7.0f),
			passengerY);
		drawList->AddCircleFilled(passengerPosition, 3.5f, IM_COL32(0, 0, 0, 245), 12);
		drawList->AddCircleFilled(
			passengerPosition,
			2.5f,
			ImColor(
				passengerColour.x,
				passengerColour.y,
				passengerColour.z,
				passengerColour.w),
			12);
	}
}

void drawAimLine(
	glm::vec2 point,
	glm::vec2 rotation,
	int aimLineLength,
	glm::vec4 color,
	float startOffset = 0.0f)
{
	const glm::vec2 direction = GetRadarFacingDirection(rotation);
	const glm::vec2 startPoint = point + (direction * startOffset);

	glm::vec2 endPoint = {
		startPoint.x + (direction.x * aimLineLength),
		startPoint.y + (direction.y * aimLineLength)
	};

	DrawLine(startPoint.x, startPoint.y, endPoint.x, endPoint.y, color, 3);

}

std::string FormatShortValue(int value)
{
	char buffer[32];

	if (value >= 1000000)
		snprintf(buffer, sizeof(buffer), "%.1fm", value / 1000000.0f);
	else if (value >= 1000)
		snprintf(buffer, sizeof(buffer), "%dk", (value + 500) / 1000);
	else
		snprintf(buffer, sizeof(buffer), "%d", value);

	return std::string(buffer);
}

std::string GetRadarHeightIndicator(float worldHeight)
{
	const float heightDifference = worldHeight - mainGame.localLocation.y;

	if (heightDifference > 8.0f)
		return ICON_FK_ANGLE_DOUBLE_UP;
	if (heightDifference >= 4.0f)
		return ICON_FK_ANGLE_UP;
	if (heightDifference < -8.0f)
		return ICON_FK_ANGLE_DOUBLE_DOWN;
	if (heightDifference <= -4.0f)
		return ICON_FK_ANGLE_DOWN;

	return {};
}

struct RadarPlayerPanelState
{
	uint64_t selectedPlayerInstance = 0;
	ImVec2 position{};
	ImVec2 size = ImVec2(260.0f, 280.0f);
	bool visible = false;
	bool panelHoveredLastFrame = false;
	bool markerClickedThisFrame = false;
};

RadarPlayerPanelState& GetRadarPlayerPanelState()
{
	static RadarPlayerPanelState state;
	return state;
}

void BeginRadarPlayerPanelFrame()
{
	GetRadarPlayerPanelState().markerClickedThisFrame = false;
}

std::string GetRadarPlayerDisplayName(const Player& player)
{
	if (player.isBlackDivision)
		return "Black Division";

	if (!player.name.empty() && player.name != "Ai")
		return player.name;

	if (player.isBoss)
		return "Boss";

	if (player.isPlayerScav)
		return "PScav";

	if (player.isPlayer && !player.isAi)
		return "PMC";

	return "AI";
}

std::string GetRadarPlayerTypeLabel(const Player& player)
{
	if (player.isBlackDivision)
		return "BLACK DIVISION";

	if (player.isCultist)
		return "CULTIST";

	if (player.isBoss)
		return "BOSS";

	if (player.isPlayerScav)
		return "PLAYER SCAV";

	if (player.type == PlayerType::AIRaider)
		return "RAIDER";

	if (player.playerSide == EPlayerSide::Usec)
		return player.isAi ? "USEC RAIDER" : "USEC";

	if (player.playerSide == EPlayerSide::Bear)
		return player.isAi ? "BEAR RAIDER" : "BEAR";

	return player.isAi ? "AI" : "PLAYER";
}

std::string GetRadarEquipmentSlotLabel(const std::string& rawSlotName)
{
	const std::string slotName = TrimEFT(rawSlotName);

	if (slotName == "FirstPrimaryWeapon")
		return "PRIMARY";
	if (slotName == "SecondPrimaryWeapon")
		return "SECONDARY";
	if (slotName == "Holster")
		return "HOLSTER";
	if (slotName == "Scabbard")
		return "MELEE";
	if (slotName == "Headwear")
		return "HELMET";
	if (slotName == "Earpiece")
		return "HEADSET";
	if (slotName == "ArmorVest")
		return "ARMOUR";
	if (slotName == "TacticalVest")
		return "RIG";
	if (slotName == "Backpack")
		return "BACKPACK";
	if (slotName == "FaceCover")
		return "FACE";
	if (slotName == "Eyewear")
		return "EYEWEAR";
	if (slotName == "ArmBand")
		return "ARMBAND";

	return slotName.empty() ? "EQUIPMENT" : slotName;
}

bool IsRadarPlayerEquipmentSlotVisible(const Player& player, const slots& slot)
{
	if (slot.equipName.empty())
		return false;

	const std::string slotName = TrimEFT(slot.name);
	if (slotName == "SecuredContainer" || slotName == "Dogtag")
		return false;

	return !(player.isPlayer && slotName == "Scabbard");
}

bool IsGenericRadarAiScav(const Player& player)
{
	if (!player.isAi || player.isPlayer || player.isPlayerScav ||
		player.isBoss || player.isBlackDivision || player.isCultist)
	{
		return false;
	}

	return player.type == PlayerType::AIScav ||
		player.name.empty() ||
		player.name == "Ai" ||
		player.name == "AI" ||
		player.name == "Scav";
}

bool HasWantedRadarPlayerEquipment(const Player& player)
{
	return std::any_of(
		player._slots.begin(),
		player._slots.end(),
		[&player](const slots& slot)
		{
			return slot.wanted && IsRadarPlayerEquipmentSlotVisible(player, slot);
		});
}

std::string GetRadarHeldItemLabel(const Player& player)
{
	const std::string itemName = TrimEFT(player.observedHandsInfo.itemName);
	if (itemName.empty())
		return {};

	const std::string ammoName = TrimEFT(player.observedHandsInfo.ammoName);
	if (ammoName.empty())
		return itemName;

	return itemName + "  " + ammoName + " / " +
		std::to_string(std::max(0, player.observedHandsInfo.magazineCount));
}

void PositionRadarPopupNearMouse(ImVec2& position, const ImVec2& size)
{
	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 workMinimum = viewport->WorkPos;
	const ImVec2 workMaximum(
		viewport->WorkPos.x + viewport->WorkSize.x,
		viewport->WorkPos.y + viewport->WorkSize.y);
	const float panelWidth = size.x > 0.0f ? size.x : 380.0f;
	const float panelHeight = size.y > 0.0f ? size.y : 520.0f;

	position = ImVec2(mousePosition.x + 18.0f, mousePosition.y + 18.0f);
	if (position.x + panelWidth > workMaximum.x - 8.0f)
		position.x = mousePosition.x - panelWidth - 18.0f;
	if (position.y + panelHeight > workMaximum.y - 8.0f)
		position.y = mousePosition.y - panelHeight - 18.0f;

	position.x = std::clamp(position.x, workMinimum.x + 8.0f, std::max(workMinimum.x + 8.0f, workMaximum.x - panelWidth - 8.0f));
	position.y = std::clamp(position.y, workMinimum.y + 8.0f, std::max(workMinimum.y + 8.0f, workMaximum.y - panelHeight - 8.0f));
}

void HandlePlayerSlotClick(float x, float y, float radius, const Player& player)
{
	RadarPlayerPanelState& state = GetRadarPlayerPanelState();
	if (state.markerClickedThisFrame ||
		state.panelHoveredLastFrame ||
		!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
	{
		return;
	}

	const float clickRadius = radius + 16.0f;
	const ImRect markerBounds(
		ImVec2(x - clickRadius, y - clickRadius),
		ImVec2(x + clickRadius, y + clickRadius));

	if (!markerBounds.Contains(ImGui::GetIO().MousePos) ||
		!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		return;
	}

	state.markerClickedThisFrame = true;
	if (state.visible && state.selectedPlayerInstance == player.instance)
	{
		state.visible = false;
		state.selectedPlayerInstance = 0;
		return;
	}

	state.visible = true;
	state.selectedPlayerInstance = player.instance;

	PositionRadarPopupNearMouse(state.position, state.size);
}

void DrawRadarPlayerLoadoutPanel(const PlayerCollection& players)
{
	RadarPlayerPanelState& state = GetRadarPlayerPanelState();
	if (!state.visible)
	{
		state.panelHoveredLastFrame = false;
		return;
	}

	const auto selectedPlayer = std::find_if(
		players.begin(),
		players.end(),
		[&state](const Player& player)
		{
			return player.instance == state.selectedPlayerInstance &&
				!player.isLocal &&
				!player.isDead &&
				!player.hasExfiled;
		});

	if (selectedPlayer == players.end())
	{
		state.visible = false;
		state.selectedPlayerInstance = 0;
		state.panelHoveredLastFrame = false;
		return;
	}

	std::vector<const slots*> visibleSlots;
	visibleSlots.reserve(selectedPlayer->_slots.size());
	for (const slots& slot : selectedPlayer->_slots)
	{
		if (IsRadarPlayerEquipmentSlotVisible(*selectedPlayer, slot))
			visibleSlots.push_back(&slot);
	}

	const ImVec4 accentColour(0.88f, 0.30f, 0.32f, 1.00f);
	const ImVec4 labelColour(0.54f, 0.58f, 0.62f, 1.00f);
	const ImVec4 primaryColour(0.94f, 0.95f, 0.96f, 1.00f);
	const ImVec4 valueColour(0.96f, 0.78f, 0.42f, 1.00f);
	const ImVec4 ammoColour(0.46f, 0.84f, 0.62f, 1.00f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.052f, 0.060f, 0.97f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.27f, 0.30f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.88f, 0.30f, 0.32f, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.88f, 0.30f, 0.32f, 0.13f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.88f, 0.30f, 0.32f, 0.18f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.88f, 0.30f, 0.32f, 0.22f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

	ImGui::SetNextWindowPos(state.position, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(ImVec2(245.0f, 0.0f), ImVec2(290.0f, 360.0f));
	const ImGuiWindowFlags panelFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_AlwaysAutoResize;

	bool panelHovered = false;
	if (ImGui::Begin("##radar_player_loadout_panel", nullptr, panelFlags))
	{
		ImGui::TextColored(accentColour, ICON_FA_USER "  LOADOUT");

		const int distance = selectedPlayer->distance > 0
			? selectedPlayer->distance
			: static_cast<int>(glm::distance(mainGame.localLocation, selectedPlayer->location));
		const std::string distanceText = std::to_string(distance) + " m";
		const float distanceX =
			ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(distanceText.c_str()).x;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), distanceX));
		ImGui::TextColored(labelColour, "%s", distanceText.c_str());

		ImGui::Separator();
		if (ImGui::BeginTable(
			"##player_summary",
			2,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 58.0f);
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			const std::string playerName = GetRadarPlayerDisplayName(*selectedPlayer);
			ImGui::TextColored(primaryColour, "%s", playerName.c_str());
			std::string playerMeta = GetRadarPlayerTypeLabel(*selectedPlayer);
			if (selectedPlayer->DT_lvl > 0)
				playerMeta += "  |  LEVEL " + std::to_string(selectedPlayer->DT_lvl);
			if (selectedPlayer->isFriend)
				playerMeta += "  |  FRIEND";
			else if (!mainGame.localGroupId.empty() && selectedPlayer->groupId == mainGame.localGroupId)
				playerMeta += "  |  GROUP";
			ImGui::TextColored(labelColour, "%s", playerMeta.c_str());

			ImGui::TableSetColumnIndex(1);
			const char* valueLabel = "VALUE";
			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() +
				std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(valueLabel).x));
			ImGui::TextColored(labelColour, "%s", valueLabel);
			const std::string totalValue = selectedPlayer->playerValue > 0
				? FormatShortValue(selectedPlayer->playerValue)
				: "-";
			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() +
				std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(totalValue.c_str()).x));
			ImGui::TextColored(valueColour, "%s", totalValue.c_str());

			ImGui::EndTable();
		}

		const std::string heldItem = GetRadarHeldItemLabel(*selectedPlayer);
		if (!heldItem.empty())
		{
			ImGui::TextColored(labelColour, "IN HAND");
			ImGui::SameLine(0.0f, 7.0f);
			ImGui::TextColored(ammoColour, "%s", heldItem.c_str());
		}

		ImGui::Separator();
		ImGui::TextColored(labelColour, "EQUIPMENT");
		const std::string slotCountText =
			std::to_string(visibleSlots.size()) +
			(visibleSlots.size() == 1 ? " SLOT" : " SLOTS");
		const float slotCountX =
			ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(slotCountText.c_str()).x;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), slotCountX));
		ImGui::TextColored(labelColour, "%s", slotCountText.c_str());

		if (visibleSlots.empty())
		{
			ImGui::TextColored(labelColour, "Equipment data not available yet");
		}
		else
		{
			const float rowHeight = ImGui::GetTextLineHeight() + 4.0f;
			const float requestedHeight = static_cast<float>(visibleSlots.size()) * rowHeight;
			const float listHeight = std::min(190.0f, requestedHeight);

			ImGui::BeginChild(
				"##player_equipment_rows",
				ImVec2(0.0f, listHeight),
				false,
				requestedHeight > listHeight ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_None);

			if (ImGui::BeginTable(
				"##player_equipment_table",
				3,
				ImGuiTableFlags_SizingStretchProp |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_NoSavedSettings))
			{
				ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 62.0f);
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 44.0f);

				for (const slots* slot : visibleSlots)
				{
					ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
					ImGui::TableSetColumnIndex(0);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
					const std::string slotLabel = GetRadarEquipmentSlotLabel(slot->name);
					ImGui::TextColored(labelColour, "%s", slotLabel.c_str());

					ImGui::TableSetColumnIndex(1);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
					if (slot->wanted)
					{
						ImGui::TextColored(valueColour, "*");
						ImGui::SameLine(0.0f, 4.0f);
					}
					ImGui::TextColored(primaryColour, "%s", slot->equipName.c_str());

					ImGui::TableSetColumnIndex(2);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
					const std::string priceText = slot->price > 0 ? FormatShortValue(slot->price) : "-";
					ImGui::SetCursorPosX(
						ImGui::GetCursorPosX() +
						std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(priceText.c_str()).x));
					ImGui::TextColored(valueColour, "%s", priceText.c_str());
				}

				ImGui::EndTable();
			}

			ImGui::EndChild();
		}

		state.size = ImGui::GetWindowSize();
		panelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
			ImGui::IsAnyItemHovered();
	}
	ImGui::End();

	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(6);

	state.panelHoveredLastFrame = panelHovered;
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!state.markerClickedThisFrame &&
		!panelHovered)
	{
		state.visible = false;
		state.selectedPlayerInstance = 0;
	}
}

void DrawRadarPlayerMarkers(float x, float y, float zoomLevel, const Player& player)
{
	const float markerFontSize = std::clamp(30.f / zoomLevel, 7.f, 9.f);
	const float labelFontSize = ScaleRadarTextSize(markerFontSize + 8.0f);
	const float metaFontSize = std::max(8.0f, labelFontSize * 0.76f);
	const float itemFontSize = std::max(8.0f, labelFontSize * 0.78f);
	const float heightIconFontSize = std::max(7.0f, labelFontSize * 0.56f);
	constexpr float labelGap = 7.0f;
	constexpr float lineGap = 1.0f;

	const float height = player.location.y;
	std::string hString;
	std::string hStringVal;

	if (height > (mainGame.localLocation.y + 2.f)) // 2.f per level?!
	{
		if (height > (mainGame.localLocation.y + 4.f))
			hString = ICON_FK_ANGLE_DOUBLE_UP;
		else
			hString = ICON_FK_ANGLE_UP;
	}
	if (height < (mainGame.localLocation.y - 2.f))
	{
		if (height < (mainGame.localLocation.y - 4.f))
			hString = ICON_FK_ANGLE_DOUBLE_DOWN;
		else
			hString = ICON_FK_ANGLE_DOWN;
	}
	if (height != mainGame.localLocation.y)
	{
		const int correctedNumber = static_cast<int>(height - mainGame.localLocation.y);
		hStringVal = std::to_string(correctedNumber);
	}

	const bool genericAiScav = IsGenericRadarAiScav(player);
	const bool hasWantedEquipment =
		radarGlobals::getPlayerEquip &&
		radarGlobals::drawPlayerEquip &&
		HasWantedRadarPlayerEquipment(player);
	const std::string itemInHand = GetRadarHeldItemLabel(player);
	const bool showHeldItem =
		radarGlobals::drawHandItem &&
		!player.isBTR &&
		!itemInHand.empty();
	const glm::vec4 color = GetRadarPlayerMarkerColour(player);

	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();
	const ImU32 drawColor = ImColor(color.x, color.y, color.z, color.w);
	const ImU32 primaryTextColor = IM_COL32(238, 241, 244, 255);
	const ImU32 metaTextColor = IM_COL32(165, 173, 181, 255);
	const ImU32 itemTextColor = IM_COL32(202, 208, 213, 255);
	const ImU32 wantedColor = IM_COL32(245, 190, 76, 255);

	if (!player.isDead)
	{
		// Vehicles retain their circular marker; live players use the facing triangle.
		if (player.isBTR)
			DrawCircleFilled(x, y, kRadarPlayerTriangleRadius, ImColor(color.x, color.y, color.z, color.w));
		else
			DrawRadarDirectionalTriangle(x, y, player.rotation, drawColor);

		if (player.isInBTR)
			return;

		if (!player.isBTR)
			DrawRadarHealthDot(
				x - kRadarPlayerTriangleRadius - 1.0f,
				y + kRadarPlayerTriangleRadius - 1.0f,
				player.healthETAG);

		HandlePlayerSlotClick(x, y, kRadarPlayerTriangleRadius, player);

		if (radarGlobals::minimalView)
			return;

		// Height is useful positional information for every player type, including
		// anonymous AI scavs.
		const float heightRightX = x - kRadarPlayerTriangleRadius - 5.0f;
		const ImVec2 heightIconSize = MeasureRadarText(font, heightIconFontSize, hString.c_str());
		DrawRadarMarkerText(
			draw_list,
			font,
			heightIconFontSize,
			ImVec2(heightRightX - heightIconSize.x, y - (heightIconSize.y * 0.5f)),
			drawColor,
			hString.c_str());

		if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
		{
			const ImVec2 heightValueSize = MeasureRadarText(font, metaFontSize, hStringVal.c_str());
			DrawRadarMarkerText(
				draw_list,
				font,
				metaFontSize,
				ImVec2(heightRightX - heightValueSize.x, y - heightIconSize.y - heightValueSize.y),
				drawColor,
				hStringVal.c_str());
		}

		const float textX = x + kRadarPlayerTriangleRadius + labelGap;
		if (genericAiScav)
		{
			if (showHeldItem)
			{
				const ImVec2 itemSize = MeasureRadarText(font, itemFontSize, itemInHand.c_str());
				const float itemY = y - (itemSize.y * 0.5f);
				DrawRadarMarkerText(
					draw_list,
					font,
					itemFontSize,
					ImVec2(textX, itemY),
					itemTextColor,
					itemInHand.c_str());

				if (hasWantedEquipment)
				{
					DrawRadarMarkerText(
						draw_list,
						font,
						labelFontSize,
						ImVec2(textX + itemSize.x + 4.0f, itemY - 1.0f),
						wantedColor,
						"*");
				}
			}
			else if (hasWantedEquipment)
			{
				const ImVec2 wantedSize = MeasureRadarText(font, labelFontSize, "*");
				DrawRadarMarkerText(
					draw_list,
					font,
					labelFontSize,
					ImVec2(textX, y - (wantedSize.y * 0.5f)),
					wantedColor,
					"*");
			}

			return;
		}

		const std::string displayName = GetRadarPlayerDisplayName(player);
		std::string metaText = GetRadarPlayerTypeLabel(player);

		const ImVec2 nameSize = MeasureRadarText(font, labelFontSize, displayName.c_str());
		const ImVec2 metaSize = MeasureRadarText(font, metaFontSize, metaText.c_str());
		const ImVec2 itemSize = showHeldItem
			? MeasureRadarText(font, itemFontSize, itemInHand.c_str())
			: ImVec2(0.0f, 0.0f);
		const float totalTextHeight =
			nameSize.y + lineGap + metaSize.y +
			(showHeldItem ? lineGap + itemSize.y : 0.0f);
		float nextTextY = y - (totalTextHeight * 0.5f);

		DrawRadarMarkerText(draw_list, font, labelFontSize, ImVec2(textX, nextTextY), primaryTextColor, displayName.c_str());
		if (hasWantedEquipment)
		{
			DrawRadarMarkerText(
				draw_list,
				font,
				labelFontSize,
				ImVec2(textX + nameSize.x + 4.0f, nextTextY),
				wantedColor,
				"*");
		}

		nextTextY += nameSize.y + lineGap;
		DrawRadarMarkerText(draw_list, font, metaFontSize, ImVec2(textX, nextTextY), metaTextColor, metaText.c_str());

		if (showHeldItem)
		{
			nextTextY += metaSize.y + lineGap;
			DrawRadarMarkerText(draw_list, font, itemFontSize, ImVec2(textX, nextTextY), itemTextColor, itemInHand.c_str());
		}
	}
}

void DrawRadarCorpseTooltip(const LootEntity& lootList)
{
	const CorpseLootState& corpseState = lootList.getCorpseState();
	const std::string valueText = FormatShortValue(lootList.getCorpseValue());

	std::vector<const CorpseEquipment*> lootableEquipment;
	lootableEquipment.reserve(corpseState.equipment.size());

	for (const auto& slot : corpseState.equipment)
	{
		if (corpseState.isEquipmentLootable(slot))
			lootableEquipment.push_back(&slot);
	}

	const std::string resolvedOwnerName = GetCorpseOwnerLabel(lootList);
	const std::string ownerName = resolvedOwnerName.empty()
		? "Corpse"
		: resolvedOwnerName;

	const ImVec4 accentColour(0.88f, 0.30f, 0.32f, 1.00f);
	const ImVec4 labelColour(0.54f, 0.58f, 0.62f, 1.00f);
	const ImVec4 primaryColour(0.94f, 0.95f, 0.96f, 1.00f);
	const ImVec4 valueColour(0.96f, 0.78f, 0.42f, 1.00f);

	ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.045f, 0.052f, 0.060f, 0.97f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.27f, 0.30f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.88f, 0.30f, 0.32f, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.00f, 1.00f, 1.00f, 0.025f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.0f, 5.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 3.0f));

	ImGui::SetNextWindowSizeConstraints(ImVec2(235.0f, 0.0f), ImVec2(310.0f, 360.0f));
	if (ImGui::BeginTooltip())
	{
		ImGui::TextColored(accentColour, ICON_FA_CUBE "  CORPSE LOOT");

		const std::string itemCountText =
			std::to_string(lootableEquipment.size()) +
			(lootableEquipment.size() == 1 ? " item" : " items");
		const float itemCountX =
			ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(itemCountText.c_str()).x;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), itemCountX));
		ImGui::TextColored(labelColour, "%s", itemCountText.c_str());

		if (ImGui::BeginTable(
			"##corpse_summary",
			2,
			ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
		{
			ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed);

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(labelColour, "OWNER");
			ImGui::TableSetColumnIndex(1);
			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() +
				std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("EST. VALUE").x));
			ImGui::TextColored(labelColour, "EST. VALUE");

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(primaryColour, "%s", ownerName.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::SetCursorPosX(
				ImGui::GetCursorPosX() +
				std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(valueText.c_str()).x));
			ImGui::TextColored(valueColour, "%s", valueText.c_str());

			ImGui::EndTable();
		}

		if (!lootableEquipment.empty())
		{
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::BeginTable(
				"##corpse_equipment",
				2,
				ImGuiTableFlags_SizingStretchProp |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_NoSavedSettings,
				ImVec2(0.0f, 0.0f)))
			{
				ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed);

				for (const CorpseEquipment* slot : lootableEquipment)
				{
					ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetTextLineHeight() + 4.0f);
					ImGui::TableSetColumnIndex(0);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
					if (slot->wanted)
					{
						ImGui::TextColored(accentColour, ICON_FA_STAR);
						ImGui::SameLine(0.0f, 4.0f);
					}
					ImGui::TextColored(primaryColour, "%s", slot->name.c_str());

					ImGui::TableSetColumnIndex(1);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
					const std::string itemValue = FormatShortValue(slot->value);
					ImGui::SetCursorPosX(
						ImGui::GetCursorPosX() +
						std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(itemValue.c_str()).x));
					ImGui::TextColored(valueColour, "%s", itemValue.c_str());
				}

				ImGui::EndTable();
			}
		}

		ImGui::EndTooltip();
	}

	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(4);
}

struct RadarCorpseHoverCandidate
{
	const LootEntity* corpse = nullptr;
	float distanceSquared = FLT_MAX;
};

uint64_t GetRadarCorpseUiId(const LootEntity& corpse)
{
	return corpse.instance != 0
		? corpse.instance
		: corpse.m_interactiveClass;
}

void DrawRadarCorpseHoverPanel(
	const std::vector<RadarCorpseHoverCandidate>& hoveredCorpses,
	const std::vector<const LootEntity*>& visibleCorpses)
{
	struct HoverPanelState
	{
		std::vector<LootEntity> corpses;
		uint64_t expandedCorpseId = 0;
		ImVec2 position{};
		ImVec2 size = ImVec2(250.0f, 220.0f);
		bool visible = false;
		bool panelHoveredLastFrame = false;
	};

	static HoverPanelState state;
	const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool markerClicked =
		leftClicked &&
		!state.panelHoveredLastFrame &&
		!hoveredCorpses.empty();

	if (markerClicked)
	{
		state.corpses.clear();
		state.corpses.reserve(hoveredCorpses.size());

		for (const RadarCorpseHoverCandidate& candidate : hoveredCorpses)
			state.corpses.push_back(*candidate.corpse);

		const auto nearestCorpse = std::min_element(
			hoveredCorpses.begin(),
			hoveredCorpses.end(),
			[](const RadarCorpseHoverCandidate& left, const RadarCorpseHoverCandidate& right)
			{
				return left.distanceSquared < right.distanceSquared;
			});

		state.expandedCorpseId = GetRadarCorpseUiId(*nearestCorpse->corpse);

		const ImVec2 mousePosition = ImGui::GetIO().MousePos;
		const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		const float panelWidth = state.size.x > 0.0f ? state.size.x : 285.0f;
		const float panelHeight = state.size.y > 0.0f ? state.size.y : 250.0f;

		state.position = ImVec2(mousePosition.x + 18.0f, mousePosition.y + 18.0f);
		if (state.position.x + panelWidth > displaySize.x - 8.0f)
			state.position.x = mousePosition.x - panelWidth - 18.0f;
		if (state.position.y + panelHeight > displaySize.y - 8.0f)
			state.position.y = mousePosition.y - panelHeight - 18.0f;

		state.position.x = std::max(8.0f, state.position.x);
		state.position.y = std::max(8.0f, state.position.y);
		state.visible = true;
	}

	if (!state.visible)
	{
		state.panelHoveredLastFrame = false;
		return;
	}

	for (auto corpse = state.corpses.begin(); corpse != state.corpses.end();)
	{
		const auto liveCorpse = std::find_if(
			visibleCorpses.begin(),
			visibleCorpses.end(),
			[corpse](const LootEntity* candidate)
			{
				return GetRadarCorpseUiId(*candidate) == GetRadarCorpseUiId(*corpse);
			});

		if (liveCorpse == visibleCorpses.end())
		{
			corpse = state.corpses.erase(corpse);
			continue;
		}

		*corpse = **liveCorpse;
		++corpse;
	}

	if (state.corpses.empty())
	{
		state.visible = false;
		state.panelHoveredLastFrame = false;
		return;
	}

	const auto expandedCorpse = std::find_if(
		state.corpses.begin(),
		state.corpses.end(),
		[](const LootEntity& corpse)
		{
			return GetRadarCorpseUiId(corpse) == state.expandedCorpseId;
		});
	if (state.expandedCorpseId != 0 && expandedCorpse == state.corpses.end())
		state.expandedCorpseId = GetRadarCorpseUiId(state.corpses.front());

	const ImVec4 accentColour(0.88f, 0.30f, 0.32f, 1.00f);
	const ImVec4 labelColour(0.54f, 0.58f, 0.62f, 1.00f);
	const ImVec4 primaryColour(0.94f, 0.95f, 0.96f, 1.00f);
	const ImVec4 valueColour(0.96f, 0.78f, 0.42f, 1.00f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.052f, 0.060f, 0.97f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.27f, 0.30f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.88f, 0.30f, 0.32f, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImVec4(1.00f, 1.00f, 1.00f, 0.025f));
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.88f, 0.30f, 0.32f, 0.13f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.88f, 0.30f, 0.32f, 0.18f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.88f, 0.30f, 0.32f, 0.22f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

	ImGui::SetNextWindowPos(state.position, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(ImVec2(235.0f, 0.0f), ImVec2(285.0f, 360.0f));
	const ImGuiWindowFlags panelFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_AlwaysAutoResize;

	if (ImGui::Begin("##radar_corpse_hover_panel", nullptr, panelFlags))
	{
		ImGui::TextColored(accentColour, ICON_FA_CUBE "  CORPSE LOOT");

		const std::string corpseCountText = state.corpses.size() == 1
			? "1 corpse"
			: std::to_string(state.corpses.size()) + " nearby";
		const float corpseCountX =
			ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(corpseCountText.c_str()).x;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), corpseCountX));
		ImGui::TextColored(labelColour, "%s", corpseCountText.c_str());

		for (const LootEntity& corpse : state.corpses)
		{
			const CorpseLootState& corpseState = corpse.getCorpseState();
			std::vector<const CorpseEquipment*> lootableEquipment;
			lootableEquipment.reserve(corpseState.equipment.size());

			for (const auto& slot : corpseState.equipment)
			{
				if (corpseState.isEquipmentLootable(slot))
					lootableEquipment.push_back(&slot);
			}

			const std::string resolvedOwnerName = GetCorpseOwnerLabel(corpse);
			const std::string ownerName = resolvedOwnerName.empty()
				? "Corpse"
				: resolvedOwnerName;
			const std::string valueText = FormatShortValue(corpse.getCorpseValue());
			const std::string itemCountText =
				std::to_string(lootableEquipment.size()) +
				(lootableEquipment.size() == 1 ? " item" : " items");
			const uint64_t corpseId = GetRadarCorpseUiId(corpse);
			const bool expanded = state.expandedCorpseId == corpseId;

			ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(corpseId)));
			const float rowHeight = ImGui::GetTextLineHeight() + 7.0f;
			const bool clicked = ImGui::Selectable(
				"##corpse_toggle",
				expanded,
				ImGuiSelectableFlags_None,
				ImVec2(ImGui::GetContentRegionAvail().x, rowHeight));
			const ImVec2 rowMinimum = ImGui::GetItemRectMin();
			const ImVec2 rowMaximum = ImGui::GetItemRectMax();
			ImDrawList* panelDrawList = ImGui::GetWindowDrawList();
			ImFont* panelFont = ImGui::GetFont();
			const float textY = rowMinimum.y + ((rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
			const char* caret = expanded ? ICON_FA_ANGLE_DOWN : ICON_FA_ANGLE_RIGHT;

			panelDrawList->AddText(
				ImVec2(rowMinimum.x + 5.0f, textY),
				ImGui::GetColorU32(expanded ? accentColour : labelColour),
				caret);

			const float ownerX = rowMinimum.x + 21.0f;
			const float valueWidth = ImGui::CalcTextSize(valueText.c_str()).x;
			const float valueX = rowMaximum.x - valueWidth - 5.0f;
			const ImVec4 ownerClip(
				ownerX,
				rowMinimum.y,
				std::max(ownerX, valueX - 7.0f),
				rowMaximum.y);
			panelDrawList->AddText(
				panelFont,
				panelFont->FontSize,
				ImVec2(ownerX, textY),
				ImGui::GetColorU32(primaryColour),
				ownerName.c_str(),
				nullptr,
				0.0f,
				&ownerClip);
			panelDrawList->AddText(
				ImVec2(valueX, textY),
				ImGui::GetColorU32(valueColour),
				valueText.c_str());

			const float itemCountFontSize = panelFont->FontSize * 0.72f;
			const ImVec2 ownerSize = panelFont->CalcTextSizeA(
				panelFont->FontSize,
				FLT_MAX,
				0.0f,
				ownerName.c_str());
			const float itemCountX = ownerX + ownerSize.x + 6.0f;
			if (itemCountX < ownerClip.z - 36.0f)
			{
				panelDrawList->AddText(
					panelFont,
					itemCountFontSize,
					ImVec2(itemCountX, textY + 3.0f),
					ImGui::GetColorU32(labelColour),
					itemCountText.c_str());
			}

			if (clicked)
			{
				state.expandedCorpseId = expanded ? 0 : corpseId;
			}

			if (expanded)
			{
				ImGui::Separator();

				if (lootableEquipment.empty())
				{
					ImGui::TextColored(labelColour, "No lootable equipment");
				}
				else if (ImGui::BeginTable(
					"##corpse_equipment",
					2,
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_RowBg |
					ImGuiTableFlags_NoSavedSettings,
					ImVec2(0.0f, 0.0f)))
				{
					ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed);

					for (const CorpseEquipment* slot : lootableEquipment)
					{
					ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetTextLineHeight() + 3.0f);
					ImGui::TableSetColumnIndex(0);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
						if (slot->wanted)
						{
							ImGui::TextColored(accentColour, ICON_FA_STAR);
						ImGui::SameLine(0.0f, 4.0f);
						}
						ImGui::TextColored(primaryColour, "%s", slot->name.c_str());

						ImGui::TableSetColumnIndex(1);
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1.0f);
						const std::string itemValue = FormatShortValue(slot->value);
						ImGui::SetCursorPosX(
							ImGui::GetCursorPosX() +
							std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(itemValue.c_str()).x));
						ImGui::TextColored(valueColour, "%s", itemValue.c_str());
					}

					ImGui::EndTable();
				}

			}

			ImGui::PopID();
		}

		state.size = ImGui::GetWindowSize();
		state.panelHoveredLastFrame = ImGui::IsWindowHovered(
			ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	}
	else
	{
		state.panelHoveredLastFrame = false;
	}
	ImGui::End();

	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(7);

	if (leftClicked && !markerClicked && !state.panelHoveredLastFrame)
		state.visible = false;
}

struct RadarLootCluster
{
	uint64_t id = 0;
	glm::vec3 worldCenter{};
	ImVec2 screenPosition{};
	std::vector<const LootEntity*> entries;
	bool popupSuppressed = false;
};

struct RadarLootClusterHoverCandidate
{
	const RadarLootCluster* cluster = nullptr;
	float distanceSquared = FLT_MAX;
};

uint64_t GetRadarLootEntityUiId(const LootEntity& loot)
{
	if (loot.instance != 0)
		return loot.instance;

	if (loot.m_interactiveClass != 0)
		return loot.m_interactiveClass;

	return loot.m_itemObject;
}

int GetRadarLootEntityValue(const LootEntity& loot)
{
	return loot.isCorpse()
		? loot.getCorpseValue()
		: static_cast<int>(GetLootDisplayPrice(loot));
}

std::string GetRadarLootClusterEntryName(const LootEntity& loot)
{
	if (loot.isCorpse())
	{
		const std::string ownerName = GetCorpseOwnerLabel(loot);
		return ownerName.empty() ? "Corpse" : ownerName;
	}

	if (!loot.shortName.empty())
		return loot.shortName;

	if (!loot.longName.empty())
		return loot.longName;

	return loot.isContainer() ? "Container" : "Loot";
}

float DrawRadarLootClusterMarker(const RadarLootCluster& cluster, float zoomLevel)
{
	if (cluster.entries.size() < 2)
		return FLT_MAX;

	const float fontSize = ScaleRadarTextSize(std::clamp(19.0f / zoomLevel, 9.0f, 11.5f));
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();
	const std::string countText = std::to_string(cluster.entries.size());
	const ImVec2 countSize = MeasureRadarText(font, fontSize, countText.c_str());
	const float iconSize = fontSize * 0.66f;
	const float horizontalPadding = 5.0f;
	const float gap = 3.0f;
	const ImVec2 markerSize(
		horizontalPadding * 2.0f + iconSize + gap + countSize.x,
		std::max(20.0f, countSize.y + 7.0f));
	const ImVec2 markerMinimum(
		cluster.screenPosition.x - markerSize.x * 0.5f,
		cluster.screenPosition.y - markerSize.y * 0.5f);
	const ImVec2 markerMaximum(
		markerMinimum.x + markerSize.x,
		markerMinimum.y + markerSize.y);

	glm::vec4 markerColour = cluster.entries.front()->color;
	const LootEntity* highestValueEntry = cluster.entries.front();
	for (const LootEntity* entry : cluster.entries)
	{
		if (GetRadarLootEntityValue(*entry) > GetRadarLootEntityValue(*highestValueEntry))
			highestValueEntry = entry;
	}

	if (highestValueEntry->isQuestItem())
		markerColour = coloursGlobals::questColour;
	else if (highestValueEntry->isCorpse())
		markerColour = coloursGlobals::playerCorpse;
	else
		markerColour = highestValueEntry->color;

	const ImU32 drawColour = ImColor(markerColour.x, markerColour.y, markerColour.z, markerColour.w);
	drawList->AddRectFilled(markerMinimum, markerMaximum, IM_COL32(12, 15, 17, 240), 3.0f);
	drawList->AddRect(markerMinimum, markerMaximum, drawColour, 3.0f, 0, 1.0f);

	const float iconLeft = markerMinimum.x + horizontalPadding;
	const float iconTop = cluster.screenPosition.y - iconSize * 0.5f;
	drawList->AddText(
		font,
		iconSize,
		ImVec2(iconLeft, iconTop),
		drawColour,
		ICON_FA_DIAMOND);
	drawList->AddText(
		font,
		fontSize,
		ImVec2(iconLeft + iconSize + gap, cluster.screenPosition.y - countSize.y * 0.5f),
		drawColour,
		countText.c_str());

	const float hitPadding = 6.0f;
	const ImRect hitBounds(
		ImVec2(markerMinimum.x - hitPadding, markerMinimum.y - hitPadding),
		ImVec2(markerMaximum.x + hitPadding, markerMaximum.y + hitPadding));
	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
		!hitBounds.Contains(ImGui::GetIO().MousePos))
	{
		return FLT_MAX;
	}

	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const float deltaX = mousePosition.x - cluster.screenPosition.x;
	const float deltaY = mousePosition.y - cluster.screenPosition.y;
	return (deltaX * deltaX) + (deltaY * deltaY);
}

void DrawRadarLootClusterPanel(
	const std::vector<RadarLootClusterHoverCandidate>& hoveredClusters,
	const std::vector<RadarLootCluster>& visibleClusters)
{
	struct LootClusterPanelState
	{
		uint64_t clusterId = 0;
		uint64_t expandedCorpseId = 0;
		std::vector<LootEntity> entries;
		glm::vec3 worldCenter{};
		ImVec2 position{};
		ImVec2 size = ImVec2(260.0f, 280.0f);
		bool visible = false;
		bool panelHoveredLastFrame = false;
	};

	static LootClusterPanelState state;
	const bool leftClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool markerClicked =
		leftClicked &&
		!state.panelHoveredLastFrame &&
		!hoveredClusters.empty();

	if (markerClicked)
	{
		const auto nearestCluster = std::min_element(
			hoveredClusters.begin(),
			hoveredClusters.end(),
			[](const RadarLootClusterHoverCandidate& left, const RadarLootClusterHoverCandidate& right)
			{
				return left.distanceSquared < right.distanceSquared;
			});
		const RadarLootCluster& cluster = *nearestCluster->cluster;

		if (cluster.popupSuppressed)
		{
			state.visible = false;
			state.clusterId = 0;
			state.expandedCorpseId = 0;
		}
		else if (state.visible && state.clusterId == cluster.id)
		{
			state.visible = false;
			state.clusterId = 0;
			state.expandedCorpseId = 0;
		}
		else
		{
			state.visible = true;
			state.clusterId = cluster.id;
			state.expandedCorpseId = 0;
			state.worldCenter = cluster.worldCenter;
			state.entries.clear();
			state.entries.reserve(cluster.entries.size());
			for (const LootEntity* entry : cluster.entries)
				state.entries.push_back(*entry);
			PositionRadarPopupNearMouse(state.position, state.size);
		}
	}

	if (!state.visible)
	{
		state.panelHoveredLastFrame = false;
		return;
	}

	const auto liveCluster = std::find_if(
		visibleClusters.begin(),
		visibleClusters.end(),
		[](const RadarLootCluster& cluster)
		{
			return cluster.id == state.clusterId;
		});

	if (liveCluster == visibleClusters.end() || liveCluster->popupSuppressed)
	{
		state.visible = false;
		state.clusterId = 0;
		state.expandedCorpseId = 0;
		state.panelHoveredLastFrame = false;
		return;
	}

	state.worldCenter = liveCluster->worldCenter;
	state.entries.clear();
	state.entries.reserve(liveCluster->entries.size());
	for (const LootEntity* entry : liveCluster->entries)
		state.entries.push_back(*entry);

	std::stable_sort(
		state.entries.begin(),
		state.entries.end(),
		[](const LootEntity& left, const LootEntity& right)
		{
			return GetRadarLootEntityValue(left) > GetRadarLootEntityValue(right);
		});

	if (state.expandedCorpseId != 0)
	{
		const bool expandedCorpseStillPresent = std::any_of(
			state.entries.begin(),
			state.entries.end(),
			[](const LootEntity& entry)
			{
				return entry.isCorpse() &&
					GetRadarLootEntityUiId(entry) == state.expandedCorpseId;
			});
		if (!expandedCorpseStillPresent)
			state.expandedCorpseId = 0;
	}

	const ImVec4 accentColour(0.88f, 0.30f, 0.32f, 1.00f);
	const ImVec4 labelColour(0.54f, 0.58f, 0.62f, 1.00f);
	const ImVec4 primaryColour(0.94f, 0.95f, 0.96f, 1.00f);
	const ImVec4 valueColour(0.96f, 0.78f, 0.42f, 1.00f);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.052f, 0.060f, 0.97f));
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.27f, 0.30f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.88f, 0.30f, 0.32f, 0.55f));
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.88f, 0.30f, 0.32f, 0.13f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.88f, 0.30f, 0.32f, 0.18f));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.88f, 0.30f, 0.32f, 0.22f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

	ImGui::SetNextWindowPos(state.position, ImGuiCond_Always);
	ImGui::SetNextWindowSizeConstraints(ImVec2(245.0f, 0.0f), ImVec2(295.0f, 370.0f));
	const ImGuiWindowFlags panelFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_AlwaysAutoResize;

	bool panelHovered = false;
	if (ImGui::Begin("##radar_loot_cluster_panel", nullptr, panelFlags))
	{
		ImGui::TextColored(accentColour, ICON_FA_CUBES_STACKED "  NEARBY LOOT");
		const int distance = static_cast<int>(glm::distance(mainGame.localLocation, state.worldCenter));
		const std::string entryCountText =
			std::to_string(state.entries.size()) + " | 2 m | " +
			std::to_string(distance) + " m";
		const float entryCountX =
			ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(entryCountText.c_str()).x;
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), entryCountX));
		ImGui::TextColored(labelColour, "%s", entryCountText.c_str());

		ImGui::Separator();

		const float rowHeight = ImGui::GetTextLineHeight() + 6.0f;
		float requestedHeight = static_cast<float>(state.entries.size()) * rowHeight;
		if (state.expandedCorpseId != 0)
		{
			const auto expandedCorpse = std::find_if(
				state.entries.begin(),
				state.entries.end(),
				[](const LootEntity& entry)
				{
					return entry.isCorpse() &&
						GetRadarLootEntityUiId(entry) == state.expandedCorpseId;
				});
			if (expandedCorpse != state.entries.end())
			{
				const CorpseLootState& corpseState =
					expandedCorpse->getCorpseState();
				const size_t lootableCount = static_cast<size_t>(std::count_if(
					corpseState.equipment.begin(),
					corpseState.equipment.end(),
					[&corpseState](const CorpseEquipment& equipment)
					{
						return corpseState.isEquipmentLootable(equipment);
					}));
				requestedHeight += std::min(
					110.0f,
					17.0f * static_cast<float>(lootableCount));
			}
		}
		const float listHeight = std::min(235.0f, std::max(rowHeight, requestedHeight));

		ImGui::BeginChild(
			"##loot_cluster_entries",
			ImVec2(0.0f, listHeight),
			false,
			requestedHeight > listHeight ? ImGuiWindowFlags_AlwaysVerticalScrollbar : ImGuiWindowFlags_None);

		for (const LootEntity& entry : state.entries)
		{
			const bool isCorpse = entry.isCorpse();
			const uint64_t entryId = GetRadarLootEntityUiId(entry);
			const bool expanded = isCorpse && state.expandedCorpseId == entryId;
			const CorpseLootState* corpseState = isCorpse ? &entry.getCorpseState() : nullptr;
			size_t lootableCorpseItems = 0;
			if (corpseState)
			{
				lootableCorpseItems = static_cast<size_t>(std::count_if(
					corpseState->equipment.begin(),
					corpseState->equipment.end(),
					[corpseState](const CorpseEquipment& equipment)
					{
						return corpseState->isEquipmentLootable(equipment);
					}));
			}

			const float entryRowHeight = rowHeight;
			ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(entryId)));
			const bool clicked = ImGui::Selectable(
				"##loot_cluster_entry",
				expanded,
				ImGuiSelectableFlags_None,
				ImVec2(ImGui::GetContentRegionAvail().x, entryRowHeight));
			const ImVec2 rowMinimum = ImGui::GetItemRectMin();
			const ImVec2 rowMaximum = ImGui::GetItemRectMax();
			ImDrawList* panelDrawList = ImGui::GetWindowDrawList();
			ImFont* panelFont = ImGui::GetFont();
			const float textY = rowMinimum.y + 2.0f;

			const char* marker = isCorpse
				? (expanded ? ICON_FA_ANGLE_DOWN : ICON_FA_ANGLE_RIGHT)
				: ICON_FA_DIAMOND;
			panelDrawList->AddText(
				panelFont,
				isCorpse ? panelFont->FontSize : panelFont->FontSize * 0.65f,
				ImVec2(rowMinimum.x + 4.0f, textY + (isCorpse ? 0.0f : 3.0f)),
				ImGui::GetColorU32(isCorpse ? accentColour : valueColour),
				marker);

			const std::string heightIndicator = GetRadarHeightIndicator(entry.worldLocation.y);
			if (!heightIndicator.empty())
			{
				panelDrawList->AddText(
					panelFont,
					panelFont->FontSize * 0.72f,
					ImVec2(rowMinimum.x + 18.0f, textY + 2.0f),
					ImGui::GetColorU32(labelColour),
					heightIndicator.c_str());
			}

			const float nameX = rowMinimum.x + 32.0f;
			const std::string valueText = GetRadarLootEntityValue(entry) > 0
				? FormatShortValue(GetRadarLootEntityValue(entry))
				: "-";
			const float valueWidth = ImGui::CalcTextSize(valueText.c_str()).x;
			const float valueX = rowMaximum.x - valueWidth - 5.0f;
			const ImVec4 nameClip(nameX, rowMinimum.y, std::max(nameX, valueX - 7.0f), rowMaximum.y);
			std::string entryName = GetRadarLootClusterEntryName(entry);
			if (isCorpse)
				entryName += " [" + std::to_string(lootableCorpseItems) + "]";
			panelDrawList->AddText(
				panelFont,
				panelFont->FontSize,
				ImVec2(nameX, textY),
				ImGui::GetColorU32(primaryColour),
				entryName.c_str(),
				nullptr,
				0.0f,
				&nameClip);
			panelDrawList->AddText(
				ImVec2(valueX, textY),
				ImGui::GetColorU32(valueColour),
				valueText.c_str());

			if (isCorpse && clicked)
				state.expandedCorpseId = expanded ? 0 : entryId;

			if (expanded && corpseState)
			{
				ImGui::Spacing();
				ImGui::Indent(16.0f);
				if (lootableCorpseItems == 0)
				{
					ImGui::TextColored(labelColour, "No lootable equipment");
				}
				else if (ImGui::BeginTable(
					"##cluster_corpse_equipment",
					2,
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_RowBg |
					ImGuiTableFlags_NoSavedSettings))
				{
					ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Price", ImGuiTableColumnFlags_WidthFixed);
					for (const CorpseEquipment& equipment : corpseState->equipment)
					{
						if (!corpseState->isEquipmentLootable(equipment))
							continue;

						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextColored(primaryColour, "%s", equipment.name.c_str());
						ImGui::TableSetColumnIndex(1);
						const std::string equipmentValue = FormatShortValue(equipment.value);
						ImGui::SetCursorPosX(
							ImGui::GetCursorPosX() +
							std::max(0.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(equipmentValue.c_str()).x));
						ImGui::TextColored(valueColour, "%s", equipmentValue.c_str());
					}
					ImGui::EndTable();
				}
				ImGui::Unindent(16.0f);
				ImGui::Spacing();
			}

			ImGui::PopID();
		}

		ImGui::EndChild();
		state.size = ImGui::GetWindowSize();
		panelHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
			ImGui::IsAnyItemHovered();
	}
	ImGui::End();

	ImGui::PopStyleVar(4);
	ImGui::PopStyleColor(6);

	state.panelHoveredLastFrame = panelHovered;
	if (leftClicked && !markerClicked && !panelHovered)
	{
		state.visible = false;
		state.clusterId = 0;
		state.expandedCorpseId = 0;
	}
}

float DrawRadarPlayerCorpseMarkers(int x, int y, float zoomLevel, const LootEntity& lootList)
{
	float markerFontSize = std::clamp(30.f / zoomLevel, 7.f, 9.f);
	const float textFontSize = ScaleRadarTextSize(markerFontSize + 8.0f);
	const float spacingX = 6.0f;
	const float spacingY = 1.0f;

	const std::string markerText = ICON_FK_TIMES;
	const int corpseValue = lootList.getCorpseValue();
	const std::string valueText = corpseValue > 0
		? FormatShortValue(corpseValue)
		: std::string{};
	const std::string ownerText = GetRenderableCorpseOwnerLabel(lootList);
	const bool hasWantedEquipment =
		lootList.getCorpseState().hasWantedEquipment();

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();

	ImVec4 color(
		coloursGlobals::playerCorpse.x,
		coloursGlobals::playerCorpse.y,
		coloursGlobals::playerCorpse.z,
		coloursGlobals::playerCorpse.w
	);
	ImU32 drawColor = ImColor(color.x, color.y, color.z, color.w);

	float baseX = static_cast<float>(x) + 5.0f;
	float baseY = static_cast<float>(y) + 2.0f;

	ImVec2 markerSize = font->CalcTextSizeA(markerFontSize, FLT_MAX, 0.0f, markerText.c_str());

	ImVec2 markerPos(baseX - (markerSize.x * 0.5f), baseY - (markerSize.y * 0.5f));

	drawList->AddText(
		font,
		markerFontSize,
		markerPos,
		drawColor,
		markerText.c_str()
	);

	if (hasWantedEquipment)
	{
		const float wantedFontSize = std::max(textFontSize * 1.15f, 12.0f);
		DrawRadarMarkerText(
			drawList,
			font,
			wantedFontSize,
			ImVec2(markerPos.x + markerSize.x + 1.0f, markerPos.y - 9.0f),
			IM_COL32(245, 190, 76, 255),
			"*");
	}

	float rowX = baseX + (markerSize.x * 0.5f) + spacingX;
	float rowY = baseY - (markerSize.y * 0.5f);

	// hover bounds
	float minX = markerPos.x;
	float minY = markerPos.y;
	float maxX = markerPos.x + markerSize.x;
	float maxY = markerPos.y + markerSize.y;

	float firstRowHeight = markerSize.y;

	if (!valueText.empty())
	{
		ImVec2 valueSize = font->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, valueText.c_str());
		ImVec2 valuePos(rowX, rowY);

		drawList->AddText(
			font,
			textFontSize,
			valuePos,
			drawColor,
			valueText.c_str()
		);

		firstRowHeight = std::max(firstRowHeight, valueSize.y);

		minX = std::min(minX, valuePos.x);
		minY = std::min(minY, valuePos.y);
		maxX = std::max(maxX, valuePos.x + valueSize.x);
		maxY = std::max(maxY, valuePos.y + valueSize.y);
	}

	if (!ownerText.empty())
	{
		const float ownerY = rowY + firstRowHeight + spacingY;
		ImVec2 nameSize = font->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, ownerText.c_str());
		ImVec2 namePos(rowX, ownerY);

		drawList->AddText(
			font,
			textFontSize,
			namePos,
			drawColor,
			ownerText.c_str()
		);

		minX = std::min(minX, namePos.x);
		minY = std::min(minY, namePos.y);
		maxX = std::max(maxX, namePos.x + nameSize.x);
		maxY = std::max(maxY, namePos.y + nameSize.y);
	}

	// make hover area a bit easier to hit
	const float hoverPadding = 4.0f;
	ImVec2 hoverMin(minX - hoverPadding, minY - hoverPadding);
	ImVec2 hoverMax(maxX + hoverPadding, maxY + hoverPadding);

	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
		!ImGui::IsMouseHoveringRect(hoverMin, hoverMax))
		return FLT_MAX;

	const ImVec2 mousePosition = ImGui::GetIO().MousePos;
	const float deltaX = mousePosition.x - baseX;
	const float deltaY = mousePosition.y - baseY;
	return (deltaX * deltaX) + (deltaY * deltaY);
}

void drawGroupLine(glm::vec3 position, Player player)
{
	//get current players groupid
	std::string groupid = player.groupId;

	//skip people that is same as localgroup ie. friendly people
	if (groupid == mainGame.localGroupId)
		return;

	//filter out no groups here
	if (groupid > "")
	{
		const PlayerSnapshot playerCacheSnapshot = registeredPlayers.getCacheSnapshot();
		const PlayerCollection& playerCache = *playerCacheSnapshot;
		//loop player list to find other players with same group and draw line to them from current player
		for (auto& cache : playerCache)
		{
			if (cache.isDead || cache.hasExfiled)
				continue;

			

			if (groupid == cache.groupId)
			{

				//position on map

				glm::vec3 positionOther = mapControl.getMapPosition(cache.location, currentMap::configX, currentMap::configY, currentMap::configScale);

				if (positionOther.x > 100000 || cache.distance > 4000)
					continue;


				//draw line to this player
				DrawLine(position.x, position.y, positionOther.x, positionOther.y, glm::vec4(0, 1, 0, 1), 2);


			}
		}
	}
}

void DrawQuest(float x, float y, float zoom, QuestLocation qloc)
{
	float fontSize = std::clamp(20.f / zoom, 8.f, 10.f);
	const float heightIconFontSize = (fontSize + 6.0f) * 0.5f;
	float fontSizeFix = 0.f;

	//loot height indicator
	std::string hString = "";
	std::string hStringVal = "";

	//local height
	float height = qloc.pos.y;

	if (height > (mainGame.localLocation.y + 2.f)) // 2.f per level?!
	{

		if (height > (mainGame.localLocation.y + 4.f))
			hString = ICON_FK_ANGLE_DOUBLE_UP;
		else
			hString = ICON_FK_ANGLE_UP;

	}
	if (height < (mainGame.localLocation.y - 2.f))
	{

		if (height < (mainGame.localLocation.y - 4.f))
			hString = ICON_FK_ANGLE_DOUBLE_DOWN;
		else
			hString = ICON_FK_ANGLE_DOWN;

	}
	if (height != mainGame.localLocation.y)
	{
		int correctedNumber = static_cast<int>(height - mainGame.localLocation.y);
		hStringVal = std::to_string(correctedNumber); // height difference
	}

	//color 
	ImVec4 color = (ImVec4&)coloursGlobals::questMarker;

	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	std::string string = ICON_FK_SQUARE;


	std::string name = "(" + qloc.objectiveType + ") " + qloc.questName;
	
	//main marker
	draw_list->AddText(ImGui::GetFont(), 5, ImVec2(x, y), ImColor(color.x, color.y, color.z, color.w), string.c_str(), 0, 0.0f, 0);

	//Height indicator
	draw_list->AddText(ImGui::GetFont(), heightIconFontSize, ImVec2(x - 15, y - 5), ImColor(color.x, color.y, color.z, color.w), hString.c_str(), 0, 0.0f, 0);
	//if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
	//	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 17, y + 20), ImColor(1.f,1.f,1.f,1.f), hStringVal.c_str(), 0, 0.0f, 0);

	// name
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x + 10, y - 7), ImColor(color.x, color.y, color.z, color.w), name.c_str(), 0, 0.0f, 0);

}

void DrawExfil(int x, int y, float zoomLevel, const exfilsMemory& exfil)
{
	float fontSize = std::clamp(30.f / zoomLevel, 12.f, 14.f);
	std::string string = ICON_FK_SIGN_OUT;

	//int distancetoMe = std::trunc(glm::distance(gameGlobals::LocalPlayer::localRootPos, exfil.extractLocationWorld));
	int distancetoMe = std::trunc(glm::distance(mainGame.localLocation, exfil.locationWorld));
	std::string name = exfil.extractName;
	float nameSizeHalf = ImGui::CalcTextSize(name.c_str()).x / 2;


	//color 
	ImVec4 color = { 1, 1, 1, 1 };

	if (exfil.type == ExfilType::Transit)
	{
		color = { 0.2f, 0.8f, 1.0f, 1.0f };
	}
	else if (exfil.type == ExfilType::Secret)
	{
		color = { 0.85f, 0.35f, 1.0f, 1.0f };
	}
	else if (exfil.status.find("Open") != std::string::npos)
	{
		color = { 0,1,0,1 }; // green
	}
	else if (exfil.status.find("Closed") != std::string::npos)
	{
		color = { 1,0,0,1 }; // red
	}
	else if (exfil.status.find("Pending") != std::string::npos || exfil.status.find("Req") != std::string::npos)
	{
		color = { 1,0.5,0,1 }; // orange
	}

	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	//icon
	draw_list->AddText(ImGui::GetFont(), fontSize, ImVec2(x, y), ImColor(color.x, color.y, color.z, color.w), string.c_str(), 0, 0.0f, 0);

	//name
	draw_list->AddText(ImGui::GetFont(), fontSize, ImVec2(x - nameSizeHalf + 5, y + fontSize + 3), ImColor(1.f, 1.f, 1.f, 1.f), exfil.extractName.c_str(), 0, 0.0f, 0);

}

void DrawLootContainerMarker(float x, float y, glm::vec4 color, float zoomLevel, const LootEntity& loot)
{
	const float markerFontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);
	const float labelFontSize = ScaleRadarTextSize(markerFontSize + 6.0f);
	const float heightIconFontSize = labelFontSize * 0.5f;
	constexpr float markerHalfSize = 3.5f;
	constexpr float labelGap = 3.0f;

	const std::string hString = GetRadarHeightIndicator(loot.worldLocation.y);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();
	const ImU32 drawColor = ImColor(color.x, color.y, color.z, color.w);

	//main marker
	DrawCenteredSquareMarker(x, y, markerHalfSize, drawColor);

	//Height indicator
	const ImVec2 heightSize = MeasureRadarText(font, heightIconFontSize, hString.c_str());
	drawList->AddText(
		font,
		heightIconFontSize,
		ImVec2(x + markerHalfSize + 4.0f, y - (heightSize.y * 0.5f)),
		drawColor,
		hString.c_str());

	// name
	DrawCenteredRadarText(drawList, font, labelFontSize, x, y + markerHalfSize + labelGap, drawColor, loot.shortName.c_str());
}

void DrawLootItemMarker(float x, float y, glm::vec4 color, float zoomLevel, const LootEntity& loot)
{
	const float markerFontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);
	const float labelFontSize = ScaleRadarTextSize(markerFontSize + 6.0f);
	const float heightIconFontSize = labelFontSize * 0.5f;
	constexpr float markerHalfSize = 3.0f;
	constexpr float labelGap = 3.0f;

	const std::string hString = GetRadarHeightIndicator(loot.worldLocation.y);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();
	const ImU32 drawColor = ImColor(color.x, color.y, color.z, color.w);

	//loose item 
	//main marker
	DrawCenteredSquareMarker(x, y, markerHalfSize, drawColor);

	//Height indicator
	const ImVec2 heightSize = MeasureRadarText(font, heightIconFontSize, hString.c_str());
	drawList->AddText(
		font,
		heightIconFontSize,
		ImVec2(x + markerHalfSize + 4.0f, y - (heightSize.y * 0.5f)),
		drawColor,
		hString.c_str());

    //item name and optional price
    const std::string displayName = GetLootDisplayName(loot);
    DrawCenteredRadarText(drawList, font, labelFontSize, x, y + markerHalfSize + labelGap, drawColor, displayName.c_str());
}

void DrawLootFocusRipple(float x, float y, float phase, const glm::vec4& colour)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    for (int ring = 0; ring < 2; ++ring)
    {
        float ringPhase = phase + (ring * 0.5f);

        if (ringPhase >= 1.0f)
            ringPhase -= 1.0f;

        const float radius = 8.0f + (ringPhase * 28.0f);
        const float alpha = (1.0f - ringPhase) * colour.a;
        const ImU32 rippleColour = ImColor(colour.r, colour.g, colour.b, alpha);
        drawList->AddCircle(ImVec2(x, y), radius, rippleColour, 32, 2.0f);
    }
}

void DrawGrenade(int x, int y, float zoomLevel, GrenadeList grenade)
{
	int innerRadius = 5;
	int outterRadius = 15;

	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddCircleFilled(ImVec2(x, y), innerRadius, ImColor(1.f, 0.f, 0.f, 1.f));
	draw_list->AddCircle(ImVec2(x, y), outterRadius, ImColor(1.f, 0.f, 0.f, 1.f), 100);
}

void DrawTripWire(int x, int y, glm::vec4 color, float zoomLevel)
{
	const float radius = std::clamp(5.0f / zoomLevel, 2.5f, 5.0f);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImU32 drawColor = ImColor(color.x, color.y, color.z, color.w);

	// The wire itself is drawn between its two anchors. This marker just makes
	// the armed endpoint easy to distinguish at low zoom levels.
	drawList->AddCircleFilled(ImVec2(x, y), radius, drawColor);
	drawList->AddCircle(ImVec2(x, y), radius + 1.0f, IM_COL32(0, 0, 0, 220), 12, 1.0f);
}
