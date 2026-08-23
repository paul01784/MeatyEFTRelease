#pragma once
#include "../game/headers/players.h"
#include "../game/headers/exfil.h"
#include "../game/headers/explosives.h"
#include "../game/headers/utils.h"
#include "../game/headers/loot.h"

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

void drawAimLine(glm::vec2 point, glm::vec2 rotation, int aimLineLength, glm::vec4 color)
{

	double radians = (PI / 180) * rotation.x;

	glm::vec2 endPoint = {
		point.x + (glm::cos(radians) * aimLineLength),
		point.y + (glm::sin(radians) * aimLineLength)
	};

	DrawLine(point.x, point.y, endPoint.x, endPoint.y, color, 3);

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

void HandlePlayerSlotClick(int x, int y, int radius, const PlayerCache& player, uint64_t& selectedPlayerInstance)
{
	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 mouseScreen = ImGui::GetIO().MousePos;
	ImVec2 mouseLocal(mouseScreen.x - windowPos.x, mouseScreen.y - windowPos.y);

	float clickRadius = radius + 16.0f;  // increase this

	ImRect rect(
		ImVec2((float)x - clickRadius, (float)y - clickRadius),
		ImVec2((float)x + clickRadius, (float)y + clickRadius)
	);

	if (!rect.Contains(mouseLocal))
		return;

	if (ImGui::IsMouseClicked(0))
	{
		if (selectedPlayerInstance == player.instance)
			selectedPlayerInstance = 0;
		else
			selectedPlayerInstance = player.instance;
	}
}

void DrawPinnedPlayerSlotsBox(int x, int y, const PlayerCache& player)
{
	ImDrawList* draw_list = ImGui::GetForegroundDrawList();

	constexpr float pad = 6.0f;
	constexpr float rounding = 4.0f;
	constexpr float fontSize = 16.0f;
	const float lineHeight = fontSize + 2.0f;

	float maxWidth = 0.0f;
	std::vector<std::string> lines;
	lines.reserve(player._slots.size());

	for (const auto& slot : player._slots)
	{
		if (slot.equipName.empty())
			continue;

		std::string line = slot.equipName;

		if (slot.price > 0)
			line += " (" + FormatShortValue(slot.price) + ")";

		ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, line.c_str());
		if (size.x > maxWidth)
			maxWidth = size.x;

		lines.push_back(line);
	}

	if (lines.empty())
		return;

	ImVec2 windowPos = ImGui::GetWindowPos();
	ImVec2 anchor((float)x + windowPos.x, (float)y + windowPos.y);

	ImVec2 boxMin(anchor.x + 16.0f, anchor.y - 8.0f);
	ImVec2 boxMax(
		boxMin.x + maxWidth + (pad * 2.0f),
		boxMin.y + (lines.size() * lineHeight) + (pad * 2.0f));

	draw_list->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 230), rounding);
	draw_list->AddRect(boxMin, boxMax, IM_COL32(255, 255, 255, 80), rounding);

	ImVec2 drawPos(boxMin.x + pad, boxMin.y + pad);

	for (const auto& line : lines)
	{
		draw_list->AddText(ImGui::GetFont(), fontSize, drawPos, IM_COL32(255, 255, 255, 255), line.c_str());
		drawPos.y += lineHeight;
	}
}

