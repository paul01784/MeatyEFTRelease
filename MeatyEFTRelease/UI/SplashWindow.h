#pragma once

#define WIN32_LEAN_AND_MEAN

#include <cstring>
#include <Windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>

#pragma comment(lib, "gdiplus.lib")

class SplashWindow
{
public:
    SplashWindow() = default;

    ~SplashWindow()
    {
        Close();

        if (gdiplusToken_ != 0)
        {
            Gdiplus::GdiplusShutdown(gdiplusToken_);
            gdiplusToken_ = 0;
        }
    }

    const std::wstring& GetLastErrorMessage() const
    {
        return lastError_;
    }

    SplashWindow(const SplashWindow&) = delete;
    SplashWindow& operator=(const SplashWindow&) = delete;

    bool Create(HINSTANCE hInstance, int resourceId, int width = 520, int height = 600)
    {
        lastError_.clear();

        hInstance_ = hInstance;
        width_ = width;
        height_ = height;

        // Start GDI

        Gdiplus::GdiplusStartupInput startupInput{};

        const Gdiplus::Status startupStatus =
            Gdiplus::GdiplusStartup(
                &gdiplusToken_,
                &startupInput,
                nullptr
            );

        if (startupStatus != Gdiplus::Ok)
        {
            lastError_ = L"GdiplusStartup failed. GDI+ status: " + std::to_wstring(static_cast<int>(startupStatus));

            return false;
        }

        // Load embedded logo

        if (!LoadPngResource(resourceId))
        {
            if (lastError_.empty())
                lastError_ = L"LoadPngResource failed.";

            return false;
        }

        // Register window class

        constexpr wchar_t className[] = L"MeatyEFTSplashWindow";

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &SplashWindow::StaticWindowProc;
        wc.hInstance = hInstance_;
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = className;

        if (!RegisterClassExW(&wc))
        {
            const DWORD error = GetLastError();

            if (error != ERROR_CLASS_ALREADY_EXISTS)
            {
                lastError_ = L"RegisterClassExW failed. Win32 error: " + std::to_wstring(error);

                return false;
            }
        }

        RECT workArea{};

        if (!SystemParametersInfoW(
            SPI_GETWORKAREA,
            0,
            &workArea,
            0))
        {
            workArea.left = 0;
            workArea.top = 0;
            workArea.right = GetSystemMetrics(SM_CXSCREEN);
            workArea.bottom = GetSystemMetrics(SM_CYSCREEN);
        }

        const int x = workArea.left + ((workArea.right - workArea.left) - width_) / 2;

        const int y = workArea.top + ((workArea.bottom - workArea.top) - height_) / 2;

        SetLastError(ERROR_SUCCESS);

        hwnd_ = CreateWindowExW(
            WS_EX_LAYERED |
            WS_EX_TOOLWINDOW |
            WS_EX_TOPMOST,
            className,
            L"MeatyEFT",
            WS_POPUP,
            x,
            y,
            width_,
            height_,
            nullptr,
            nullptr,
            hInstance_,
            this
        );

        if (!hwnd_)
        {
            const DWORD error = GetLastError();

            lastError_ = L"CreateWindowExW failed. Win32 error: " + std::to_wstring(error);

            return false;
        }

        HRGN roundedRegion = CreateRoundRectRgn(
            0,
            0,
            width_ + 1,
            height_ + 1,
            32,
            32
        );

        if (roundedRegion)
        {
            if (!SetWindowRgn(hwnd_, roundedRegion, TRUE))
            {
                DeleteObject(roundedRegion);
            }
        }

        if (!SetLayeredWindowAttributes(
            hwnd_,
            0,
            0,
            LWA_ALPHA))
        {
            const DWORD error = GetLastError();

            lastError_ = L"SetLayeredWindowAttributes failed. Win32 error: " + std::to_wstring(error);

            DestroyWindow(hwnd_);
            hwnd_ = nullptr;

            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        FadeTo(0, 255, 250);

        return true;
    }

    void SetStatus(const std::wstring& status)
    {
        {
            std::lock_guard<std::mutex> lock(statusMutex_);
            status_ = status;
        }

        if (hwnd_)
            PostMessageW(hwnd_, WM_STATUS_CHANGED, 0, 0);
    }

    bool PumpMessages()
    {
        MSG msg{};

        while (PeekMessageW(
            &msg,
            nullptr,
            0,
            0,
            PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return false;

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        return true;
    }

    void FadeOut(int durationMs = 250)
    {
        FadeTo(255, 0, durationMs);
    }

    void Close()
    {
        if (hwnd_)
        {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }

        image_.reset();
    }

    HWND GetWindowHandle() const
    {
        return hwnd_;
    }

private:
    static constexpr UINT WM_STATUS_CHANGED = WM_APP + 100;

    static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        SplashWindow* splash = reinterpret_cast<SplashWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);

            splash = static_cast<SplashWindow*>(createInfo->lpCreateParams);

            if (splash)
            {
                splash->hwnd_ = hwnd;

                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(splash));
            }
        }

