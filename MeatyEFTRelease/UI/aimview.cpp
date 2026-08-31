#include "aimview.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif


#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../Tarkov/GameWorld/RegisteredPlayers.h"
#include "../Tarkov/GameWorld/Loot/Loot.h"

AimViewWidget g_AimViewWidget;

// some helpers


static std::string CleanText(std::string text)
{
    text.erase(
        std::remove(text.begin(), text.end(), '\0'),
        text.end()
    );

    return text;
}

static bool HasBones(const Player& player, std::initializer_list<int> indexes)
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


AimViewConfig& AimViewWidget::GetConfig() {
    return config_;
}

const AimViewConfig& AimViewWidget::GetConfig() const {
    return config_;
}

void AimViewWidget::Render(const ImVec2& sourceResolution) {
    if (!config_.enabled)
        return;

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGui::SetNextWindowSize(config_.defaultWindowSize, ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowSizeConstraints(ImVec2(220.0f, 160.0f), ImVec2(2000.0f, 2000.0f));

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove;

    if (!ImGui::Begin("Aim View", nullptr, windowFlags)) {
        ImGui::End();
        return;
    }

    ImVec2 windowPosition = ImGui::GetWindowPos();

    const ImVec2 windowSize = ImGui::GetWindowSize();

    const float viewportLeft = mainViewport->WorkPos.x;

    const float viewportTop = mainViewport->WorkPos.y;

    const float viewportRight = mainViewport->WorkPos.x + mainViewport->WorkSize.x;

    const float viewportBottom = mainViewport->WorkPos.y + mainViewport->WorkSize.y;

    canvasPosition_ = ImGui::GetCursorScreenPos();

    canvasSize_ = ImGui::GetContentRegionAvail();

    canvasSize_.x = std::max(canvasSize_.x, 50.0f);
    canvasSize_.y = std::max(canvasSize_.y, 50.0f);

    ImGui::InvisibleButton("##AimViewCanvas",
        canvasSize_,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight
    );

    const bool canvasHovered = ImGui::IsItemHovered();

    const bool settingsPopupOpen = ImGui::IsPopupOpen("##AimViewSettingsPopup");

    UpdateTransform(sourceResolution);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    DrawBackground(drawList);

    if (transformValid_) {
        const ImVec2 clipMinimum = canvasPosition_;

        const ImVec2 clipMaximum = ImVec2(
            canvasPosition_.x + canvasSize_.x,
            canvasPosition_.y + canvasSize_.y
        );

        drawList->PushClipRect(clipMinimum, clipMaximum, true);

        if (espGlobals::drawPlayers)
            DrawPlayers(drawList);

        if (espGlobals::drawLoot || espGlobals::drawCorpse)
            DrawLoot(drawList);

        DrawContainers(drawList);

        if (espGlobals::drawCrosshair)
            DrawCrosshair(drawList);

        drawList->PopClipRect();
    }

    constexpr float buttonWidth = 24.0f;
    constexpr float buttonHeight = 20.0f;
    constexpr float buttonSpacing = 4.0f;
    constexpr float buttonMargin = 5.0f;
    constexpr float dragRegionHeight = 30.0f;


    const ImVec2 closeButtonMinimum = ImVec2(
        canvasPosition_.x +
        canvasSize_.x -
        buttonWidth -
        buttonMargin,
        canvasPosition_.y + buttonMargin
    );

    const ImVec2 closeButtonMaximum = ImVec2(
        closeButtonMinimum.x + buttonWidth,
        closeButtonMinimum.y + buttonHeight
    );

    const ImVec2 settingsButtonMinimum = ImVec2(
        closeButtonMinimum.x -
        buttonWidth -
        buttonSpacing,
        closeButtonMinimum.y
    );

    const ImVec2 settingsButtonMaximum = ImVec2(
        settingsButtonMinimum.x + buttonWidth,
        settingsButtonMinimum.y + buttonHeight
    );

    const bool settingsButtonHovered = ImGui::IsMouseHoveringRect(settingsButtonMinimum, settingsButtonMaximum, true);

    const bool closeButtonHovered = ImGui::IsMouseHoveringRect( closeButtonMinimum, closeButtonMaximum, true);

    const ImVec2 dragRegionMinimum = canvasPosition_;

    const ImVec2 dragRegionMaximum = ImVec2(settingsButtonMinimum.x - buttonSpacing, canvasPosition_.y + dragRegionHeight);

    const bool dragRegionHovered = ImGui::IsMouseHoveringRect(dragRegionMinimum, dragRegionMaximum, true) && !settingsButtonHovered && !closeButtonHovered;

    static bool draggingWindow = false;

    if (dragRegionHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        draggingWindow = true;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        draggingWindow = false;

    if (draggingWindow) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

        const ImVec2 mouseDelta = ImGui::GetIO().MouseDelta;

        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
            ImVec2 newWindowPosition = ImVec2(
                windowPosition.x + mouseDelta.x,
                windowPosition.y + mouseDelta.y
            );

            const float dragMaximumX = std::max(viewportLeft, viewportRight - windowSize.x);

            const float dragMaximumY = std::max(viewportTop, viewportBottom - windowSize.y);

            newWindowPosition.x = std::clamp(newWindowPosition.x, viewportLeft, dragMaximumX);

            newWindowPosition.y = std::clamp(newWindowPosition.y, viewportTop, dragMaximumY);

            ImGui::SetWindowPos(newWindowPosition, ImGuiCond_Always);

            windowPosition = newWindowPosition;
        }
    }
    else if (dragRegionHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    const bool showWindowControls =
        canvasHovered ||
        dragRegionHovered ||
        draggingWindow ||
        settingsButtonHovered ||
        closeButtonHovered ||
        settingsPopupOpen;

    const auto DrawWindowButton = [&](const ImVec2& minimum, const ImVec2& maximum, const char* text, bool hovered, bool isCloseButton) 
        {
            ImU32 backgroundColour;

            if (isCloseButton && hovered) {
                backgroundColour = IM_COL32(160, 45, 45, 245);
            }
            else if (hovered) {
                backgroundColour = IM_COL32(70, 70, 78, 245);
            }
            else {
                backgroundColour = IM_COL32(30, 30, 35, 215);
            }

            const ImU32 borderColour = hovered
                ? IM_COL32(150, 150, 160, 255)
                : IM_COL32(90, 90, 100, 255);

            drawList->AddRectFilled(
                minimum,
                maximum,
                backgroundColour,
                3.0f
            );

            drawList->AddRect(
                minimum,
                maximum,
                borderColour,
                3.0f,
                0,
                1.0f
            );

            const ImVec2 textSize = ImGui::CalcTextSize(text);

            const ImVec2 textPosition = ImVec2(
                minimum.x +
                ((maximum.x - minimum.x) - textSize.x) * 0.5f,
                minimum.y +
                ((maximum.y - minimum.y) - textSize.y) * 0.5f
            );

            drawList->AddText(textPosition, IM_COL32(235, 235, 240, 255), text);
        };

    if (showWindowControls) {
        DrawWindowButton(settingsButtonMinimum, settingsButtonMaximum, "...", settingsButtonHovered, false);
        DrawWindowButton(closeButtonMinimum, closeButtonMaximum, "X", closeButtonHovered, true);

        constexpr float gripWidth = 28.0f;

        const float gripCentreX = canvasPosition_.x + canvasSize_.x * 0.5f;

        drawList->AddLine(
            ImVec2(gripCentreX - gripWidth * 0.5f, canvasPosition_.y + 7.0f),
            ImVec2(gripCentreX + gripWidth * 0.5f, canvasPosition_.y + 7.0f),
            IM_COL32(150, 150, 155, 170),
            2.0f
        );
    }

    if (settingsButtonHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Settings");
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            ImGui::OpenPopup("##AimViewSettingsPopup");
        }
    }

    bool closeRequested = false;

    if (closeButtonHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::BeginTooltip();
        ImGui::TextUnformatted("Close");
        ImGui::EndTooltip();

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            closeRequested = true;
        }
    }

    constexpr float popupWidth = 300.0f;
    constexpr float popupHeight = 152.0f;
    constexpr float popupSpacing = 6.0f;

    ImVec2 popupPosition = ImVec2(
        windowPosition.x + windowSize.x + popupSpacing,
        windowPosition.y + windowSize.y - popupHeight
    );

    if (popupPosition.x + popupWidth > viewportRight) {
        popupPosition.x =
            windowPosition.x -
            popupWidth -
            popupSpacing;
    }

    const float maximumPopupX = std::max(
        viewportLeft,
        viewportRight - popupWidth
    );

    const float maximumPopupY = std::max(
        viewportTop,
        viewportBottom - popupHeight
    );

    popupPosition.x = std::clamp(
        popupPosition.x,
        viewportLeft,
        maximumPopupX
    );

    popupPosition.y = std::clamp(
        popupPosition.y,
        viewportTop,
        maximumPopupY
    );

    ImGui::SetNextWindowViewport(mainViewport->ID);

    ImGui::SetNextWindowPos(popupPosition, ImGuiCond_Always);

    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);

    constexpr ImGuiWindowFlags popupFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginPopup("##AimViewSettingsPopup", popupFlags)) {
        ImGui::TextUnformatted(
            "Aim View Settings"
        );

        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("Visibility, styles and ranges use ESP settings.");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Zoom");

        constexpr float zoomControlWidth = 105.0f;
        const float zoomPositionX = ImGui::GetWindowContentRegionMax().x - zoomControlWidth;

        ImGui::SameLine();

        if (ImGui::GetCursorPosX() < zoomPositionX) {
            ImGui::SetCursorPosX(zoomPositionX);
        }

        ImGui::SetNextItemWidth(zoomControlWidth);

        ImGui::DragFloat("##AimViewZoom", &config_.zoom,
            0.1f,
            config_.minimumZoom,
            config_.maximumZoom,
            "%.1fx",
            ImGuiSliderFlags_AlwaysClamp
        );

        ImGui::Spacing();

        if (ImGui::Button("Reset zoom", ImVec2(-1.0f, 0.0f))) {
            config_.zoom = 2.5f;
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        ImGui::EndPopup();
    }

    ImGui::End();

    if (closeRequested)
        config_.enabled = false;
}