void DrawRadarPlayerMarkers(float x, float y, float zoomLevel, const PlayerCache& player)
{
	const float markerFontSize = std::clamp(30.f / zoomLevel, 7.f, 9.f);
	const float labelFontSize = ScaleRadarTextSize(markerFontSize + 8.0f);
	const float heightIconFontSize = labelFontSize * 0.5f;
	const float equipmentFontSize = ScaleRadarTextSize(markerFontSize + 10.0f);
	constexpr float markerRadius = 8.0f;
	constexpr float labelGap = 3.0f;
	constexpr float lineGap = 1.0f;

	//player height
	const float height = player.location.y;

	//player height indicator
	std::string hString = "";
	std::string hStringVal = "";

	//arrow up and down calc

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




	std::string prefix;

	if (player.playerSide == EPlayerSide::Usec)
		prefix = "U:";
	else if (player.playerSide == EPlayerSide::Bear)
		prefix = "B:";

	const std::string name = prefix + player.name;

	//item in hand
	const std::string itemInHand = player.observedHandsInfo.itemName + " (" + std::string(player.observedHandsInfo.ammoName) + ")";

	//color 
	ImVec4 color;
	color.x = player.colour.x;
	color.y = player.colour.y;
	color.z = player.colour.z;
	color.w = player.colour.w;

	//friendly
	if (player.groupId == mainGame.localGroupId)
	{
		if (mainGame.localGroupId != "")
		{
			color.x = coloursGlobals::playerFriendly.x;
			color.y = coloursGlobals::playerFriendly.y;
			color.z = coloursGlobals::playerFriendly.z;
			color.w = coloursGlobals::playerFriendly.w;
		}
	}

	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	ImFont* font = ImGui::GetFont();
	const ImU32 drawColor = ImColor(color.x, color.y, color.z, color.w);

	static uint64_t selectedPlayerInstance = 0;

	if (!player.isDead)
	{
		// main marker
		DrawCircleFilled(x, y, markerRadius, ImColor(color.x, color.y, color.z, color.w));

		if (player.isInBTR)
			return;

		if (!player.isBTR)
			DrawRadarHealthDot(x, y, player.healthETAG);

		// hover tooltip
		HandlePlayerSlotClick(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), static_cast<int>(markerRadius), player, selectedPlayerInstance);

		if (selectedPlayerInstance == player.instance)
			DrawPinnedPlayerSlotsBox(static_cast<int>(std::round(x)), static_cast<int>(std::round(y)), player);

		//Height indicator
		const float heightTextX = x + markerRadius + 4.0f;
		const ImVec2 heightIconSize = MeasureRadarText(font, heightIconFontSize, hString.c_str());
		draw_list->AddText(font, heightIconFontSize, ImVec2(heightTextX, y - (heightIconSize.y * 0.5f)), drawColor, hString.c_str());

		if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
		{
			const ImVec2 heightValueSize = MeasureRadarText(font, labelFontSize, hStringVal.c_str());
			draw_list->AddText(font, labelFontSize, ImVec2(heightTextX, y - heightIconSize.y - heightValueSize.y), drawColor, hStringVal.c_str());
		}

		float nextTextY = y + markerRadius + labelGap;

		if (!appMenu::minView)
		{
			if (player.isPlayer || player.isBoss || player.isPlayerScav)
			{
				//name text
				const float nameHeight = DrawCenteredRadarText(draw_list, font, labelFontSize, x, nextTextY, drawColor, name.c_str());
				if (nameHeight > 0.0f)
					nextTextY += nameHeight + lineGap;

				//item in hand
				const float itemHeight = DrawCenteredRadarText(draw_list, font, labelFontSize, x, nextTextY, drawColor, itemInHand.c_str());
				if (itemHeight > 0.0f)
					nextTextY += itemHeight + lineGap;
			}
			else if (player.isBTR)
			{
				//name text
				const float nameHeight = DrawCenteredRadarText(draw_list, font, labelFontSize, x, nextTextY, drawColor, name.c_str());
				if (nameHeight > 0.0f)
					nextTextY += nameHeight + lineGap;
			}
			else
			{
				//item in hand
				const float itemHeight = DrawCenteredRadarText(draw_list, font, labelFontSize, x, nextTextY, drawColor, itemInHand.c_str());
				if (itemHeight > 0.0f)
					nextTextY += itemHeight + lineGap;
			}
		}

		//draw equipment that is wanted
		if (radarGlobals::getPlayerEquip)
		{
			for (const auto& slot : player._slots)
			{
				const std::string slotn = TrimEFT(slot.name);

				if (!slot.wanted)
					continue;

				if (slotn == "SecuredContainer")
					continue;

				if (player.isPlayer && slotn == "Scabbard")
					continue;

				const std::string text = slot.equipName + " " + FormatShortValue(slot.price);
				const float textHeight = DrawCenteredRadarText(draw_list, font, equipmentFontSize, x, nextTextY, drawColor, text.c_str());
				if (textHeight > 0.0f)
					nextTextY += textHeight + lineGap;
			}
		}
	}
}

