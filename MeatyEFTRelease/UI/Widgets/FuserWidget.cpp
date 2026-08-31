#include "FuserWidget.h"

#include "../includes.h"
#include "../config.h"
#include "../DxRenderWindow.h"
#include "../fuserRender.h"
#include "../globals.h"
#include "../menuLayout.h"

#include <algorithm>

namespace uiWidgets
{
static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return "";

    const int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (needed <= 1)
        return "";

    std::string result(static_cast<size_t>(needed - 1), '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        needed,
        nullptr,
        nullptr
    );

    return result;
}

static std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return L"";

    const int needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        nullptr,
        0
    );

    if (needed <= 1)
        return L"";

    std::wstring result(static_cast<size_t>(needed - 1), L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.c_str(),
        -1,
        result.data(),
        needed
    );

    return result;
}

static std::string BuildMonitorLabel(const DxMonitorInfo& monitor)
{
    std::string name = WideToUtf8(monitor.name);

    if (name.empty())
        name = WideToUtf8(monitor.deviceName);

    if (name.empty())
        name = "Monitor";

    std::string label =
        name +
        " (" +
        std::to_string(monitor.x) +
        ", " +
        std::to_string(monitor.y) +
        ") " +
        std::to_string(monitor.width) +
        "x" +
        std::to_string(monitor.height) +
        " @" +
        std::to_string(monitor.refreshRate) +
        "Hz";

    if (monitor.primary)
        label += " [Primary]";

    return label;
}

static int GetFontIndexFromName(const std::wstring& fontName)
{
    static const wchar_t* fonts[] =
    {
        L"Arial",
        L"Segoe UI",
        L"Tahoma",
        L"Verdana",
        L"Calibri"
    };

    for (int i = 0; i < IM_ARRAYSIZE(fonts); ++i)
    {
        if (_wcsicmp(fontName.c_str(), fonts[i]) == 0)
            return i;
    }

    return 0;
}

static const wchar_t* GetFontNameFromIndex(int index)
{
    static const wchar_t* fonts[] =
    {
        L"Arial",
        L"Segoe UI",
        L"Tahoma",
        L"Verdana",
        L"Calibri"
    };

    index = std::clamp(index, 0, IM_ARRAYSIZE(fonts) - 1);
    return fonts[index];
}

static const char* GetFontPreviewName(int index)
{
    static const char* fonts[] =
    {
        "Arial",
        "Segoe UI",
        "Tahoma",
        "Verdana",
        "Calibri"
    };

    index = std::clamp(index, 0, IM_ARRAYSIZE(fonts) - 1);
    return fonts[index];
}

static void RestartDxFuserWindow(DxWindowConfig& cfg)
{
    if (g_DxWindow.IsRunning())
        g_DxWindow.Stop();

    g_DxWindow.Init(cfg);
    g_DxWindow.Start();
}

static void ApplyAndSaveFuserConfig(DxWindowConfig& cfg)
{
    cfg.defaultFont.italic = false;

    g_DxWindow.SetConfig(cfg);
    configManager.SaveConfig();
}

static void RestartAndSaveFuserWindow(DxWindowConfig& cfg)
{
    cfg.defaultFont.italic = false;

    g_DxWindow.SetConfig(cfg);
    configManager.SaveConfig();

    RestartDxFuserWindow(cfg);
}

