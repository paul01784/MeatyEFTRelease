#include "app/debug.h"
#include "app/DxRenderWindow.h"
#include "app/fuserRender.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <cmath>
#include <cfloat>
#include <dwmapi.h>
#include <sstream>
#include <mmsystem.h>
#include <eh.h>
#include <sstream>
#include <iostream>


#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;

DxRenderWindow g_DxWindow;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

class FuserSehException : public std::exception
{
public:
    FuserSehException(unsigned int code, EXCEPTION_POINTERS* exceptionPointers)
        : m_code(code)
    {
        void* address = nullptr;

        if (exceptionPointers &&
            exceptionPointers->ExceptionRecord)
        {
            address = exceptionPointers->ExceptionRecord->ExceptionAddress;
        }

        std::ostringstream ss;
        ss << "SEH exception. Code: 0x"
            << std::hex << m_code
            << " Address: " << address;

        m_message = ss.str();
    }

    const char* what() const noexcept override
    {
        return m_message.c_str();
    }

    unsigned int code() const
    {
        return m_code;
    }

private:
    unsigned int m_code = 0;
    std::string m_message;
};

static void FuserSehTranslator(unsigned int code, EXCEPTION_POINTERS* exceptionPointers)
{
    throw FuserSehException(code, exceptionPointers);
}

static void LogFuserCrash(const std::string& message)
{
    LOGS.logError("[FUSER][CRASH] " + message);
}

static constexpr const wchar_t* DX_RENDER_WINDOW_CLASS_NAME = L"DxRenderWindowClass";

DxRenderWindow::DxRenderWindow()
{
}

DxRenderWindow::~DxRenderWindow()
{
    Shutdown();
}

bool DxRenderWindow::Init(const DxWindowConfig& config)
{
    Shutdown();

    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config = config;
    }

    RefreshMonitorList();

    m_initialized.store(true);

    if (config.autoStart)
    {
        return Start();
    }

    return true;
}

bool DxRenderWindow::Start()
{
    if (!m_initialized.load())
        return false;

    if (m_running.load())
        return true;

    if (m_renderThread.joinable())
        m_renderThread.join();

    m_stopRequested.store(false);
    m_windowReady.store(false);
    m_running.store(true);

    try
    {
        m_renderThread = std::thread(&DxRenderWindow::RenderLoop, this);
    }
    catch (...)
    {
        m_running.store(false);
        return false;
    }

    return true;
}