void AimViewWidget::UpdateTransform(const ImVec2& sourceResolution) {
    transformValid_ = false;

    if (sourceResolution.x <= 0.0f ||
        sourceResolution.y <= 0.0f ||
        canvasSize_.x <= 0.0f ||
        canvasSize_.y <= 0.0f) {
        return;
    }

    sourceResolution_ = sourceResolution;

    sourceCentre_ = ImVec2(
        sourceResolution_.x * 0.5f,
        sourceResolution_.y * 0.5f
    );

    canvasCentre_ = ImVec2(
        canvasPosition_.x + canvasSize_.x * 0.5f,
        canvasPosition_.y + canvasSize_.y * 0.5f
    );

    const float horizontalScale =
        canvasSize_.x / sourceResolution_.x;

    const float verticalScale =
        canvasSize_.y / sourceResolution_.y;

    const float fitScale = std::min(
        horizontalScale,
        verticalScale
    );

    transformScale_ = fitScale * config_.zoom;
    transformValid_ = transformScale_ > 0.0f;
}

bool AimViewWidget::MapScreenPoint(const ImVec2& sourcePoint, ImVec2& widgetPoint, float padding) const {
    widgetPoint = {};

    if (!transformValid_)
        return false;

    const ImVec2 sourceOffset = ImVec2(
        sourcePoint.x - sourceCentre_.x,
        sourcePoint.y - sourceCentre_.y
    );

    widgetPoint = ImVec2(
        canvasCentre_.x + sourceOffset.x * transformScale_,
        canvasCentre_.y + sourceOffset.y * transformScale_
    );

    const float minimumX = canvasPosition_.x - padding;
    const float minimumY = canvasPosition_.y - padding;

    const float maximumX =
        canvasPosition_.x + canvasSize_.x + padding;

    const float maximumY =
        canvasPosition_.y + canvasSize_.y + padding;

    return
        widgetPoint.x >= minimumX &&
        widgetPoint.y >= minimumY &&
        widgetPoint.x <= maximumX &&
        widgetPoint.y <= maximumY;
}

