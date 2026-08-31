#pragma once

#include "../external/imgui/imgui.h"
#include "IconsFontAwesomeCompat.h"

#include <algorithm>
#include <string>

namespace menuLayout
{
    inline constexpr float ContentInset = 12.0f;
    inline constexpr float WidgetToolbarX = 10.0f;
    inline constexpr float WidgetToolbarY = 10.0f;
    inline constexpr float WidgetToolbarButtonSize = 40.0f;
    inline constexpr float WidgetToolbarGap = 6.0f;

    inline ImVec2 TopLeftWidgetPosition()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        return ImVec2(
            viewport->Pos.x + WidgetToolbarX,
            viewport->Pos.y + WidgetToolbarY + WidgetToolbarButtonSize + WidgetToolbarGap);
    }

    inline void PushContentInset()
    {
        ImGui::Indent(ContentInset);
    }

    inline void PopContentInset()
    {
        ImGui::Unindent(ContentInset);
    }

    inline float ControlColumnX(float rowStartX, float availableWidth)
    {
        const float controlOffset = std::clamp(availableWidth * 0.40f, 135.0f, 170.0f);
        return rowStartX + controlOffset;
    }

    inline float ControlRightX(float rowStartX, float availableWidth)
    {
        const float controlX = ControlColumnX(rowStartX, availableWidth);
        const float availableAfterControl = availableWidth - (controlX - rowStartX);
        const float controlWidth = std::clamp(availableAfterControl, 120.0f, 220.0f);
        return controlX + controlWidth;
    }

    inline bool Section(const char* label, ImGuiTreeNodeFlags flags = 0)
    {
        (void)flags;
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float labelWidth = ImGui::CalcTextSize(label).x;
        const float gap = 8.0f;
        const float lineWidth = std::max(0.0f, (availableWidth - labelWidth - (gap * 2.0f)) * 0.5f);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float lineY = start.y + (ImGui::GetTextLineHeight() * 0.5f);
        const ImVec4 headingColour(0.94f, 0.22f, 0.25f, 1.0f);
        const ImU32 lineColour = ImGui::GetColorU32(ImVec4(0.62f, 0.12f, 0.15f, 0.58f));

        if (lineWidth > 0.0f)
        {
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(start.x, lineY),
                ImVec2(start.x + lineWidth, lineY),
                lineColour
            );
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(start.x + lineWidth + gap + labelWidth + gap, lineY),
                ImVec2(start.x + availableWidth, lineY),
                lineColour
            );
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + lineWidth + gap);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, headingColour);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        return true;
    }

    inline bool InlineToggle(
        const char* label,
        const char* id,
        bool* value,
        bool alignToColumn = true,
        bool enabled = true)
    {
        ImGui::PushID(id);
        ImGui::BeginDisabled(!enabled);
        const float rowStartX = ImGui::GetCursorPosX();
        const float controlX = ControlColumnX(rowStartX, ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, 6.0f);
        if (alignToColumn)
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        const bool changed = ImGui::Checkbox("##toggle", value);
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    }

    inline bool ToggleRow(const char* label, const char* id, bool* value)
    {
        return InlineToggle(label, id, value);
    }

    inline bool SliderIntRow(
        const char* label,
        const char* id,
        int* value,
        int minValue,
        int maxValue,
        const char* format = "%d",
        bool enabled = true
    )
    {
        ImGui::PushID(id);
        ImGui::BeginDisabled(!enabled);
        const float rowStartX = ImGui::GetCursorPosX();
        const float controlX = ControlColumnX(rowStartX, ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x, 120.0f, 220.0f));
        const bool changed = ImGui::SliderInt("##slider", value, minValue, maxValue, format);
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    }

    inline bool SliderFloatRow(
        const char* label,
        const char* id,
        float* value,
        float minValue,
        float maxValue,
        const char* format = "%.2f"
    )
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float controlX = ControlColumnX(rowStartX, ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x, 120.0f, 220.0f));
        const bool changed = ImGui::SliderFloat("##slider", value, minValue, maxValue, format);
        ImGui::PopID();
        return changed;
    }

    inline bool ColourRow(const char* label, const char* id, float* colour)
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float controlX = ControlColumnX(rowStartX, ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        const bool changed = ImGui::ColorEdit4(
            "##colour",
            colour,
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs
        );
        ImGui::PopID();
        return changed;
    }

    inline bool TogglePairRow(
        const char* firstLabel,
        const char* firstId,
        bool* firstValue,
        const char* secondLabel,
        const char* secondId,
        bool* secondValue,
        bool enabled = true
    )
    {
        const bool firstChanged = InlineToggle(firstLabel, firstId, firstValue, true, enabled);
        ImGui::SameLine(0.0f, 12.0f);
        const bool secondChanged = InlineToggle(secondLabel, secondId, secondValue, false, enabled);
        return firstChanged || secondChanged;
    }

    inline bool AlignedTogglePairRow(
        const char* firstLabel,
        const char* firstId,
        bool* firstValue,
        const char* secondLabel,
        const char* secondId,
        bool* secondValue,
        bool firstEnabled = true,
        bool secondEnabled = true)
    {
        const float rowStartX = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float firstControlX = ControlColumnX(rowStartX, rowWidth);
        const float rowEndX = ControlRightX(rowStartX, rowWidth);
        const float secondLabelX = firstControlX + ImGui::GetFrameHeight() + 12.0f;
        const float secondControlX = rowEndX - ImGui::GetFrameHeight();

        ImGui::PushID(firstId);
        ImGui::BeginDisabled(!firstEnabled);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(firstLabel);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), firstControlX));
        bool changed = ImGui::Checkbox("##toggle", firstValue);
        ImGui::EndDisabled();
        ImGui::PopID();

        ImGui::SameLine(0.0f, 12.0f);
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), secondLabelX));
        ImGui::PushID(secondId);
        ImGui::BeginDisabled(!secondEnabled);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(secondLabel);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), secondControlX));
        changed |= ImGui::Checkbox("##toggle", secondValue);
        ImGui::EndDisabled();
        ImGui::PopID();

        return changed;
    }

    inline bool RightIntSliderRow(
        const char* label,
        const char* id,
        int* value,
        int minValue,
        int maxValue,
        const char* format = "%d",
        bool leftAlignLabel = false)
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float controlX = ControlColumnX(rowStartX, rowWidth);
        const float rowEndX = ControlRightX(rowStartX, rowWidth);
        const float inlineLabelX = controlX + ImGui::GetFrameHeight() + 12.0f;
        const float sliderX = inlineLabelX + ImGui::CalcTextSize("Range").x + 6.0f;
        const float labelWidth = ImGui::CalcTextSize(label).x;

        ImGui::AlignTextToFramePadding();
        const float labelX = leftAlignLabel
            ? inlineLabelX
            : sliderX - labelWidth - 6.0f;
        ImGui::SetCursorPosX((std::max)(rowStartX, labelX));
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(sliderX);
        ImGui::SetNextItemWidth((std::max)(1.0f, rowEndX - sliderX));
        const bool changed = ImGui::SliderInt(
            "##range",
            value,
            minValue,
            maxValue,
            format);
        ImGui::PopID();
        return changed;
    }

    inline bool LeftLabelRightIntSliderRow(
        const char* leftLabel,
        const char* inlineLabel,
        const char* id,
        int* value,
        int minValue,
        int maxValue,
        const char* format = "%d")
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float controlX = ControlColumnX(rowStartX, rowWidth);
        const float rowEndX = ControlRightX(rowStartX, rowWidth);
        const float inlineLabelX = controlX + ImGui::GetFrameHeight() + 12.0f;
        const float sliderX = inlineLabelX + ImGui::CalcTextSize("Range").x + 6.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(leftLabel);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), inlineLabelX));
        ImGui::TextDisabled("%s", inlineLabel);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(sliderX);
        ImGui::SetNextItemWidth((std::max)(1.0f, rowEndX - sliderX));
        const bool changed = ImGui::SliderInt(
            "##range",
            value,
            minValue,
            maxValue,
            format);
        ImGui::PopID();
        return changed;
    }

    inline bool ToggleIntSliderRow(
        const char* label,
        const char* id,
        bool* toggleValue,
        const char* rangeLabel,
        int* sliderValue,
        int minValue,
        int maxValue,
        const char* format = "%d",
        bool disableSliderWhenOff = false
    )
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float controlX = ControlColumnX(rowStartX, rowWidth);
        const float rowEndX = ControlRightX(rowStartX, rowWidth);
        const float inlineLabelWidth = (std::max)(
            ImGui::CalcTextSize("Range").x,
            ImGui::CalcTextSize("Loose").x);
        const float sliderX = controlX + ImGui::GetFrameHeight() + 12.0f +
            inlineLabelWidth + 6.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        bool changed = ImGui::Checkbox("##toggle", toggleValue);
        ImGui::BeginDisabled(disableSliderWhenOff && !*toggleValue);
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::TextDisabled("%s", rangeLabel);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(sliderX);
        ImGui::SetNextItemWidth(std::max(1.0f, rowEndX - sliderX));
        changed |= ImGui::SliderInt("##range", sliderValue, minValue, maxValue, format);
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    }

    inline bool ToggleFloatSliderRow(
        const char* label,
        const char* id,
        bool* toggleValue,
        const char* valueLabel,
        float* sliderValue,
        float minValue,
        float maxValue,
        const char* format = "%.1f",
        bool disableSliderWhenOff = false,
        bool alignSliderToRange = false)
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float controlX = ControlColumnX(rowStartX, rowWidth);
        const float rowEndX = ControlRightX(rowStartX, rowWidth);
        const float inlineLabelWidth = alignSliderToRange
            ? ImGui::CalcTextSize("Range").x
            : ImGui::CalcTextSize(valueLabel).x;
        const float sliderX = controlX + ImGui::GetFrameHeight() + 12.0f +
            inlineLabelWidth + 6.0f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        bool changed = ImGui::Checkbox("##toggle", toggleValue);
        ImGui::BeginDisabled(disableSliderWhenOff && !*toggleValue);
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::TextDisabled("%s", valueLabel);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(sliderX);
        ImGui::SetNextItemWidth(std::max(1.0f, rowEndX - sliderX));
        changed |= ImGui::SliderFloat(
            "##value",
            sliderValue,
            minValue,
            maxValue,
            format);
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    }

    inline bool ToggleComboIntSliderRow(
        const char* label,
        const char* id,
        bool* toggleValue,
        const char* comboLabel,
        int* comboValue,
        const char* const comboItems[],
        int comboItemCount,
        const char* sliderLabel,
        int* sliderValue,
        int minValue,
        int maxValue,
        const char* format = "%d",
        bool disableOptionsWhenOff = false)
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float controlX = ControlColumnX(rowStartX, rowWidth);
        const float rowEndX = ControlRightX(rowStartX, rowWidth);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        bool changed = ImGui::Checkbox("##toggle", toggleValue);
        ImGui::BeginDisabled(disableOptionsWhenOff && !*toggleValue);
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("%s", comboLabel);
        ImGui::SameLine(0.0f, 5.0f);
        ImGui::SetNextItemWidth(66.0f);
        changed |= ImGui::Combo(
            "##combo",
            comboValue,
            comboItems,
            comboItemCount);
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextDisabled("%s", sliderLabel);
        ImGui::SameLine(0.0f, 5.0f);
        const float sliderX = ImGui::GetCursorPosX();
        ImGui::SetNextItemWidth(std::max(1.0f, rowEndX - sliderX));
        changed |= ImGui::SliderInt(
            "##slider",
            sliderValue,
            minValue,
            maxValue,
            format);
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    }

    inline bool ComboRow(
        const char* label,
        const char* id,
        int* value,
        const char* const items[],
        int itemCount)
    {
        ImGui::PushID(id);
        const float rowStartX = ImGui::GetCursorPosX();
        const float controlX = ControlColumnX(rowStartX, ImGui::GetContentRegionAvail().x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
        ImGui::SetNextItemWidth(std::clamp(ImGui::GetContentRegionAvail().x, 120.0f, 220.0f));
        const bool changed = ImGui::Combo("##combo", value, items, itemCount);
        ImGui::PopID();
        return changed;
    }

    inline bool BeginTwoColumns(const char* id)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(
            ImGuiStyleVar_CellPadding,
            ImVec2(18.0f, style.CellPadding.y)
        );

        const bool began = ImGui::BeginTable(
            id,
            2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV
        );
        if (!began)
            ImGui::PopStyleVar();
        return began;
    }

    inline void NextColumn()
    {
        ImGui::TableNextColumn();
    }

    inline void EndTwoColumns()
    {
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }

    inline bool TopTabButton(const char* icon, const char* label, bool selected)
    {
        const std::string buttonLabel = std::string(icon) + "  " + label;
        const ImVec4 activeColour = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
        const ImVec4 inactiveColour = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? activeColour : inactiveColour);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        const bool pressed = ImGui::Button(buttonLabel.c_str(), ImVec2(0.0f, 30.0f));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return pressed;
    }
}
