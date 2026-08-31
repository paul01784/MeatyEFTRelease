#include "KeyManager.h"

#include "../../external/imgui/imgui.h"
#include "../../memory/Memory.h"
#include "../../UI/globals.h"
#include "../../UI/menuLayout.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace
{
    constexpr WindowsKey PresetKeys[] =
    {
        WindowsKey::Mouse0,
        WindowsKey::Mouse1,
        WindowsKey::Mouse2,
        WindowsKey::Mouse3,
        WindowsKey::Mouse4,
        WindowsKey::LeftControl,
        WindowsKey::LeftAlt,
        WindowsKey::LeftShift,
        WindowsKey::Enter,
        WindowsKey::F11,
        WindowsKey::F12,
    };

    const char* PresetKeyName(WindowsKey key)
    {
        switch (key)
        {
        case WindowsKey::LeftControl: return "Left Control";
        case WindowsKey::LeftAlt: return "Left Alt";
        case WindowsKey::LeftShift: return "Left Shift";
        case WindowsKey::Mouse0: return "Mouse 0";
        case WindowsKey::Mouse1: return "Mouse 1";
        case WindowsKey::Mouse2: return "Mouse 2";
        case WindowsKey::Mouse3: return "Mouse 3";
        case WindowsKey::Mouse4: return "Mouse 4";
        case WindowsKey::Enter: return "Enter";
        case WindowsKey::F11: return "F11";
        case WindowsKey::F12: return "F12";
        default: return nullptr;
        }
    }

    std::string VirtualKeyName(uint32_t virtualKeyCode)
    {
        if (const char* presetName = PresetKeyName(static_cast<WindowsKey>(virtualKeyCode)))
            return presetName;

        switch (virtualKeyCode)
        {
        case VK_RCONTROL: return "Right Control";
        case VK_RMENU: return "Right Alt";
        case VK_RSHIFT: return "Right Shift";
        default:
            break;
        }

        if (virtualKeyCode >= 256)
            return "Unknown";

        const UINT scanCode = MapVirtualKeyA(virtualKeyCode, MAPVK_VK_TO_VSC_EX);
        if (scanCode != 0)
        {
            LONG keyNameData = static_cast<LONG>((scanCode & 0xFFu) << 16);
            if ((scanCode & 0xFF00u) == 0xE000u)
                keyNameData |= 1 << 24;

            char keyName[128]{};
            if (GetKeyNameTextA(
                keyNameData,
                keyName,
                static_cast<int>(sizeof(keyName))) > 0)
            {
                return keyName;
            }
        }

        char fallback[16]{};
        sprintf_s(fallback, "VK 0x%02X", virtualKeyCode);
        return fallback;
    }
}

bool keyManager::DrawKeyBindingRow(WindowsKey& selectedKey, const char* label)
{
    bool changed = false;
    const std::string preview = VirtualKeyName(static_cast<uint32_t>(selectedKey));
    c_keys* keyboard = mem.GetKeyboard();
    const bool captureAvailable =
        memoryGlobals::dmaConnected.load(std::memory_order_acquire) &&
        keyboard &&
        keyboard->IsReady();

    ImGui::PushID(label);

    const float rowStartX = ImGui::GetCursorPosX();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float controlX = menuLayout::ControlColumnX(rowStartX, rowWidth);
    const float customButtonWidth = ImGui::CalcTextSize("Custom").x +
        (ImGui::GetStyle().FramePadding.x * 2.0f);
    const float comboWidth = std::clamp(
        rowWidth - (controlX - rowStartX) - customButtonWidth - 8.0f,
        120.0f,
        220.0f);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), controlX));
    ImGui::SetNextItemWidth(comboWidth);

    if (ImGui::BeginCombo("##selection", preview.c_str()))
    {
        for (const WindowsKey key : PresetKeys)
        {
            const bool isSelected = selectedKey == key;
            if (ImGui::Selectable(PresetKeyName(key), isSelected))
            {
                selectedKey = key;
                changed = true;
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine(0.0f, 8.0f);
    ImGui::BeginDisabled(!captureAvailable);
    const bool customClicked = ImGui::Button("Custom");
    ImGui::EndDisabled();

    if (!captureAvailable &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("Connect DMA or Input Manager Failed");
    }

    if (customClicked)
    {
        keyboard->BeginKeyCapture();
        ImGui::OpenPopup("Custom hotkey##capture");
    }

    if (ImGui::BeginPopupModal(
        "Custom hotkey##capture",
        nullptr,
        ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Press Key on main PC");
        ImGui::TextDisabled("Binding: %s", label);
        ImGui::Spacing();

        if (captureAvailable)
        {
            ImGui::TextDisabled("Waiting for a new key press...");
            const uint32_t pressedKey = keyboard->GetFirstPressedKey();
            if (pressedKey != 0)
            {
                selectedKey = static_cast<WindowsKey>(pressedKey);
                changed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.94f, 0.36f, 0.36f, 1.0f),
                "Connect DMA or Input Manager Failed");
        }

        ImGui::Spacing();
        if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}
