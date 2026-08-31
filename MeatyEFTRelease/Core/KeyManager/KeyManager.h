#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>

enum class WindowsKey : uint32_t
{
    Mouse0 = VK_LBUTTON,
    Mouse1 = VK_RBUTTON,
    Mouse2 = VK_MBUTTON,
    Mouse3 = VK_XBUTTON1,
    Mouse4 = VK_XBUTTON2,
    LeftControl = VK_LCONTROL,
    LeftAlt = VK_LMENU,
    LeftShift = VK_LSHIFT,
    Enter = VK_RETURN,
    F11 = VK_F11,
    F12 = VK_F12,
};

namespace keyManager
{
    bool DrawKeyBindingRow(WindowsKey& selectedKey, const char* label);
}