        if (splash)
            return splash->WindowProc(message, wParam, lParam);

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_STATUS_CHANGED:
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
            return 0;

        case WM_PAINT:
            Paint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_NCHITTEST:
        {
            const LRESULT result = DefWindowProcW(hwnd_, message, wParam, lParam);

            if (result == HTCLIENT)
                return HTCAPTION;

            return result;
        }

        case WM_CLOSE:
            return 0;

        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;

        default:
            return DefWindowProcW(hwnd_, message, wParam, lParam);
        }
    }

    void Paint()
    {
        if (!hwnd_)
            return;

        PAINTSTRUCT paint{};
        HDC windowDc = BeginPaint(hwnd_, &paint);

        if (!windowDc)
            return;

        HDC memoryDc = CreateCompatibleDC(windowDc);

        if (!memoryDc)
        {
            EndPaint(hwnd_, &paint);
            return;
        }

        HBITMAP backBuffer = CreateCompatibleBitmap(windowDc, width_, height_);

        if (!backBuffer)
        {
            DeleteDC(memoryDc);
            EndPaint(hwnd_, &paint);
            return;
        }

        HGDIOBJ oldBitmap = SelectObject(memoryDc, backBuffer);

        {
            Gdiplus::Graphics graphics(memoryDc);

            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            Gdiplus::SolidBrush backgroundBrush(Gdiplus::Color(255, 0, 0, 0));

            graphics.FillRectangle(&backgroundBrush, 0, 0, width_, height_);

            if (image_)
            {
                constexpr int logoSize = 430;

                const int logoX = (width_ - logoSize) / 2;
                constexpr int logoY = 20;

                graphics.DrawImage(
                    image_.get(),
                    logoX,
                    logoY,
                    logoSize,
                    logoSize
                );
            }

            Gdiplus::Pen separatorPen(Gdiplus::Color(255, 55, 55, 55), 1.0f);

            graphics.DrawLine(
                &separatorPen,
                45,
                480,
                width_ - 45,
                480
            );

            std::wstring currentStatus;

            {
                std::lock_guard<std::mutex> lock(statusMutex_);
                currentStatus = status_;
            }

            Gdiplus::FontFamily fontFamily(L"Segoe UI");

            Gdiplus::Font statusFont(
                &fontFamily,
                17.0f,
                Gdiplus::FontStyleRegular,
                Gdiplus::UnitPixel
            );

            Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 225, 225, 225));

            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

            Gdiplus::RectF statusRect(
                20.0f,
                495.0f,
                static_cast<Gdiplus::REAL>(width_ - 40),
                70.0f
            );

            graphics.DrawString(
                currentStatus.c_str(),
                static_cast<INT>(currentStatus.length()),
                &statusFont,
                statusRect,
                &format,
                &textBrush
            );
        }

        BitBlt(
            windowDc,
            0,
            0,
            width_,
            height_,
            memoryDc,
            0,
            0,
            SRCCOPY
        );

        SelectObject(memoryDc, oldBitmap);
        DeleteObject(backBuffer);
        DeleteDC(memoryDc);

        EndPaint(hwnd_, &paint);
    }

    bool LoadPngResource(int resourceId)
    {
        lastError_.clear();

        HMODULE moduleHandle = GetModuleHandleW(nullptr);

        if (!moduleHandle)
        {
            lastError_ = L"GetModuleHandleW failed. Win32 error: " + std::to_wstring(GetLastError());

            return false;
        }

        HRSRC resource = FindResourceW(moduleHandle, MAKEINTRESOURCEW(resourceId), (LPCWSTR)RT_RCDATA);

        if (!resource)
        {
            const DWORD rcDataError = GetLastError();

            resource = FindResourceW(moduleHandle, MAKEINTRESOURCEW(resourceId), L"PNG");

            if (!resource)
            {
                const DWORD pngError = GetLastError();

                lastError_ =
                    L"Embedded logo resource was not found.\n\n"
                    L"Resource ID: " +
                    std::to_wstring(resourceId) +
                    L"\nRCDATA error: " +
                    std::to_wstring(rcDataError) +
                    L"\nPNG error: " +
                    std::to_wstring(pngError);

                return false;
            }
        }

        HGLOBAL loadedResource = LoadResource(moduleHandle, resource);

        if (!loadedResource)
        {
            lastError_ = L"LoadResource failed. Win32 error: " + std::to_wstring(GetLastError());

            return false;
        }

        const DWORD resourceSize = SizeofResource(moduleHandle, resource);

        if (resourceSize == 0)
        {
            lastError_ = L"SizeofResource returned zero for the logo.";

            return false;
        }

        const void* resourceData = LockResource(loadedResource);

        if (!resourceData)
        {
            lastError_ = L"LockResource returned null.";

            return false;
        }

        HGLOBAL imageMemory = GlobalAlloc(GMEM_MOVEABLE, resourceSize);

        if (!imageMemory)
        {
            lastError_ = L"GlobalAlloc failed. Win32 error: " + std::to_wstring(GetLastError());

            return false;
        }

        void* destination = GlobalLock(imageMemory);

        if (!destination)
        {
            const DWORD error = GetLastError();

            GlobalFree(imageMemory);

            lastError_ = L"GlobalLock failed. Win32 error: " + std::to_wstring(error);

            return false;
        }

        std::memcpy(destination, resourceData, resourceSize);

        GlobalUnlock(imageMemory);

        IStream* stream = nullptr;

        const HRESULT streamResult = CreateStreamOnHGlobal(imageMemory, TRUE, &stream);

        if (FAILED(streamResult) || !stream)
        {
            GlobalFree(imageMemory);

            lastError_ = L"CreateStreamOnHGlobal failed. HRESULT: " + std::to_wstring(static_cast<long>(streamResult));

            return false;
        }

        std::unique_ptr<Gdiplus::Image> decodedImage(Gdiplus::Image::FromStream(stream, FALSE));

        if (!decodedImage)
        {
            stream->Release();

            lastError_ = L"GDI+ Image::FromStream returned null.";

            return false;
        }

        const Gdiplus::Status decodeStatus = decodedImage->GetLastStatus();

        if (decodeStatus != Gdiplus::Ok)
        {
            stream->Release();

            lastError_ = L"GDI+ failed to decode the embedded PNG. Status: " +
                std::to_wstring(static_cast<int>(decodeStatus));

            return false;
        }

        Gdiplus::Image* clonedImage = decodedImage->Clone();

        if (!clonedImage)
        {
            stream->Release();

            lastError_ = L"GDI+ Image::Clone returned null.";

            return false;
        }

        image_.reset(clonedImage);

        stream->Release();

        if (image_->GetLastStatus() != Gdiplus::Ok)
        {
            lastError_ = L"The cloned GDI+ image is invalid. Status: " +
                std::to_wstring(
                    static_cast<int>(
                        image_->GetLastStatus()
                        )
                );

            image_.reset();
            return false;
        }

        return true;
    }

    void FadeTo(int startAlpha, int endAlpha, int durationMs)
    {
        if (!hwnd_)
            return;

        constexpr int steps = 25;

        const int sleepTime = std::max(1, durationMs / steps);

        for (int step = 0; step <= steps; ++step)
        {
            const double progress = static_cast<double>(step) / static_cast<double>(steps);

            const int alpha = static_cast<int>(
                startAlpha +
                ((endAlpha - startAlpha) * progress)
                );

            SetLayeredWindowAttributes(
                hwnd_,
                0,
                static_cast<BYTE>(
                    std::clamp(alpha, 0, 255)
                    ),
                LWA_ALPHA
            );

            PumpMessages();
            Sleep(sleepTime);
        }
    }

private:
    HINSTANCE hInstance_ = nullptr;
    HWND hwnd_ = nullptr;

    int width_ = 520;
    int height_ = 600;

    ULONG_PTR gdiplusToken_ = 0;

    std::wstring lastError_;

    std::unique_ptr<Gdiplus::Image> image_;

    std::mutex statusMutex_;
    std::wstring status_ = L"Starting application...";
};