float AimViewWidget::MapScreenLength(float sourceLength) const {
    if (!transformValid_)
        return 0.0f;

    return sourceLength * transformScale_;
}

void AimViewWidget::DrawBackground(ImDrawList* drawList) const {
    if (!drawList)
        return;

    const ImVec2 maximum = ImVec2(
        canvasPosition_.x + canvasSize_.x,
        canvasPosition_.y + canvasSize_.y
    );

    drawList->AddRectFilled(
        canvasPosition_,
        maximum,
        IM_COL32(10, 10, 12, 235),
        4.0f
    );

    drawList->AddRect(
        canvasPosition_,
        maximum,
        IM_COL32(80, 80, 90, 255),
        4.0f,
        0,
        1.0f
    );
}

void AimViewWidget::DrawCrosshair(ImDrawList* drawList) const {
    if (!drawList)
        return;

    const float size = static_cast<float>(std::clamp(espGlobals::crosshairSize, 1, 20));
    const ImU32 colour = ImGui::ColorConvertFloat4ToU32(ImVec4(
        coloursGlobals::crosshair.r,
        coloursGlobals::crosshair.g,
        coloursGlobals::crosshair.b,
        coloursGlobals::crosshair.a
    ));

    if (espGlobals::crosshairType == 1) {
        drawList->AddLine(
            ImVec2(canvasCentre_.x - size, canvasCentre_.y),
            ImVec2(canvasCentre_.x + size, canvasCentre_.y),
            colour,
            1.0f
        );
        drawList->AddLine(
            ImVec2(canvasCentre_.x, canvasCentre_.y - size),
            ImVec2(canvasCentre_.x, canvasCentre_.y + size),
            colour,
            1.0f
        );
        return;
    }

    drawList->AddCircle(canvasCentre_, size, colour, 0, 1.0f);
}

