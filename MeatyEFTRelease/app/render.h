#pragma once
#include "../external/imgui/imgui.h"
#include "../external/imgui/backends/imgui_impl_dx9.h"
#include "../external/imgui/backends/imgui_impl_win32.h"
#include "../external/imgui/imgui_internal.h"
#include <d3d9.h>
#include <d3d9types.h>
#include <wtypes.h>
#include <d3dx9tex.h>
#include <tchar.h>
#include <windows.h>
#include "IconsForkAwesome.h"

// Data
static LPDIRECT3D9              g_pD3D = nullptr;
static LPDIRECT3DDEVICE9        g_pd3dDevice = nullptr;
static bool                     g_DeviceLost = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static D3DPRESENT_PARAMETERS    g_d3dpp = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void ResetDevice();
bool renderThread();
bool LoadTextureFromFile(const char* filename, PDIRECT3DTEXTURE9* out_texture, int* out_width, int* out_height);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

enum GameResolution
{
    RES_1080P = 0,
    RES_1440P,
    RES_3440X1440
};


enum boneListIndexes
{
    Pelvis,      // 0
    Head,        // 1
    Neck,        // 2
    Spine,       // 3
    LForearm,    // 4
    LPalm,       // 5
    RForearm,    // 6
    RPalm,       // 7
    LThigh,      // 8
    LFoot,       // 9
    RThigh,      // 10
    RFoot,       // 11
    Base         // 12
};

enum class WindowsKey {
    Mouse0 = VK_LBUTTON,
    Mouse1 = VK_RBUTTON,
    Mouse2 = VK_MBUTTON,
    Mouse3 = VK_XBUTTON1,
    Mouse4 = VK_XBUTTON2,
    LeftControl = VK_LCONTROL,
    LeftAlt = VK_LMENU,
    LeftShift = VK_LSHIFT,
    Enter = VK_RETURN,
    F12 = VK_F12,
};

enum class BoneList
{
    head = 133,
    neck = 132,
    pelvis = 14,
    hip = 14,
    leftarm = 91,
    rightarm = 112,
    leftleg = 17,
    rightleg = 22

};

const char* WindowsKeyToString(WindowsKey key);


static bool setCurrentMapSpecs = false;