void DxRenderWindow::Stop()
{
    m_stopRequested.store(true);

    HWND hwnd = m_hwnd.load();
    if (hwnd)
    {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    else
    {
        DWORD threadId = m_renderThreadId.load();
        if (threadId != 0)
            PostThreadMessageW(threadId, WM_QUIT, 0, 0);
    }

    if (m_renderThread.joinable())
    {
        if (m_renderThread.get_id() != std::this_thread::get_id())
        {
            m_renderThread.join();
        }
    }

    m_running.store(false);
    m_windowReady.store(false);
    m_renderThreadId.store(0);
}

void DxRenderWindow::Shutdown()
{
    Stop();

    {
        std::lock_guard<std::mutex> lock(m_drawMutex);
        m_pendingDrawList.clear();
        m_submittedDrawList.clear();
    }

    m_renderDrawList.clear();
    m_drawVersion.store(0);
    m_renderDrawVersion = 0;

    m_initialized.store(false);
}

bool DxRenderWindow::IsInitialized() const
{
    return m_initialized.load();
}

bool DxRenderWindow::IsRunning() const
{
    return m_running.load();
}

bool DxRenderWindow::IsWindowReady() const
{
    return m_windowReady.load();
}

HWND DxRenderWindow::GetHWND() const
{
    return m_hwnd.load();
}

int DxRenderWindow::GetWindowWidth() const
{
    return m_windowWidth.load();
}

int DxRenderWindow::GetWindowHeight() const
{
    return m_windowHeight.load();
}

DxWindowConfig DxRenderWindow::GetConfig() const
{
    return GetConfigSnapshot();
}

void DxRenderWindow::SetConfig(const DxWindowConfig& config)
{
    std::lock_guard<std::mutex> lock(m_configMutex);
    m_config = config;
}

bool DxRenderWindow::RefreshMonitorList()
{
    std::vector<DxMonitorInfo> temp;
    EnumDisplayMonitors(nullptr, nullptr, DxRenderWindow::MonitorEnumProc, reinterpret_cast<LPARAM>(&temp));

    if (temp.empty())
    {
        DxMonitorInfo fallback{};
        fallback.index = 0;
        fallback.name = L"Primary Monitor";
        fallback.deviceName = L"DISPLAY";
        fallback.x = 0;
        fallback.y = 0;
        fallback.width = GetSystemMetrics(SM_CXSCREEN);
        fallback.height = GetSystemMetrics(SM_CYSCREEN);
        fallback.refreshRate = 60;
        fallback.primary = true;
        temp.push_back(fallback);
    }

    {
        std::lock_guard<std::mutex> lock(m_monitorMutex);
        m_monitors = std::move(temp);
    }

    return true;
}

std::vector<DxMonitorInfo> DxRenderWindow::GetMonitors() const
{
    std::lock_guard<std::mutex> lock(m_monitorMutex);
    return m_monitors;
}

DxMonitorInfo DxRenderWindow::GetCurrentMonitor() const
{
    DxWindowConfig cfg = GetConfigSnapshot();

    std::lock_guard<std::mutex> lock(m_monitorMutex);

    if (m_monitors.empty())
        return DxMonitorInfo{};

    int index = std::clamp(cfg.monitorIndex, 0, static_cast<int>(m_monitors.size()) - 1);
    return m_monitors[index];
}

bool DxRenderWindow::SetMonitor(int monitorIndex)
{
    {
        std::lock_guard<std::mutex> lock(m_monitorMutex);

        if (monitorIndex < 0 || monitorIndex >= static_cast<int>(m_monitors.size()))
            return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        m_config.monitorIndex = monitorIndex;
    }

    return true;
}

float DxRenderWindow::GetFinalRenderScale() const
{
    return GetFinalScale(GetConfigSnapshot());
}

void DxRenderWindow::BeginDrawList()
{
    std::lock_guard<std::mutex> lock(m_drawMutex);
    m_pendingDrawList.clear();
}

void DxRenderWindow::SubmitDrawList()
{
    std::lock_guard<std::mutex> lock(m_drawMutex);

    m_submittedDrawList.swap(m_pendingDrawList);
    m_pendingDrawList.clear();

    m_drawVersion.fetch_add(1, std::memory_order_release);
}

void DxRenderWindow::ClearDrawLists()
{
    {
        std::lock_guard<std::mutex> lock(m_drawMutex);
        m_pendingDrawList.clear();
        m_submittedDrawList.clear();
    }

    m_drawVersion.fetch_add(1, std::memory_order_release);
}

void DxRenderWindow::DrawLine(
    float x1,
    float y1,
    float x2,
    float y2,
    const glm::vec4& colour,
    float thickness)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::Line;
    cmd.x = x1;
    cmd.y = y1;
    cmd.x2 = x2;
    cmd.y2 = y2;
    cmd.colour = colour;
    cmd.thickness = thickness;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawRect(
    float x,
    float y,
    float w,
    float h,
    const glm::vec4& colour,
    float thickness)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::Rect;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.colour = colour;
    cmd.thickness = thickness;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawFilledRect(
    float x,
    float y,
    float w,
    float h,
    const glm::vec4& colour)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::FilledRect;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.colour = colour;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawBox(
    float x,
    float y,
    float w,
    float h,
    const glm::vec4& outlineColour,
    float outlineThickness,
    const glm::vec4& fillColour,
    bool filled)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::Box;
    cmd.x = x;
    cmd.y = y;
    cmd.w = w;
    cmd.h = h;
    cmd.outlineColour = outlineColour;
    cmd.fillColour = fillColour;
    cmd.thickness = outlineThickness;
    cmd.filled = filled;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawCircle(
    float x,
    float y,
    float radius,
    const glm::vec4& colour,
    float thickness)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::Circle;
    cmd.x = x;
    cmd.y = y;
    cmd.radius = radius;
    cmd.colour = colour;
    cmd.thickness = thickness;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawFilledCircle(
    float x,
    float y,
    float radius,
    const glm::vec4& colour)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::FilledCircle;
    cmd.x = x;
    cmd.y = y;
    cmd.radius = radius;
    cmd.colour = colour;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawText(
    const std::string& text,
    float x,
    float y,
    const glm::vec4& colour,
    bool centered,
    bool outlined,
    const glm::vec4& outlineColour)
{
    DrawText(
        text,
        x,
        y,
        0.0f,
        colour,
        centered,
        outlined,
        outlineColour,
        L""
    );
}

void DxRenderWindow::DrawText(
    const std::string& text,
    float x,
    float y,
    float size,
    const glm::vec4& colour,
    bool centered,
    bool outlined,
    const glm::vec4& outlineColour,
    const std::wstring& fontName)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::Text;
    cmd.text = text;
    cmd.x = x;
    cmd.y = y;
    cmd.fontSize = size;
    cmd.fontName = fontName;
    cmd.textColour = colour;
    cmd.outlineColour = outlineColour;
    cmd.centered = centered;
    cmd.outlined = outlined;

    PushDrawCommand(std::move(cmd));
}

