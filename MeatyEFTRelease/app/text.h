#pragma once
#include "../game/headers/players.h"
#include "../game/headers/exfil.h"
#include "../game/headers/explosives.h"
#include "../game/headers/utils.h"

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

void DrawLine(int x1, int y1, int x2, int y2, glm::vec4 color, int thickness)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), ImColor(color.x, color.y, color.z, color.w), thickness);
}

void DrawCircleFilled(int x, int y, int radius, ImVec4 color)
{
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	draw_list->AddCircleFilled(ImVec2(x, y), radius, ImColor(color.x, color.y, color.z, color.w));
	draw_list->AddCircle(ImVec2(x, y), radius, ImColor(0.f, 0.f, 0.f, 1.f), 20, 1.f);
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

void DrawRadarHealthBar(int x, int y, int healthStatus, float zoomLevel)
{
	int width = 50; // longer for more health
	int height = 7; //static 
	float scale = zoomLevel; // scale to map size
	ImVec4 healthcol = { 0,1,0,1 };

	/*
	Healthy = 1024,
	Injured = 2048,
	BadlyInjured = 4096,
	Dying = 8192,
	*/

	if (healthStatus == 1024)
	{
		width = 50;
		healthcol = { 0,1,0,1 };
	}
	else if (healthStatus == 2048)
	{
		width = 39;
		healthcol = { 1,0.64,0,1 };
	}
	else if (healthStatus == 4096)
	{
		width = 26;
		healthcol = { 1,1,0,1 };
	}
	else if (healthStatus == 8192)
	{
		width = 13;
		healthcol = { 1,0,0,1 };
	}
	else
	{
		width = 50;
		healthcol = { 0,1,0,1 };
	}

	//std::cout << "xy : " << std::to_string(x) << " " << std::to_string(y) << " Health : " << std::to_string(healthStatus) << std::endl;

	DrawRect(x - 1, y - 1, 52, height + 2, (RGBA*)&Col.black, 1.5); // added 2 to space outside of max health to add a shadow to box
	DrawFilledRect(x, y, width, height, healthcol);
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

void DrawRadarPlayerMarkers(int x, int y, float zoomLevel, PlayerCache player)
{
	float fontSize = std::clamp(30.f / zoomLevel, 7.f, 9.f);

	//player height
	float height = player.location.y;

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




	//name and details
	int distancetoMe = std::trunc(players.getDistance(mainGame.localLocation, player.location));
	std::string prefix;

	if (player.playerSide == EPlayerSide::Usec)
		prefix = "U:";
	else if (player.playerSide == EPlayerSide::Bear)
		prefix = "B:";

	//check slots for wanted items
	bool wanted = false;

	for (const auto& slot : player._slots)
	{
		if (slot.wanted)
		{
			wanted = true;
			break;
		}
	}


	std::string name =
		(wanted ? "" : "") +
		prefix +
		player.name;

	float nameSizeHalf = ImGui::CalcTextSize(name.c_str()).x / 2;

	//item in hand
	std::string itemInHand = player.itemInHand;
	float itemSizeHalf = ImGui::CalcTextSize(itemInHand.c_str()).x / 2;

	va_list va_alist;

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

	static uint64_t selectedPlayerInstance = 0;

	if (!player.isDead)
	{

		int markerX = (int)(x - 4.f);
		int markerY = (int)(y - 4.f);
		int markerRadius = 8;

		// main marker
		DrawCircleFilled(markerX, markerY, markerRadius, ImColor(color.x, color.y, color.z, color.w));

		// hover tooltip
		HandlePlayerSlotClick(markerX, markerY, markerRadius, player, selectedPlayerInstance);

		if (selectedPlayerInstance == player.instance)
			DrawPinnedPlayerSlotsBox(markerX, markerY, player);

		//Height indicator
		draw_list->AddText(ImGui::GetFont(), fontSize + 8, ImVec2(x + 10, y - 5), ImColor(color.x, color.y, color.z, color.w), hString.c_str(), 0, 0.0f, 0);
		if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
			draw_list->AddText(ImGui::GetFont(), fontSize + 8, ImVec2(x + 10, y - 22), ImColor(color.x, color.y, color.z, color.w), hStringVal.c_str(), 0, 0.0f, 0);

		int equipmentTextY = 0;

		if (!appMenu::minView)
		{
			if (player.isPlayer || player.isBoss || player.isPlayerScav)
			{

				//name text
				draw_list->AddText(ImGui::GetFont(), fontSize + 8, ImVec2(x - nameSizeHalf, y + 7), ImColor(color.x, color.y, color.z, color.w), name.c_str(), 0, 0.0f, 0);

				//item in hand
				draw_list->AddText(ImGui::GetFont(), fontSize + 8, ImVec2(x - itemSizeHalf, y + 21), ImColor(color.x, color.y, color.z, color.w), itemInHand.c_str(), 0, 0.0f, 0);

				//health
				if (player.healthETAG != 1024)
				{
					DrawRadarHealthBar(x - (52 / 2), y + 40, player.healthETAG, zoomLevel);
				}

				equipmentTextY = y + 50;
			}
			else if (player.isBTR)
			{
				//name text
				draw_list->AddText(ImGui::GetFont(), fontSize + 8, ImVec2(x - nameSizeHalf, y + 7), ImColor(color.x, color.y, color.z, color.w), name.c_str(), 0, 0.0f, 0);
			}
			else
			{
				//item in hand
				draw_list->AddText(ImGui::GetFont(), fontSize + 8, ImVec2(x - itemSizeHalf, y + 4), ImColor(color.x, color.y, color.z, color.w), itemInHand.c_str(), 0, 0.0f, 0);

				//health
				if (player.healthETAG != 1024)
				{
					DrawRadarHealthBar(x - (52 / 2), y + 24, player.healthETAG, zoomLevel);
				}

				equipmentTextY = y + 33;
			}

			
		}

		//draw equipment that is wanted
		if (radarGlobals::getPlayerEquip)
		{
			y = equipmentTextY;

			for (auto& slot : player._slots)
			{
				std::string slotn = TrimEFT(slot.name);

				if (!slot.wanted)
					continue;

				if (slotn == "SecuredContainer")
					continue;

				if (player.isPlayer && slotn == "Scabbard")
					continue;

				std::string text = slot.equipName + " " + FormatShortValue(slot.price) + "";

				ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
				float textHalf = textSize.x * 0.5f;

				draw_list->AddText(
					ImGui::GetFont(),
					fontSize + 10,
					ImVec2(x - textHalf, y),
					ImColor(color.x, color.y, color.z, color.w),
					text.c_str()
				);

				y += textSize.y + 2; // stack lines
			}
		}
	}


}

void DrawRadarPlayerCorpseMarkers(int x, int y, float zoomLevel, LootList lootList)
{
	float markerFontSize = std::clamp(30.f / zoomLevel, 7.f, 9.f);
	const float textFontSize = markerFontSize + 8.0f;
	const float itemFontSize = markerFontSize + 6.0f;
	const float spacingX = 6.0f;
	const float spacingY = 2.0f;

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
		std::vector<PlayerCache>& playerCache = players.getCache();
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
				DrawLine(position.x - 4.f, position.y - 4.f, positionOther.x - 4.f, positionOther.y - 4.f, glm::vec4(0, 1, 0, 1), 2);


			}
		}
	}
}