void DrawRadarPlayerCorpseMarkers(int x, int y, float zoomLevel, LootList lootList)
{
	float markerFontSize = std::clamp(30.f / zoomLevel, 7.f, 9.f);
	const float textFontSize = ScaleRadarTextSize(markerFontSize + 8.0f);
	const float itemFontSize = ScaleRadarTextSize(markerFontSize + 6.0f);
	const float spacingX = 6.0f;
	const float spacingY = 1.0f;

	const std::string markerText = ICON_FK_TIMES;
	const std::string valueText = FormatShortValue(lootList.corpseValue);

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

	float rowX = baseX + (markerSize.x * 0.5f) + spacingX;
	float rowY = baseY - (markerSize.y * 0.5f);

	float currentX = rowX;
	float firstRowHeight = 0.0f;

	// hover bounds
	float minX = markerPos.x;
	float minY = markerPos.y;
	float maxX = markerPos.x + markerSize.x;
	float maxY = markerPos.y + markerSize.y;

	if (!lootList.longName.empty())
	{
		std::string nameText = lootList.longName;
		ImVec2 nameSize = font->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, nameText.c_str());
		ImVec2 namePos(currentX, rowY);

		drawList->AddText(
			font,
			textFontSize,
			namePos,
			drawColor,
			nameText.c_str()
		);

		currentX += nameSize.x + spacingX;
		firstRowHeight = std::max(firstRowHeight, nameSize.y);

		minX = std::min(minX, namePos.x);
		minY = std::min(minY, namePos.y);
		maxX = std::max(maxX, namePos.x + nameSize.x);
		maxY = std::max(maxY, namePos.y + nameSize.y);
	}

	{
		ImVec2 valueSize = font->CalcTextSizeA(textFontSize, FLT_MAX, 0.0f, valueText.c_str());
		ImVec2 valuePos(currentX, rowY);

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

	float equipmentY = rowY + firstRowHeight + spacingY;

	for (auto& slot : lootList.corpseEquip)
	{
		if (!slot.wanted)
			continue;

		std::string text = slot.equipmentName + " [" + FormatShortValue(slot.value) + "]";
		ImVec2 itemSize = font->CalcTextSizeA(itemFontSize, FLT_MAX, 0.0f, text.c_str());
		ImVec2 itemPos(rowX, equipmentY);

		drawList->AddText(
			font,
			itemFontSize,
			itemPos,
			drawColor,
			text.c_str()
		);

		minX = std::min(minX, itemPos.x);
		minY = std::min(minY, itemPos.y);
		maxX = std::max(maxX, itemPos.x + itemSize.x);
		maxY = std::max(maxY, itemPos.y + itemSize.y);

		equipmentY += itemSize.y + spacingY;
	}

	// make hover area a bit easier to hit
	const float hoverPadding = 4.0f;
	ImVec2 hoverMin(minX - hoverPadding, minY - hoverPadding);
	ImVec2 hoverMax(maxX + hoverPadding, maxY + hoverPadding);

	if (ImGui::IsMouseHoveringRect(hoverMin, hoverMax))
	{
		ImGui::BeginTooltip();

		ImGui::Separator();

		if (!lootList.longName.empty())
			ImGui::Text("%s", lootList.longName.c_str());

		ImGui::Text("Value: %s", valueText.c_str());

		if (!lootList.corpseEquip.empty())
		{
			ImGui::Separator();

			for (auto& slot : lootList.corpseEquip)
			{
				std::string line = slot.equipmentName + " [" + FormatShortValue(slot.value) + "]";
				ImGui::Text("%s", line.c_str());
			}
		}	

		ImGui::EndTooltip();
	}
}

void drawGroupLine(glm::vec3 position, PlayerCache player)
{
	//get current players groupid
	std::string groupid = player.groupId;

	//skip people that is same as localgroup ie. friendly people
	if (groupid == mainGame.localGroupId)
		return;

	//filter out no groups here
	if (groupid > "")
	{
		const PlayerCacheSnapshot playerCacheSnapshot = players.getCacheSnapshot();
		const PlayerCacheCollection& playerCache = *playerCacheSnapshot;
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

void DrawLootContainerMarker(float x, float y, glm::vec4 color, float zoomLevel, const LootList& loot)
{
	const float markerFontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);
	const float labelFontSize = ScaleRadarTextSize(markerFontSize + 6.0f);
	const float heightIconFontSize = labelFontSize * 0.5f;
	constexpr float markerHalfSize = 3.5f;
	constexpr float labelGap = 3.0f;

	//loot height indicator
	std::string hString;

	//local height
	const float height = loot.worldLocation.y;

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

void DrawLootItemMarker(float x, float y, glm::vec4 color, float zoomLevel, const LootList& loot)
{
	const float markerFontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);
	const float labelFontSize = ScaleRadarTextSize(markerFontSize + 6.0f);
	const float heightIconFontSize = labelFontSize * 0.5f;
	constexpr float markerHalfSize = 3.0f;
	constexpr float labelGap = 3.0f;

	//loot height indicator
	std::string hString;

	//local height
	const float height = loot.worldLocation.y;

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