void DxRenderWindow::DrawMarkerWithText(
    float x,
    float y,
    float markerSize,
    const std::string& text,
    const glm::vec4& markerOutlineColour,
    const glm::vec4& markerFillColour,
    const glm::vec4& textColour,
    float textSize,
    float textOffsetY,
    float outlineThickness,
    bool outlinedText,
    const glm::vec4& textOutlineColour)
{
    DrawCommand cmd{};
    cmd.type = DrawCommandType::MarkerWithText;

    cmd.x = x;
    cmd.y = y;

    cmd.markerSize = markerSize;
    cmd.text = text;
    cmd.fontSize = textSize;
    cmd.textOffsetY = textOffsetY;

    cmd.outlineColour = markerOutlineColour;
    cmd.fillColour = markerFillColour;
    cmd.textColour = textColour;

    cmd.thickness = outlineThickness;
    cmd.outlined = outlinedText;
    cmd.colour = textOutlineColour;

    PushDrawCommand(std::move(cmd));
}

bool DxRenderWindow::CreateAppWindow()
{
    RefreshMonitorList();

    DxWindowConfig cfg = GetConfigSnapshot();
    DxMonitorInfo monitor = GetCurrentMonitor();

    m_selectedRefreshRate.store(std::max(1, monitor.refreshRate));

    int width = cfg.windowWidth;
    int height = cfg.windowHeight;

    if (cfg.fullscreen || cfg.useMonitorSize)
    {
        width = monitor.width;
        height = monitor.height;
    }

    width = std::max(1, width);
    height = std::max(1, height);

    int x = monitor.x;
    int y = monitor.y;

    if (!cfg.fullscreen && !cfg.useMonitorSize)
    {
        x = monitor.x + ((monitor.width - width) / 2);
        y = monitor.y + ((monitor.height - height) / 2);
    }

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DxRenderWindow::WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = DX_RENDER_WINDOW_CLASS_NAME;

    ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    const bool borderless = cfg.fullscreen || cfg.borderless;

    DWORD style = borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;

    DWORD exStyle = 0;

    if (cfg.topMost)
        exStyle |= WS_EX_TOPMOST;

    if (cfg.showInTaskbar)
        exStyle |= WS_EX_APPWINDOW;
    else
        exStyle |= WS_EX_TOOLWINDOW;

    if (cfg.transparentBackground)
        exStyle |= WS_EX_LAYERED;

    RECT windowRect{};
    windowRect.left = x;
    windowRect.top = y;
    windowRect.right = x + width;
    windowRect.bottom = y + height;

    if (!borderless)
    {
        AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
    }

    HWND hwnd = CreateWindowExW(
        exStyle,
        DX_RENDER_WINDOW_CLASS_NAME,
        L"DxRenderWindow",
        style,
        windowRect.left,
        windowRect.top,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd)
        return false;

    m_hwnd.store(hwnd);
    m_windowWidth.store(width);
    m_windowHeight.store(height);

    if (cfg.transparentBackground)
    {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

        MARGINS margins{};
        margins.cxLeftWidth = -1;
        margins.cxRightWidth = -1;
        margins.cyTopHeight = -1;
        margins.cyBottomHeight = -1;
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    m_dpiScale.store(GetDpiScaleForWindow(hwnd));

    return true;
}

bool DxRenderWindow::CreateDeviceResources()
{
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL createdFeatureLevel{};

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        m_d3dDevice.GetAddressOf(),
        &createdFeatureLevel,
        m_d3dContext.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr))
        return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr))
        return false;

    hr = adapter->GetParent(IID_PPV_ARGS(m_dxgiFactory.GetAddressOf()));
    if (FAILED(hr))
        return false;

    HWND hwnd = m_hwnd.load();
    if (!hwnd)
        return false;

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Width = static_cast<UINT>(m_windowWidth.load());
    swapDesc.BufferDesc.Height = static_cast<UINT>(m_windowHeight.load());
    swapDesc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = static_cast<UINT>(m_selectedRefreshRate.load());
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = hwnd;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = m_dxgiFactory->CreateSwapChain(
        m_d3dDevice.Get(),
        &swapDesc,
        m_swapChain.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    m_dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
    );

    if (FAILED(hr))
        return false;

    return CreateRenderTargets();
}

