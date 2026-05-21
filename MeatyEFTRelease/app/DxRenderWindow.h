#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <d3d11.h>
#include <dxgi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <glm/glm.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct DxFontSettings
{
    std::wstring name = L"Arial";
    float size = 14.0f;
    bool bold = false;
    bool italic = false;
};

struct DxWindowConfig
{
    // Startup
    bool autoStart = false;

    // Monitor / display
    int monitorIndex = 0;
    bool useMonitorSize = false;

    // Window mode
    bool fullscreen = false;
    bool borderless = true;
    int windowWidth = 1280;
    int windowHeight = 720;
    bool topMost = false;
    bool showInTaskbar = true;

    // Background
    bool transparentBackground = false;
    glm::vec4 backgroundColour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    // Frame timing
    bool useVSync = true;
    bool useMonitorRefreshRate = true;
    int maxFPS = 144;

    // Used only when fuserRender::Render() returns false.
    // Keeps the window responsive without rendering at full speed while idle.
    int idleFPS = 2;

    // Rendering quality
    bool antiAliasing = true;

    // Scaling: positions are not scaled. Sizes, thickness, radius and text are scaled.
    bool useDpiScale = true;
    float renderScale = 1.0f;

    // Text / fonts
    DxFontSettings defaultFont;
};

struct DxMonitorInfo
{
    int index = 0;
    std::wstring name;
    std::wstring deviceName;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int refreshRate = 60;
    bool primary = false;
};

class DxRenderWindow
{
public:
    DxRenderWindow();
    ~DxRenderWindow();

    DxRenderWindow(const DxRenderWindow&) = delete;
    DxRenderWindow& operator=(const DxRenderWindow&) = delete;

    bool Init(const DxWindowConfig& config);
    bool Start();
    void Stop();
    void Shutdown();

    bool IsInitialized() const;
    bool IsRunning() const;
    bool IsWindowReady() const;

    HWND GetHWND() const;
    int GetWindowWidth() const;
    int GetWindowHeight() const;

    DxWindowConfig GetConfig() const;
    void SetConfig(const DxWindowConfig& config);

    bool RefreshMonitorList();
    std::vector<DxMonitorInfo> GetMonitors() const;
    DxMonitorInfo GetCurrentMonitor() const;
    bool SetMonitor(int monitorIndex);

    float GetFinalRenderScale() const;

    // Draw list control
    void BeginDrawList();
    void SubmitDrawList();
    void ClearDrawLists();

    // Basic drawing
    void DrawLine(float x1, float y1, float x2, float y2, const glm::vec4& colour, float thickness = 1.0f);
    void DrawRect(float x, float y, float w, float h, const glm::vec4& colour, float thickness = 1.0f);
    void DrawFilledRect(float x, float y, float w, float h, const glm::vec4& colour);

    void DrawBox(
        float x,
        float y,
        float w,
        float h,
        const glm::vec4& outlineColour,
        float outlineThickness = 1.0f,
        const glm::vec4& fillColour = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f),
        bool filled = false
    );

    void DrawCircle(float x, float y, float radius, const glm::vec4& colour, float thickness = 1.0f);
    void DrawFilledCircle(float x, float y, float radius, const glm::vec4& colour);

    void DrawText(
        const std::string& text,
        float x,
        float y,
        const glm::vec4& colour,
        bool centered = false,
        bool outlined = false,
        const glm::vec4& outlineColour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    );

    void DrawText(
        const std::string& text,
        float x,
        float y,
        float size,
        const glm::vec4& colour,
        bool centered = false,
        bool outlined = false,
        const glm::vec4& outlineColour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        const std::wstring& fontName = L""
    );

    void DrawMarkerWithText(
        float x,
        float y,
        float markerSize,
        const std::string& text,
        const glm::vec4& markerOutlineColour,
        const glm::vec4& markerFillColour,
        const glm::vec4& textColour,
        float textSize = 0.0f,
        float textOffsetY = 5.0f,
        float outlineThickness = 1.0f,
        bool outlinedText = true,
        const glm::vec4& textOutlineColour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    );