void AimViewWidget::DrawPlayers(ImDrawList* drawList) {
    if (!drawList)
        return;

    const PlayerSnapshot cacheSnapshot = registeredPlayers.getCacheSnapshot();
    const PlayerCollection& cache = *cacheSnapshot;

    if (cache.empty())
        return;

    const float maximumDistance = static_cast<float>(espGlobals::drawPlayerDist);

    for (const Player& player : cache) {
        if (!Utils::valid_pointer(player.instance) ||
            player.isZombie ||
            player.isLocal ||
            player.hasExfiled ||
            player.isDead ||
            player.isBTR) {
            continue;
        }

        if (player.distance <= 0 ||
            static_cast<float>(player.distance) > maximumDistance) {
            continue;
        }

        glm::vec2 sourcePlayerPosition{};

        if (!Utils::Camera::world_to_screen(player.location, &sourcePlayerPosition)) {
            continue;
        }

        if (sourcePlayerPosition.x == 0.0f && sourcePlayerPosition.y == 0.0f) {
            continue;
        }

        ImVec2 widgetPlayerPosition{};

        if (!MapScreenPoint(ImVec2(sourcePlayerPosition.x, sourcePlayerPosition.y), widgetPlayerPosition, 100.0f)) {
            continue;
        }

        const glm::vec4& playerColour = player.colour;

        const ImU32 drawColour = ImGui::ColorConvertFloat4ToU32(ImVec4(
                    playerColour.r,
                    playerColour.g,
                    playerColour.b,
                    playerColour.a
                )
            );

        ImVec2 mappedHeadPosition{};
        bool headVisible = false;

        if (HasBones(player, { boneListIndexes::Head })) {
            glm::vec2 sourceHeadPosition{};

            if (Utils::Camera::world_to_screen(player.bonePositions[boneListIndexes::Head], &sourceHeadPosition) &&
                sourceHeadPosition.x != 0.0f &&
                sourceHeadPosition.y != 0.0f) {
                headVisible = MapScreenPoint(
                    ImVec2(sourceHeadPosition.x, sourceHeadPosition.y),
                    mappedHeadPosition,
                    100.0f
                );
            }
        }

        const bool skeletonDrawn = espGlobals::drawSkeletons &&
            DrawPlayerSkeleton(drawList, player, drawColour, mappedHeadPosition);

        if (espGlobals::drawBoxPlayers && headVisible) {
            const float height = widgetPlayerPosition.y - mappedHeadPosition.y + 2.0f;
            const float width = height * 0.5f + 2.0f;

            if (height > 4.0f && width > 2.0f) {
                const ImVec2 boxMinimum = ImVec2(
                    widgetPlayerPosition.x - width * 0.5f - 2.0f,
                    widgetPlayerPosition.y - height
                );
                const ImVec2 boxMaximum = ImVec2(
                    boxMinimum.x + width,
                    boxMinimum.y + height
                );

                drawList->AddRect(boxMinimum, boxMaximum, drawColour, 0.0f, 0, 1.0f);
            }
        }

        if (espGlobals::drawHeadDot && headVisible) {
            const float height = std::max(4.0f, widgetPlayerPosition.y - mappedHeadPosition.y + 2.0f);
            const float radius = std::max(1.0f, (height / 12.0f) * (espGlobals::headDotSize / 5.0f));
            const ImVec2 dotPosition = ImVec2(mappedHeadPosition.x, mappedHeadPosition.y - height / 20.0f);

            drawList->AddCircle(dotPosition, radius, drawColour, 0, 1.0f);
        }

        if (!skeletonDrawn && !espGlobals::drawBoxPlayers && !espGlobals::drawHeadDot)
            drawList->AddCircleFilled(widgetPlayerPosition, 2.5f, drawColour);

        const ImVec2 labelAnchor = skeletonDrawn || headVisible
            ? mappedHeadPosition
            : widgetPlayerPosition;

        const bool showName =
            player.isBoss ||
            player.isPlayer ||
            player.isPlayerScav;

        if (!showName)
            continue;

        std::string cleanName = CleanText(player.name);

        if (cleanName.empty()) {
            cleanName = player.isBoss
                ? "Boss"
                : "Player";
        }

        const std::string label = cleanName;

        constexpr float fontSize = 12.0f;

        const ImVec2 textSize =
            ImGui::GetFont()->CalcTextSizeA(
                fontSize,
                FLT_MAX,
                0.0f,
                label.c_str()
            );


        const ImVec2 textPosition = ImVec2(labelAnchor.x - textSize.x * 0.5f, labelAnchor.y - textSize.y - 4.0f);

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(
                textPosition.x + 1.0f,
                textPosition.y + 1.0f
            ),
            IM_COL32(0, 0, 0, 220),
            label.c_str()
        );

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            textPosition,
            drawColour,
            label.c_str()
        );
    }
}