bool DxRenderWindow::CreateRenderTargets()
{
    if (!m_swapChain || !m_d3dDevice || !m_d2dFactory)
        return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr))
        return false;

    hr = m_d3dDevice->CreateRenderTargetView(
        backBuffer.Get(),
        nullptr,
        m_renderTargetView.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    ComPtr<IDXGISurface> dxgiSurface;
    hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(dxgiSurface.GetAddressOf()));
    if (FAILED(hr))
        return false;

    DxWindowConfig cfg = GetConfigSnapshot();

    const float dpi = 96.0f * m_dpiScale.load();

    D2D1_ALPHA_MODE alphaMode = cfg.transparentBackground
        ? D2D1_ALPHA_MODE_PREMULTIPLIED
        : D2D1_ALPHA_MODE_IGNORE;

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, alphaMode),
        dpi,
        dpi
    );

    hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(
        dxgiSurface.Get(),
        &props,
        m_d2dRenderTarget.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    if (cfg.antiAliasing)
    {
        m_d2dRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_d2dRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    }
    else
    {
        m_d2dRenderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        m_d2dRenderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    }

    hr = m_d2dRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f),
        m_solidBrush.GetAddressOf()
    );

    return SUCCEEDED(hr);
}

void DxRenderWindow::CleanupRenderTargets()
{
    m_solidBrush.Reset();
    m_d2dRenderTarget.Reset();
    m_renderTargetView.Reset();
}

void DxRenderWindow::CleanupDeviceResources()
{
    CleanupRenderTargets();

    m_textFormatCache.clear();

    m_swapChain.Reset();
    m_dxgiFactory.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();

    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
}

void DxRenderWindow::DestroyAppWindow()
{
    HWND hwnd = m_hwnd.exchange(nullptr);

    if (hwnd)
    {
        DestroyWindow(hwnd);
    }
}

void DxRenderWindow::RenderLoop()
{
    m_renderThreadId.store(GetCurrentThreadId());

    _set_se_translator(FuserSehTranslator);

    if (!CreateAppWindow())
    {
        m_running.store(false);
        m_windowReady.store(false);
        return;
    }

    if (!CreateDeviceResources())
    {
        CleanupDeviceResources();
        DestroyAppWindow();

        m_running.store(false);
        m_windowReady.store(false);
        return;
    }

    timeBeginPeriod(1);

    m_frameWaitTimer = CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS
    );

    if (!m_frameWaitTimer)
    {
        m_frameWaitTimer = CreateWaitableTimerW(
            nullptr,
            FALSE,
            nullptr
        );
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    m_frameLimiterPrimed = false;
    m_lastFrameLimitFPS = 0;

    m_windowReady.store(true);

    int consecutiveRenderFailures = 0;

    while (!m_stopRequested.load())
    {
        auto frameStart = std::chrono::steady_clock::now();

        try
        {
            ProcessMessages();

            if (m_stopRequested.load())
                break;

            const bool activeScene = fuserRender::Render();
            const int forcedFps = activeScene ? 0 : 1;

            RenderFrame();

            FrameLimit(frameStart, forcedFps);

            consecutiveRenderFailures = 0;
        }
        catch (const FuserSehException& e)
        {
            consecutiveRenderFailures++;

            LogFuserCrash(
                std::string("Render thread SEH crash: ") +
                e.what() +
                " Stage: " +
                fuserRender::GetCurrentStage()
            );

            ClearDrawLists();
            m_frameLimiterPrimed = false;

            if (consecutiveRenderFailures >= 10)
            {
                LogFuserCrash("Too many consecutive render crashes. Stopping fuser window.");
                m_stopRequested.store(true);
                break;
            }

            Sleep(50);
        }
        catch (const std::exception& e)
        {
            consecutiveRenderFailures++;

            LogFuserCrash(
                std::string("Render thread std exception: ") +
                e.what() +
                " Stage: " +
                fuserRender::GetCurrentStage()
            );

            ClearDrawLists();
            m_frameLimiterPrimed = false;

            if (consecutiveRenderFailures >= 10)
            {
                LogFuserCrash("Too many consecutive render crashes. Stopping fuser window.");
                m_stopRequested.store(true);
                break;
            }

            Sleep(50);
        }
        catch (...)
        {
            consecutiveRenderFailures++;

            LogFuserCrash(
                std::string("Render thread unknown exception. Stage: ") +
                fuserRender::GetCurrentStage()
            );

            ClearDrawLists();
            m_frameLimiterPrimed = false;

            if (consecutiveRenderFailures >= 10)
            {
                LogFuserCrash("Too many consecutive render crashes. Stopping fuser window.");
                m_stopRequested.store(true);
                break;
            }

            Sleep(50);
        }
    }

    m_windowReady.store(false);

    if (m_frameWaitTimer)
    {
        CloseHandle(m_frameWaitTimer);
        m_frameWaitTimer = nullptr;
    }

    timeEndPeriod(1);

    CleanupDeviceResources();
    DestroyAppWindow();

    m_running.store(false);
    m_renderThreadId.store(0);
}