void renderFuserWindow()
{
    enum class FuserPage : int { Control, Window, Render, Diagnostics };
    static FuserPage activePage = FuserPage::Control;
    static bool firstLoad = true;

    if (firstLoad)
    {
        g_DxWindow.RefreshMonitorList();
        firstLoad = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 30.0f, viewport->Pos.y + 30.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760.0f, 610.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(620.0f, 450.0f), ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f));
    ImGui::SetNextWindowBgAlpha(globals::appWindowAlpha);

    if (!ImGui::Begin("Fuser", &appMenu::appFuser, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    DxWindowConfig editorConfig = g_DxWindow.GetConfig();
    ImGui::BeginChild("##fuserNavigation", ImVec2(0.0f, 42.0f), false);
    const auto nav = [&](const char* icon, const char* label, FuserPage page)
    {
        if (menuLayout::TopTabButton(icon, label, activePage == page))
            activePage = page;
        ImGui::SameLine();
    };
    nav(ICON_FA_DISPLAY, "Control", FuserPage::Control);
    nav(ICON_FA_DESKTOP, "Window", FuserPage::Window);
    nav(ICON_FA_EYE, "Render", FuserPage::Render);
    nav(ICON_FA_BUG, "Diagnostics", FuserPage::Diagnostics);
    ImGui::EndChild();
    ImGui::BeginChild("##fuserContent", ImVec2(0.0f, 0.0f), false);
    menuLayout::PushContentInset();
    if (activePage == FuserPage::Control)
    {
        const bool running = g_DxWindow.IsRunning();
        if (menuLayout::Section("Fuser window"))
        {
            if (ImGui::Button(running ? "Stop" : "Launch", ImVec2(130.0f, 30.0f)))
            {
                if (running)
                    g_DxWindow.Stop();
                else
                {
                    g_DxWindow.Init(editorConfig);
                    g_DxWindow.Start();
                }
            }
            ImGui::SameLine();
            ImGui::TextColored(
                running ? ImVec4(0.2f, 1.0f, 0.35f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                running ? "Running" : "Stopped"
            );
        }
    }
    else if (activePage == FuserPage::Window)
    {
        bool changed = false;
        if (menuLayout::Section("Monitor"))
        {
            if (ImGui::Button("Refresh monitors", ImVec2(150.0f, 26.0f)))
                g_DxWindow.RefreshMonitorList();

            std::vector<DxMonitorInfo> monitors = g_DxWindow.GetMonitors();
            if (monitors.empty())
            {
                g_DxWindow.RefreshMonitorList();
                monitors = g_DxWindow.GetMonitors();
            }
            if (monitors.empty())
            {
                ImGui::TextDisabled("No monitors found.");
            }
            else
            {
                if (editorConfig.monitorIndex < 0 || editorConfig.monitorIndex >= static_cast<int>(monitors.size()))
                {
                    editorConfig.monitorIndex = 0;
                    changed = true;
                }
                std::vector<std::string> labels;
                labels.reserve(monitors.size());
                for (const auto& monitor : monitors)
                    labels.push_back(BuildMonitorLabel(monitor));
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("Selected monitor", labels[editorConfig.monitorIndex].c_str()))
                {
                    for (int i = 0; i < static_cast<int>(labels.size()); ++i)
                    {
                        const bool selected = editorConfig.monitorIndex == i;
                        if (ImGui::Selectable(labels[i].c_str(), selected))
                        {
                            editorConfig.monitorIndex = i;
                            changed = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }
        if (menuLayout::BeginTwoColumns("##fuserWindowColumns"))
        {
            menuLayout::NextColumn();
            if (menuLayout::Section("Behaviour"))
            {
                changed |= menuLayout::ToggleRow("Start automatically", "fuserAutoStart", &editorConfig.autoStart);
                changed |= menuLayout::ToggleRow("Fullscreen", "fuserFullscreen", &editorConfig.fullscreen);
                if (editorConfig.fullscreen)
                {
                    editorConfig.borderless = true;
                    editorConfig.useMonitorSize = true;
                }
                else
                {
                    editorConfig.useMonitorSize = false;
                }
                changed |= menuLayout::ToggleRow("Borderless", "fuserBorderless", &editorConfig.borderless);
                changed |= menuLayout::ToggleRow("Always on top", "fuserTopMost", &editorConfig.topMost);
                changed |= menuLayout::ToggleRow("Show in taskbar", "fuserTaskbar", &editorConfig.showInTaskbar);
            }
            menuLayout::NextColumn();
            if (menuLayout::Section("Background"))
            {
                changed |= menuLayout::ToggleRow("Transparent", "fuserTransparent", &editorConfig.transparentBackground);
                if (!editorConfig.transparentBackground)
                    changed |= menuLayout::ColourRow("Colour", "fuserBackground", (float*)&editorConfig.backgroundColour);
            }
            if (menuLayout::Section("Overlay alignment"))
            {
                changed |= menuLayout::SliderFloatRow(
                    "Horizontal (right +)",
                    "fuserRenderOffsetX",
                    &editorConfig.renderOffsetX,
                    -100.0f,
                    100.0f,
                    "%.1f px");
                changed |= menuLayout::SliderFloatRow(
                    "Vertical (down +)",
                    "fuserRenderOffsetY",
                    &editorConfig.renderOffsetY,
                    -100.0f,
                    100.0f,
                    "%.1f px");

                if (ImGui::Button("Reset alignment", ImVec2(150.0f, 26.0f)))
                {
                    editorConfig.renderOffsetX = 0.0f;
                    editorConfig.renderOffsetY = 0.0f;
                    changed = true;
                }

                ImGui::TextDisabled(
                    "Moves every Fuser draw in physical pixels. Aim is unchanged.");
            }
            menuLayout::EndTwoColumns();
        }
        if (changed)
            ApplyAndSaveFuserConfig(editorConfig);
        if (ImGui::Button("Restart window", ImVec2(150.0f, 28.0f)))
            RestartAndSaveFuserWindow(editorConfig);
    }
    else if (activePage == FuserPage::Render)
    {
        bool changed = false;
        if (menuLayout::BeginTwoColumns("##fuserRenderColumns"))
        {
            menuLayout::NextColumn();
            if (menuLayout::Section("Frame timing"))
            {
                changed |= menuLayout::ToggleRow("Use VSync", "fuserVsync", &editorConfig.useVSync);
                changed |= menuLayout::ToggleRow("Use monitor refresh", "fuserMonitorRefresh", &editorConfig.useMonitorRefreshRate);
                if (!editorConfig.useVSync && !editorConfig.useMonitorRefreshRate)
                    changed |= menuLayout::SliderIntRow("Maximum FPS", "fuserMaxFps", &editorConfig.maxFPS, 30, 360, "%d FPS");
            }
            menuLayout::NextColumn();
            if (menuLayout::Section("Scale & quality"))
            {
                changed |= menuLayout::ToggleRow("Anti-aliasing", "fuserAa", &editorConfig.antiAliasing);
                changed |= menuLayout::ToggleRow("Use DPI scale", "fuserDpi", &editorConfig.useDpiScale);
                changed |= menuLayout::SliderFloatRow("Render scale", "fuserScale", &editorConfig.renderScale, 0.50f, 2.50f, "%.2fx");
                editorConfig.renderScale = std::clamp(editorConfig.renderScale, 0.05f, 5.0f);
            }
            menuLayout::EndTwoColumns();
        }
        if (menuLayout::Section("Font"))
        {
            int selectedFontIndex = GetFontIndexFromName(editorConfig.defaultFont.name);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Default font", GetFontPreviewName(selectedFontIndex)))
            {
                for (int i = 0; i < 5; ++i)
                {
                    const bool selected = selectedFontIndex == i;
                    if (ImGui::Selectable(GetFontPreviewName(i), selected))
                    {
                        editorConfig.defaultFont.name = GetFontNameFromIndex(i);
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            changed |= menuLayout::ToggleRow("Bold", "fuserBold", &editorConfig.defaultFont.bold);
        }
        if (changed)
            ApplyAndSaveFuserConfig(editorConfig);
        if (ImGui::Button("Apply live", ImVec2(120.0f, 28.0f)))
            ApplyAndSaveFuserConfig(editorConfig);
        ImGui::SameLine();
        if (ImGui::Button("Restart window", ImVec2(150.0f, 28.0f)))
            RestartAndSaveFuserWindow(editorConfig);
    }
    else
    {
        if (menuLayout::Section("Test scene"))
        {
            bool testScene = fuserRender::IsTestSceneEnabled();
            if (menuLayout::ToggleRow("Render test scene", "fuserTestScene", &testScene))
                fuserRender::SetTestSceneEnabled(testScene);
        }
        if (menuLayout::Section("Selected monitor"))
        {
            const std::vector<DxMonitorInfo> monitors = g_DxWindow.GetMonitors();
            if (editorConfig.monitorIndex >= 0 && editorConfig.monitorIndex < static_cast<int>(monitors.size()))
            {
                const DxMonitorInfo& monitor = monitors[editorConfig.monitorIndex];
                ImGui::Text("Name: %s", WideToUtf8(monitor.name).c_str());
                ImGui::Text("Device: %s", WideToUtf8(monitor.deviceName).c_str());
                ImGui::Text("Position: %d, %d", monitor.x, monitor.y);
                ImGui::Text("Size: %d x %d @ %d Hz", monitor.width, monitor.height, monitor.refreshRate);
            }
            else
            {
                ImGui::TextDisabled("No selected monitor data.");
            }
        }
        if (menuLayout::Section("Runtime"))
        {
            ImGui::Text("Window ready: %s", g_DxWindow.IsWindowReady() ? "Yes" : "No");
            ImGui::Text("Window size: %d x %d", g_DxWindow.GetWindowWidth(), g_DxWindow.GetWindowHeight());
            ImGui::Text("Final scale: %.2f", g_DxWindow.GetFinalRenderScale());
            ImGui::Text(
                "Overlay offset: X %.1f px | Y %.1f px",
                editorConfig.renderOffsetX,
                editorConfig.renderOffsetY);
            ImGui::Text("Window handle: %s", g_DxWindow.GetHWND() ? "Valid" : "None");
        }
    }
    menuLayout::PopContentInset();
    ImGui::EndChild();
    ImGui::End();
}

}