bool AimViewWidget::DrawPlayerSkeleton(ImDrawList* drawList, const Player& player, ImU32 colour, ImVec2& outHeadPosition)
{
    if (!drawList)
        return false;

    if (!HasBones(
        player,
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
        }
    )) {
        return false;
    }

    const auto ProjectBone = [&](int boneIndex, ImVec2& output ) -> bool {
            if (boneIndex < 0)
                return false;

            const size_t index = static_cast<size_t>(boneIndex);

            if (index >= player.bonePositions.size())
                return false;

            glm::vec2 sourcePosition{};

            if (!Utils::Camera::world_to_screen(player.bonePositions[index], &sourcePosition)) {
                return false;
            }

            if (sourcePosition.x == 0.0f && sourcePosition.y == 0.0f) {
                return false;
            }

            return MapScreenPoint(
                ImVec2(sourcePosition.x, sourcePosition.y),
                output,
                100.0f
            );
        };

    ImVec2 head{};
    ImVec2 pelvis{};

    ImVec2 leftForearm{};
    ImVec2 leftPalm{};

    ImVec2 rightForearm{};
    ImVec2 rightPalm{};

    ImVec2 leftThigh{};
    ImVec2 leftFoot{};

    ImVec2 rightThigh{};
    ImVec2 rightFoot{};

    if (!ProjectBone(
        boneListIndexes::Head,
        head
    ) ||
        !ProjectBone(
            boneListIndexes::Pelvis,
            pelvis
        ) ||
        !ProjectBone(
            boneListIndexes::LForearm,
            leftForearm
        ) ||
        !ProjectBone(
            boneListIndexes::LPalm,
            leftPalm
        ) ||
        !ProjectBone(
            boneListIndexes::RForearm,
            rightForearm
        ) ||
        !ProjectBone(
            boneListIndexes::RPalm,
            rightPalm
        ) ||
        !ProjectBone(
            boneListIndexes::LThigh,
            leftThigh
        ) ||
        !ProjectBone(
            boneListIndexes::LFoot,
            leftFoot
        ) ||
        !ProjectBone(
            boneListIndexes::RThigh,
            rightThigh
        ) ||
        !ProjectBone(
            boneListIndexes::RFoot,
            rightFoot
        )) {
        return false;
    }

    constexpr float lineThickness = 1.0f;

    const auto DrawBoneLine = [&](const ImVec2& start, const ImVec2& end) {
            
            drawList->AddLine(
                start,
                end,
                IM_COL32(0, 0, 0, 190),
                lineThickness + 1.0f
            );

            drawList->AddLine(
                start,
                end,
                colour,
                lineThickness
            );
        };

    DrawBoneLine(
        head,
        pelvis
    );

    DrawBoneLine(
        head,
        leftForearm
    );

    DrawBoneLine(
        leftForearm,
        leftPalm
    );

    DrawBoneLine(
        head,
        rightForearm
    );

    DrawBoneLine(
        rightForearm,
        rightPalm
    );

    DrawBoneLine(
        pelvis,
        leftThigh
    );

    DrawBoneLine(
        leftThigh,
        leftFoot
    );

    DrawBoneLine(
        pelvis,
        rightThigh
    );

    DrawBoneLine(
        rightThigh,
        rightFoot
    );

    outHeadPosition = head;
    return true;
}