void DxRenderWindow::ProcessMessages()
{
    MSG msg{};

    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            m_stopRequested.store(true);
            return;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void DxRenderWindow::RenderFrame()
{
    if (!m_swapChain || !m_d2dRenderTarget || !m_d3dContext || !m_renderTargetView)
        return;

    DxWindowConfig cfg = GetConfigSnapshot();
    float scale = GetFinalScale(cfg);

    UpdateRenderDrawList();

    glm::vec4 clearColour = cfg.transparentBackground
        ? glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)
        : cfg.backgroundColour;

    float clear[4]
    {
        clearColour.r,
        clearColour.g,
        clearColour.b,
        clearColour.a
    };

    m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), nullptr);
    m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), clear);

    m_d2dRenderTarget->BeginDraw();
    m_d2dRenderTarget->Clear(ToD2DColour(clearColour));

    RenderDrawCommands(m_renderDrawList, cfg, scale);

    HRESULT hr = m_d2dRenderTarget->EndDraw();

    if (hr == D2DERR_RECREATE_TARGET)
    {
        CleanupRenderTargets();
        CreateRenderTargets();
        return;
    }

    if (FAILED(hr))
    {
        CleanupRenderTargets();
        CreateRenderTargets();
        return;
    }

    hr = m_swapChain->Present(cfg.useVSync ? 1 : 0, 0);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        CleanupDeviceResources();
        CreateDeviceResources();
        return;
    }

    m_swapChain->Present(cfg.useVSync ? 1 : 0, 0);
}

void DxRenderWindow::FrameLimit(
    std::chrono::steady_clock::time_point frameStart,
    int forcedTargetFPS)
{
    (void)frameStart;

    DxWindowConfig cfg = GetConfigSnapshot();

    const bool usingForcedLimit = forcedTargetFPS > 0;

    if (cfg.useVSync && !usingForcedLimit)
    {
        m_frameLimiterPrimed = false;
        m_lastFrameLimitFPS = 0;
        return;
    }

    const int targetFPS = usingForcedLimit
        ? forcedTargetFPS
        : GetTargetFPS(cfg);

    if (targetFPS <= 0)
    {
        m_frameLimiterPrimed = false;
        m_lastFrameLimitFPS = 0;
        return;
    }

    using namespace std::chrono;

    const auto frameDuration =
        duration_cast<steady_clock::duration>(
            duration<double>(1.0 / static_cast<double>(targetFPS))
        );

    const auto now = steady_clock::now();

    // Reset pacing when starting limiter or FPS target changes.
    if (!m_frameLimiterPrimed || m_lastFrameLimitFPS != targetFPS)
    {
        m_nextFrameTime = now + frameDuration;
        m_frameLimiterPrimed = true;
        m_lastFrameLimitFPS = targetFPS;
        return;
    }

    // If we fell badly behind, resync instead of trying to catch up.
    if (now > m_nextFrameTime + frameDuration)
    {
        m_nextFrameTime = now + frameDuration;
        return;
    }

    const auto targetTime = m_nextFrameTime;

    while (true)
    {
        const auto current = steady_clock::now();

        if (current >= targetTime)
            break;

        const auto remaining = targetTime - current;

        if (remaining > milliseconds(2))
        {
            const auto waitFor = remaining - milliseconds(1);

            if (m_frameWaitTimer)
            {
                const auto wait100ns =
                    duration_cast<duration<long long, std::ratio<1, 10000000>>>(waitFor).count();

                LARGE_INTEGER dueTime{};
                dueTime.QuadPart = -wait100ns;

                if (SetWaitableTimer(m_frameWaitTimer, &dueTime, 0, nullptr, nullptr, FALSE))
                {
                    WaitForSingleObject(m_frameWaitTimer, INFINITE);
                }
                else
                {
                    Sleep(1);
                }
            }
            else
            {
                Sleep(1);
            }
        }
        else if (remaining > microseconds(1000))
        {
            std::this_thread::yield();
        }
        else
        {
            while (steady_clock::now() < targetTime)
            {
            }

            break;
        }
    }

    m_nextFrameTime += frameDuration;
}

void DxRenderWindow::OnResize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    m_windowWidth.store(static_cast<int>(width));
    m_windowHeight.store(static_cast<int>(height));

    if (!m_swapChain || !m_d3dContext)
        return;

    m_d3dContext->OMSetRenderTargets(0, nullptr, nullptr);

    CleanupRenderTargets();

    HRESULT hr = m_swapChain->ResizeBuffers(
        0,
        width,
        height,
        DXGI_FORMAT_UNKNOWN,
        0
    );

    if (SUCCEEDED(hr))
    {
        CreateRenderTargets();
    }
}