private:
    enum class DrawCommandType
    {
        Line,
        Rect,
        FilledRect,
        Box,
        Circle,
        FilledCircle,
        Text,
        MarkerWithText
    };

    struct DrawCommand
    {
        DrawCommandType type = DrawCommandType::Line;

        float x = 0.0f;
        float y = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        float radius = 0.0f;
        float thickness = 1.0f;

        glm::vec4 colour = glm::vec4(1.0f);
        glm::vec4 fillColour = glm::vec4(0.0f);
        glm::vec4 outlineColour = glm::vec4(0.0f);
        glm::vec4 textColour = glm::vec4(1.0f);

        bool filled = false;

        std::string text;
        std::wstring fontName;
        float fontSize = 0.0f;
        bool centered = false;
        bool outlined = false;

        float markerSize = 0.0f;
        float textOffsetY = 0.0f;
    };

private:
    bool CreateAppWindow();
    bool CreateDeviceResources();
    bool CreateRenderTargets();
    bool RecreateRenderTargets();
    bool RecreateDeviceResources();

    void CleanupRenderTargets();
    void CleanupDeviceResources();
    void DestroyAppWindow();

    void RenderLoop();
    void ProcessMessages();
    bool BuildFrameDrawList();
    bool RenderFrame();
    void LimitFrame(std::chrono::steady_clock::time_point frameStart, bool activeScene);
    void SleepUntil(std::chrono::steady_clock::time_point targetTime);

    void OnResize(UINT width, UINT height);

    void PushDrawCommand(DrawCommand&& command);
    void UpdateRenderDrawList();
    void ReleaseDrawStorageUnlocked();

    void RenderDrawCommands(const std::vector<DrawCommand>& commands, const DxWindowConfig& cfg, float scale);
    void RenderTextCommand(const DrawCommand& cmd, const DxWindowConfig& cfg, float scale);
    void RenderMarkerWithTextCommand(const DrawCommand& cmd, const DxWindowConfig& cfg, float scale);

    DxWindowConfig GetConfigSnapshot() const;
    float GetDpiScaleForWindow(HWND hwnd) const;
    float GetFinalScale(const DxWindowConfig& cfg) const;
    int GetTargetFPS(const DxWindowConfig& cfg) const;

    IDWriteTextFormat* GetTextFormat(const std::wstring& fontName, float size, bool bold, bool italic);

    static std::wstring Utf8ToWide(const std::string& text);
    static D2D1_COLOR_F ToD2DColour(const glm::vec4& colour);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);

private:
    mutable std::mutex m_configMutex;
    DxWindowConfig m_config;

    mutable std::mutex m_monitorMutex;
    std::vector<DxMonitorInfo> m_monitors;

    std::atomic<bool> m_initialized = false;
    std::atomic<bool> m_running = false;
    std::atomic<bool> m_windowReady = false;
    std::atomic<bool> m_stopRequested = false;

    std::atomic<HWND> m_hwnd = nullptr;
    std::atomic<DWORD> m_renderThreadId = 0;
    std::thread m_renderThread;

    std::atomic<int> m_windowWidth = 0;
    std::atomic<int> m_windowHeight = 0;
    std::atomic<int> m_selectedRefreshRate = 60;
    std::atomic<float> m_dpiScale = 1.0f;

    std::chrono::steady_clock::time_point m_nextFrameTime{};
    bool m_frameLimiterPrimed = false;
    int m_lastFrameLimitFPS = 0;

    mutable std::mutex m_drawMutex;
    std::vector<DrawCommand> m_pendingDrawList;
    std::vector<DrawCommand> m_submittedDrawList;
    std::vector<DrawCommand> m_renderDrawList;
    std::atomic<std::uint64_t> m_drawVersion = 0;
    std::uint64_t m_renderDrawVersion = 0;

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGIFactory> m_dxgiFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;

    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> m_d2dRenderTarget;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_solidBrush;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;

    std::unordered_map<std::wstring, Microsoft::WRL::ComPtr<IDWriteTextFormat>> m_textFormatCache;
};

extern DxRenderWindow g_DxWindow;