void AimViewWidget::DrawLoot(ImDrawList* drawList) {
    if (!drawList)
        return;

    const std::vector<LootEntity> lootList = Loot.getCacheLoot();

    for (const LootEntity& loot : lootList) {
        if (loot.pendingResolve ||
            loot.failed ||
            !loot.hasValidPosition ||
            !loot.wanted) {
            continue;
        }

        const bool isQuestItem = loot.isQuestItem();
        const bool isCorpse = loot.isCorpse();
        const bool isRegularLoot = loot.isItem() || isQuestItem;

        if (!isRegularLoot && !isCorpse) {
            continue;
        }

        if (loot.isContainer() ||
            loot.isAirdrop()) {
            continue;
        }

        if (isCorpse && !espGlobals::drawCorpse)
            continue;

        if (!isCorpse && !espGlobals::drawLoot)
            continue;

        if (isQuestItem && !espGlobals::drawQuestHelper)
            continue;

        const float distance = glm::distance(mainGame.localLocation, loot.worldLocation);

        const float maximumDistance = static_cast<float>(
            isCorpse
            ? espGlobals::drawCorpseDist
            : espGlobals::drawLootDist);

        if (distance <= 0.0f || distance > maximumDistance) {
            continue;
        }

        glm::vec2 sourceScreenPosition{};

        if (!Utils::Camera::world_to_screen(loot.worldLocation, &sourceScreenPosition)) {
            continue;
        }

        if (sourceScreenPosition.x == 0.0f && sourceScreenPosition.y == 0.0f) {
            continue;
        }

        ImVec2 widgetPosition{};

        if (!MapScreenPoint(ImVec2(sourceScreenPosition.x, sourceScreenPosition.y), widgetPosition, 100.0f)) {
            continue;
        }

        const glm::vec4& lootColour = isQuestItem
            ? coloursGlobals::questMarker
            : loot.color;

        const ImU32 drawColour = ImGui::ColorConvertFloat4ToU32(ImVec4(
                    lootColour.r,
                    lootColour.g,
                    lootColour.b,
                    lootColour.a
                )
            );

        std::string lootName = CleanText(GetLootDisplayName(loot));

        if (isCorpse)
        {
            const std::string ownerName = CleanText(
                loot.getCorpseState().ownerName);

            if (!ownerName.empty())
                lootName += "\n" + ownerName;
        }

        if (lootName.empty())
            continue;

        constexpr float dotRadius = 2.5f;
        constexpr float fontSize = 11.0f;
        constexpr float textSpacing = 4.0f;

        drawList->AddCircleFilled(
            widgetPosition,
            dotRadius + 1.0f,
            IM_COL32(0, 0, 0, 210)
        );

        drawList->AddCircleFilled(
            widgetPosition,
            dotRadius,
            drawColour
        );

        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(
                fontSize,
                FLT_MAX,
                0.0f,
                lootName.c_str()
            );

        const ImVec2 textPosition = ImVec2(
            widgetPosition.x -
            textSize.x * 0.5f,

            widgetPosition.y +
            dotRadius +
            textSpacing
        );

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(
                textPosition.x + 1.0f,
                textPosition.y + 1.0f
            ),
            IM_COL32(0, 0, 0, 220),
            lootName.c_str()
        );

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            textPosition,
            drawColour,
            lootName.c_str()
        );
    }
}