void DxRenderWindow::PushDrawCommand(DrawCommand&& command)
{
    std::lock_guard<std::mutex> lock(m_drawMutex);
    m_pendingDrawList.emplace_back(std::move(command));
}

void DxRenderWindow::UpdateRenderDrawList()
{
    std::uint64_t version = m_drawVersion.load(std::memory_order_acquire);

    if (version == m_renderDrawVersion)
        return;

    {
        std::lock_guard<std::mutex> lock(m_drawMutex);
        m_renderDrawList = m_submittedDrawList;
    }

    m_renderDrawVersion = version;
}

void DxRenderWindow::RenderDrawCommands(
    const std::vector<DrawCommand>& commands,
    const DxWindowConfig& cfg,
    float scale)
{
    if (!m_d2dRenderTarget || !m_solidBrush)
        return;

    for (const DrawCommand& cmd : commands)
    {
        switch (cmd.type)
        {
        case DrawCommandType::Line:
        {
            m_solidBrush->SetColor(ToD2DColour(cmd.colour));

            float thickness = std::max(0.1f, cmd.thickness * scale);

            m_d2dRenderTarget->DrawLine(
                D2D1::Point2F(cmd.x, cmd.y),
                D2D1::Point2F(cmd.x2, cmd.y2),
                m_solidBrush.Get(),
                thickness
            );

            break;
        }

        case DrawCommandType::Rect:
        {
            m_solidBrush->SetColor(ToD2DColour(cmd.colour));

            float w = cmd.w * scale;
            float h = cmd.h * scale;
            float thickness = std::max(0.1f, cmd.thickness * scale);

            D2D1_RECT_F rect = D2D1::RectF(
                cmd.x,
                cmd.y,
                cmd.x + w,
                cmd.y + h
            );

            m_d2dRenderTarget->DrawRectangle(rect, m_solidBrush.Get(), thickness);
            break;
        }

        case DrawCommandType::FilledRect:
        {
            m_solidBrush->SetColor(ToD2DColour(cmd.colour));

            float w = cmd.w * scale;
            float h = cmd.h * scale;

            D2D1_RECT_F rect = D2D1::RectF(
                cmd.x,
                cmd.y,
                cmd.x + w,
                cmd.y + h
            );

            m_d2dRenderTarget->FillRectangle(rect, m_solidBrush.Get());
            break;
        }

        case DrawCommandType::Box:
        {
            float w = cmd.w * scale;
            float h = cmd.h * scale;
            float thickness = std::max(0.1f, cmd.thickness * scale);

            D2D1_RECT_F rect = D2D1::RectF(
                cmd.x,
                cmd.y,
                cmd.x + w,
                cmd.y + h
            );

            if (cmd.filled && cmd.fillColour.a > 0.0f)
            {
                m_solidBrush->SetColor(ToD2DColour(cmd.fillColour));
                m_d2dRenderTarget->FillRectangle(rect, m_solidBrush.Get());
            }

            m_solidBrush->SetColor(ToD2DColour(cmd.outlineColour));
            m_d2dRenderTarget->DrawRectangle(rect, m_solidBrush.Get(), thickness);

            break;
        }

        case DrawCommandType::Circle:
        {
            m_solidBrush->SetColor(ToD2DColour(cmd.colour));

            float radius = cmd.radius * scale;
            float thickness = std::max(0.1f, cmd.thickness * scale);

            D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                D2D1::Point2F(cmd.x, cmd.y),
                radius,
                radius
            );

            m_d2dRenderTarget->DrawEllipse(ellipse, m_solidBrush.Get(), thickness);
            break;
        }

        case DrawCommandType::FilledCircle:
        {
            m_solidBrush->SetColor(ToD2DColour(cmd.colour));

            float radius = cmd.radius * scale;

            D2D1_ELLIPSE ellipse = D2D1::Ellipse(
                D2D1::Point2F(cmd.x, cmd.y),
                radius,
                radius
            );

            m_d2dRenderTarget->FillEllipse(ellipse, m_solidBrush.Get());
            break;
        }

        case DrawCommandType::Text:
        {
            RenderTextCommand(cmd, cfg, scale);
            break;
        }

        case DrawCommandType::MarkerWithText:
        {
            RenderMarkerWithTextCommand(cmd, cfg, scale);
            break;
        }
        }
    }
}