void DrawQuest(float x, float y, float zoom, QuestLocation qloc)
{
	float fontSize = std::clamp(20.f / zoom, 8.f, 10.f);
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
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 15, y - 5), ImColor(color.x, color.y, color.z, color.w), hString.c_str(), 0, 0.0f, 0);
	//if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
	//	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 17, y + 20), ImColor(1.f,1.f,1.f,1.f), hStringVal.c_str(), 0, 0.0f, 0);

	// name
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x + 10, y - 7), ImColor(color.x, color.y, color.z, color.w), name.c_str(), 0, 0.0f, 0);

}

void DrawExfil(int x, int y, float zoomLevel, exfilsMemory exfil)
{
	float fontSize = std::clamp(30.f / zoomLevel, 12.f, 14.f);
	std::string string = ICON_FK_SIGN_OUT;

	//int distancetoMe = std::trunc(glm::distance(gameGlobals::LocalPlayer::localRootPos, exfil.extractLocationWorld));
	int distancetoMe = std::trunc(glm::distance(mainGame.localLocation, exfil.locationWorld));
	std::string name = exfil.extractName;
	float nameSizeHalf = ImGui::CalcTextSize(name.c_str()).x / 2;


	//color 
	ImVec4 color;

	if (exfil.status.find("Open") != std::string::npos)
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

void DrawLootContainerMarker(int x, int y, glm::vec4 color, float zoomLevel, LootList loot)
{
	float fontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);
	float fontSizeFix = 0.f;

	//loot height indicator
	std::string hString = "";
	std::string hStringVal = "";

	//local height
	float height = loot.worldLocation.y;

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



	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	std::string string = ICON_FK_SQUARE;


	//airdrop check and order name
	std::string name;
	if (loot.shortName == "AirDrop")
		name = loot.shortName;
	else
		name = loot.shortName;

	//main marker
	draw_list->AddText(ImGui::GetFont(), 5, ImVec2(x, y), ImColor(1.f, 1.f, 1.f, 1.f), string.c_str(), 0, 0.0f, 0);

	//Height indicator
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 15, y - 5), ImColor(1.f, 1.f, 1.f, 1.f), hString.c_str(), 0, 0.0f, 0);
	//if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
	//	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 17, y + 20), ImColor(1.f,1.f,1.f,1.f), hStringVal.c_str(), 0, 0.0f, 0);

	// name
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x + 10, y - 7), ImColor(1.f, 1.f, 1.f, 1.f), name.c_str(), 0, 0.0f, 0);

}

void DrawLootItemMarker(int x, int y, glm::vec4 color, float zoomLevel, LootList loot)
{
	float fontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);
	float fontSizeFix = 0.f;

	//loot height indicator
	std::string hString = "";
	std::string hStringVal = "";

	//local height
	float height = loot.worldLocation.y;

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


	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();
	std::string string = ICON_FK_SQUARE;

	//loose item 
	//main marker
	draw_list->AddText(ImGui::GetFont(), 6, ImVec2(x - 6.f, y - 6.f), ImColor(color.x, color.y, color.z, color.w), string.c_str(), 0, 0.0f, 0);

	//Height indicator
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 15, y - 4), ImColor(color.x, color.y, color.z, color.w), hString.c_str(), 0, 0.0f, 0);
	//if (hStringVal != "0" && hStringVal != "1" && hStringVal != "-1")
	//	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 17, y + 20), ImColor(color.x, color.y, color.z, color.w), hStringVal.c_str(), 0, 0.0f, 0);

	//item name
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x + 10, y - 7), ImColor(color.x, color.y, color.z, color.w), loot.shortName.c_str(), 0, 0.0f, 0);
	
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
	float fontSize = std::clamp(20.f / zoomLevel, 8.f, 10.f);

	//draw list
	ImDrawList* draw_list = ImGui::GetWindowDrawList();

	std::string string = ICON_FK_WIRE;
	//main marker
	draw_list->AddText(ImGui::GetFont(), fontSize + 6, ImVec2(x - 3, y), ImColor(color.x, color.y, color.z, color.w), string.c_str(), 0, 0.0f, 0);

}