void AimViewWidget::DrawContainers(ImDrawList* drawList) {
    if (!drawList)
        return;

    const std::vector<LootEntity> lootList = Loot.getCacheLoot();

    for (const LootEntity& loot : lootList) {
        if (loot.pendingResolve ||
            loot.failed ||
            !loot.hasValidPosition ||
            !loot.wanted) {
            continue;
        }

        const bool isContainerLike = loot.isContainer() || loot.isAirdrop();

        if (!isContainerLike)
            continue;

        if (loot.isItem() ||
            loot.isQuestItem() ||
            loot.isCorpse()) {
            continue;
        }

        const float distance = glm::distance(mainGame.localLocation, loot.worldLocation);

        if (distance <= 0.0f || distance > static_cast<float>(lootGlobals::containerDistance)) {
            continue;
        }

        glm::vec2 sourceScreenPosition{};

        if (!Utils::Camera::world_to_screen(loot.worldLocation, &sourceScreenPosition)) {
            continue;
        }

        if (sourceScreenPosition.x == 0.0f && sourceScreenPosition.y == 0.0f) {
            continue;
        }

        ImVec2 widgetPosition{};

        if (!MapScreenPoint(ImVec2(sourceScreenPosition.x, sourceScreenPosition.y),
            widgetPosition,
            100.0f
        )) {
            continue;
        }

        const glm::vec4& containerColour = loot.color;

        const ImU32 drawColour = ImGui::ColorConvertFloat4ToU32(
                ImVec4(
                    containerColour.r,
                    containerColour.g,
                    containerColour.b,
                    containerColour.a
                )
            );

        std::string containerName =
            CleanText(loot.shortName);

        if (containerName.empty()) {
            containerName = loot.isAirdrop()
                ? "Airdrop"
                : "Container";
        }

        constexpr float dotRadius = 3.0f;
        constexpr float fontSize = 11.0f;
        constexpr float textSpacing = 4.0f;

        drawList->AddCircleFilled(
            widgetPosition,
            dotRadius + 1.0f,
            IM_COL32(0, 0, 0, 210)
        );

        drawList->AddCircleFilled(
            widgetPosition,
            dotRadius,
            drawColour
        );

        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(
                fontSize,
                FLT_MAX,
                0.0f,
                containerName.c_str()
            );

        const ImVec2 textPosition = ImVec2(
            widgetPosition.x - textSize.x * 0.5f,
            widgetPosition.y + dotRadius + textSpacing
        );

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            ImVec2(
                textPosition.x + 1.0f,
                textPosition.y + 1.0f
            ),
            IM_COL32(0, 0, 0, 220),
            containerName.c_str()
        );

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            textPosition,
            drawColour,
            containerName.c_str()
        );
    }
}