void DxRenderWindow::RenderTextCommand(
    const DrawCommand& cmd,
    const DxWindowConfig& cfg,
    float scale)
{
    if (!m_d2dRenderTarget || !m_solidBrush || !m_dwriteFactory)
        return;

    if (cmd.text.empty())
        return;

    std::wstring wideText = Utf8ToWide(cmd.text);
    if (wideText.empty())
        return;

    std::wstring fontName = cmd.fontName.empty()
        ? cfg.defaultFont.name
        : cmd.fontName;

    float baseSize = cmd.fontSize > 0.0f
        ? cmd.fontSize
        : cfg.defaultFont.size;

    float finalSize = std::max(1.0f, baseSize * scale);

    IDWriteTextFormat* format = GetTextFormat(
        fontName,
        finalSize,
        cfg.defaultFont.bold,
        cfg.defaultFont.italic
    );

    if (!format)
        return;

    ComPtr<IDWriteTextLayout> layout;

    HRESULT hr = m_dwriteFactory->CreateTextLayout(
        wideText.c_str(),
        static_cast<UINT32>(wideText.size()),
        format,
        4096.0f,
        4096.0f,
        layout.GetAddressOf()
    );

    if (FAILED(hr) || !layout)
        return;

    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);

    float drawX = cmd.x;
    float drawY = cmd.y;

    if (cmd.centered)
    {
        drawX -= metrics.widthIncludingTrailingWhitespace * 0.5f;
    }

    const float outlineOffset = std::max(1.0f, 1.0f * scale);

    if (cmd.outlined)
    {
        m_solidBrush->SetColor(ToD2DColour(cmd.outlineColour));

        const std::array<D2D1_POINT_2F, 8> offsets =
        {
            D2D1::Point2F(-outlineOffset, 0.0f),
            D2D1::Point2F(outlineOffset, 0.0f),
            D2D1::Point2F(0.0f, -outlineOffset),
            D2D1::Point2F(0.0f, outlineOffset),
            D2D1::Point2F(-outlineOffset, -outlineOffset),
            D2D1::Point2F(outlineOffset, -outlineOffset),
            D2D1::Point2F(-outlineOffset, outlineOffset),
            D2D1::Point2F(outlineOffset, outlineOffset)
        };

        for (const auto& offset : offsets)
        {
            m_d2dRenderTarget->DrawTextLayout(
                D2D1::Point2F(drawX + offset.x, drawY + offset.y),
                layout.Get(),
                m_solidBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_NO_SNAP
            );
        }
    }

    m_solidBrush->SetColor(ToD2DColour(cmd.textColour));

    m_d2dRenderTarget->DrawTextLayout(
        D2D1::Point2F(drawX, drawY),
        layout.Get(),
        m_solidBrush.Get(),
        D2D1_DRAW_TEXT_OPTIONS_NO_SNAP
    );
}

void DxRenderWindow::RenderMarkerWithTextCommand(
    const DrawCommand& cmd,
    const DxWindowConfig& cfg,
    float scale)
{
    if (!m_d2dRenderTarget || !m_solidBrush)
        return;

    float markerSize = std::max(1.0f, cmd.markerSize * scale);
    float half = markerSize * 0.5f;
    float thickness = std::max(0.1f, cmd.thickness * scale);

    D2D1_RECT_F markerRect = D2D1::RectF(
        cmd.x - half,
        cmd.y - half,
        cmd.x + half,
        cmd.y + half
    );

    if (cmd.fillColour.a > 0.0f)
    {
        m_solidBrush->SetColor(ToD2DColour(cmd.fillColour));
        m_d2dRenderTarget->FillRectangle(markerRect, m_solidBrush.Get());
    }

    m_solidBrush->SetColor(ToD2DColour(cmd.outlineColour));
    m_d2dRenderTarget->DrawRectangle(markerRect, m_solidBrush.Get(), thickness);

    if (!cmd.text.empty())
    {
        DrawCommand textCmd{};
        textCmd.type = DrawCommandType::Text;
        textCmd.text = cmd.text;
        textCmd.x = cmd.x;
        textCmd.y = cmd.y + half + (cmd.textOffsetY * scale);
        textCmd.fontSize = cmd.fontSize;
        textCmd.fontName = cmd.fontName;
        textCmd.textColour = cmd.textColour;
        textCmd.outlineColour = cmd.colour;
        textCmd.centered = true;
        textCmd.outlined = cmd.outlined;

        RenderTextCommand(textCmd, cfg, scale);
    }
}

DxWindowConfig DxRenderWindow::GetConfigSnapshot() const
{
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_config;
}

float DxRenderWindow::GetDpiScaleForWindow(HWND hwnd) const
{
    if (!hwnd)
        return 1.0f;

    HDC hdc = GetDC(hwnd);
    if (!hdc)
        return 1.0f;

    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(hwnd, hdc);

    if (dpiX <= 0)
        return 1.0f;

    return static_cast<float>(dpiX) / 96.0f;
}

float DxRenderWindow::GetFinalScale(const DxWindowConfig& cfg) const
{
    float scale = std::max(0.05f, cfg.renderScale);

    if (cfg.useDpiScale)
        scale *= std::max(0.05f, m_dpiScale.load());

    return scale;
}

int DxRenderWindow::GetTargetFPS(const DxWindowConfig& cfg) const
{
    if (cfg.useMonitorRefreshRate)
        return std::max(1, m_selectedRefreshRate.load());

    return std::max(1, cfg.maxFPS);
}

IDWriteTextFormat* DxRenderWindow::GetTextFormat(
    const std::wstring& fontName,
    float size,
    bool bold,
    bool italic)
{
    if (!m_dwriteFactory)
        return nullptr;

    int sizeKey = static_cast<int>(std::round(size * 10.0f));

    std::wstringstream ss;
    ss << fontName << L"|"
        << sizeKey << L"|"
        << (bold ? L"b" : L"n") << L"|"
        << (italic ? L"i" : L"r");

    std::wstring key = ss.str();

    auto it = m_textFormatCache.find(key);
    if (it != m_textFormatCache.end())
        return it->second.Get();

    DWRITE_FONT_WEIGHT weight = bold
        ? DWRITE_FONT_WEIGHT_BOLD
        : DWRITE_FONT_WEIGHT_NORMAL;

    DWRITE_FONT_STYLE style = italic
        ? DWRITE_FONT_STYLE_ITALIC
        : DWRITE_FONT_STYLE_NORMAL;

    ComPtr<IDWriteTextFormat> format;

    HRESULT hr = m_dwriteFactory->CreateTextFormat(
        fontName.c_str(),
        nullptr,
        weight,
        style,
        DWRITE_FONT_STRETCH_NORMAL,
        size,
        L"",
        format.GetAddressOf()
    );

    if (FAILED(hr))
        return nullptr;

    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    IDWriteTextFormat* raw = format.Get();
    m_textFormatCache.emplace(key, std::move(format));

    return raw;
}

std::wstring DxRenderWindow::Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    int needed = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );

    if (needed <= 0)
        return L"";

    std::wstring result(static_cast<size_t>(needed), L'\0');

    int written = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        needed
    );

    if (written <= 0)
        return L"";

    result.resize(static_cast<size_t>(written));
    return result;
}

D2D1_COLOR_F DxRenderWindow::ToD2DColour(const glm::vec4& colour)
{
    return D2D1::ColorF(
        std::clamp(colour.r, 0.0f, 1.0f),
        std::clamp(colour.g, 0.0f, 1.0f),
        std::clamp(colour.b, 0.0f, 1.0f),
        std::clamp(colour.a, 0.0f, 1.0f)
    );
}

LRESULT CALLBACK DxRenderWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DxRenderWindow* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<DxRenderWindow*>(createStruct->lpCreateParams);

        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<DxRenderWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA)
            );
    }

    if (self)
    {
        switch (msg)
        {
        case WM_SIZE:
        {
            if (wParam != SIZE_MINIMIZED)
            {
                UINT width = LOWORD(lParam);
                UINT height = HIWORD(lParam);
                self->OnResize(width, height);
            }

            return 0;
        }

        case WM_CLOSE:
        {
            self->m_stopRequested.store(true);
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }
        }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK DxRenderWindow::MonitorEnumProc(
    HMONITOR hMonitor,
    HDC,
    LPRECT,
    LPARAM dwData)
{
    auto* monitors = reinterpret_cast<std::vector<DxMonitorInfo>*>(dwData);
    if (!monitors)
        return TRUE;

    MONITORINFOEXW monitorInfo{};
    monitorInfo.cbSize = sizeof(MONITORINFOEXW);

    if (!GetMonitorInfoW(hMonitor, &monitorInfo))
        return TRUE;

    DxMonitorInfo info{};
    info.index = static_cast<int>(monitors->size());
    info.deviceName = monitorInfo.szDevice;

    info.x = monitorInfo.rcMonitor.left;
    info.y = monitorInfo.rcMonitor.top;
    info.width = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    info.height = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
    info.primary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;

    DEVMODEW devMode{};
    devMode.dmSize = sizeof(DEVMODEW);

    if (EnumDisplaySettingsW(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
    {
        if (devMode.dmDisplayFrequency > 0)
            info.refreshRate = static_cast<int>(devMode.dmDisplayFrequency);
    }

    DISPLAY_DEVICEW displayDevice{};
    displayDevice.cb = sizeof(DISPLAY_DEVICEW);

    if (EnumDisplayDevicesW(monitorInfo.szDevice, 0, &displayDevice, 0))
    {
        info.name = displayDevice.DeviceString;
    }

    if (info.name.empty())
        info.name = monitorInfo.szDevice;

    monitors->push_back(std::move(info));

    return TRUE;
}