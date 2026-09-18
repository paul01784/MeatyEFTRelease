#include "../InputDevice.h"
#include "../Makcu/Makcu.h"
#include "../Ferrum/Ferrum.h"
#include "../../UI/menuLayout.h"
#include "../../UI/render.h"
#include "../../external/imgui/imgui.h"
#include "../../Tarkov/Features/Aim/ReadOnlyAim.h"
#include "../../Tarkov/Features/Aim/FireportTracker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void ShowSettingsTooltip(const char* text)
    {
        if (!ImGui::IsItemHovered())
            return;

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
    }

    std::string ToUpperAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        return value;
    }

    std::string NormalizeComPort(std::string value)
    {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
        if (first == value.end())
            return {};

        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
        value.assign(first, last);
        value = ToUpperAscii(value);

        if (value.size() < 4 || value.rfind("COM", 0) != 0 ||
            !std::all_of(value.begin() + 3, value.end(), [](unsigned char character) { return std::isdigit(character) != 0; }))
        {
            return {};
        }
        return value;
    }

    void CopyStringToFixedBuffer(char* destination, std::size_t destinationSize, const std::string& source)
    {
        if (!destination || destinationSize == 0)
            return;

        const std::size_t copyCount = std::min(destinationSize - 1, source.size());
        std::memcpy(destination, source.data(), copyCount);
        destination[copyCount] = '\0';
    }

    std::string BuildPortLabel(const MakcuSerialPort& port)
    {
        std::string label = port.portName;
        if (!port.friendlyName.empty())
            label += "  -  " + port.friendlyName;
        if (port.isMakcuCandidate)
            label += "  [MAKCU candidate]";
        return label;
    }

    std::string BuildPortLabel(const FerrumSerialPort& port)
    {
        std::string label = port.portName;
        if (!port.friendlyName.empty())
            label += "  -  " + port.friendlyName;
        if (port.isFerrumCandidate)
            label += "  [Ferrum candidate]";
        return label;
    }

    template <typename Port>
    std::string GetSelectedPortPreview(const std::vector<Port>& ports, const char* configuredPort)
    {
        const std::string normalizedPort = NormalizeComPort(configuredPort ? configuredPort : "");
        for (const Port& port : ports)
        {
            if (port.portName == normalizedPort)
                return BuildPortLabel(port);
        }

        return normalizedPort.empty() ? "Select a COM port" : normalizedPort + "  -  saved selection";
    }

    const char* BoneToString(boneListIndexes key) {
        switch (key) {
        case boneListIndexes::Head: return "Head";
        case boneListIndexes::Pelvis: return "Pelvis";
        case boneListIndexes::Neck: return "Neck";
        case boneListIndexes::Spine: return "Spine";
        case boneListIndexes::LForearm: return "LForearm";
        case boneListIndexes::LPalm: return "LPalm";
        case boneListIndexes::RForearm: return "RForearm";
        case boneListIndexes::RPalm: return "RPalm";
        case boneListIndexes::LThigh: return "LThigh";
        case boneListIndexes::LFoot: return "LFoot";
        case boneListIndexes::RThigh: return "RThigh";
        case boneListIndexes::RFoot: return "RFoot";
        default: return "Unknown";
        }
    }

    void DrawTargetDebugBlock(const char* title, const std::optional<TargetResult>& target)
    {
        ImGui::SeparatorText(title);

        if (!target.has_value())
        {
            ImGui::TextDisabled("No valid target");
            return;
        }

        const glm::vec2 aimRef = readOnlyAim.resolveAimReference().pos;

        const float errorX = target->screenPos.x - aimRef.x;
        const float errorY = target->screenPos.y - aimRef.y;

        ImGui::Text(
            "Instance: 0x%llX",
            static_cast<unsigned long long>(target->player.instance)
        );

        ImGui::Text(
            "Target type: %s",
            target->player.isAi ? "AI" : "PMC / Player"
        );

        ImGui::Text(
            "Selected bone: %s (%d)",
            BoneToString(target->selectedBone),
            static_cast<int>(target->selectedBone)
        );

        ImGui::Text(
            "Bone world: %.2f, %.2f, %.2f",
            target->boneWorldPos.x,
            target->boneWorldPos.y,
            target->boneWorldPos.z
        );

        ImGui::Text(
            "Bone screen: %.1f, %.1f",
            target->screenPos.x,
            target->screenPos.y
        );

        ImGui::Text(
            "Screen error: X %.1f | Y %.1f px",
            errorX,
            errorY
        );

        ImGui::Text(
            "Screen distance: %.1f px",
            std::sqrt(target->screenDistanceSq)
        );

        ImGui::Text(
            "World distance: %.1f m",
            std::sqrt(target->worldDistanceSq)
        );
    }

    constexpr std::array<boneListIndexes, 12> kSelectableAimBones =
    {
        boneListIndexes::Pelvis,
        boneListIndexes::Head,
        boneListIndexes::Neck,
        boneListIndexes::Spine,
        boneListIndexes::LForearm,
        boneListIndexes::LPalm,
        boneListIndexes::RForearm,
        boneListIndexes::RPalm,
        boneListIndexes::LThigh,
        boneListIndexes::LFoot,
        boneListIndexes::RThigh,
        boneListIndexes::RFoot
    };

    bool ShowBoneSelectionBox(boneListIndexes& bone, const char* label)
    {
        const auto currentIt = std::find(
            kSelectableAimBones.begin(),
            kSelectableAimBones.end(),
            bone);
        const int currentIndex = currentIt == kSelectableAimBones.end()
            ? 0
            : static_cast<int>(std::distance(kSelectableAimBones.begin(), currentIt));

        if (!ImGui::BeginCombo(label, BoneToString(kSelectableAimBones[currentIndex])))
            return false;

        bool changed = false;
        for (int i = 0; i < static_cast<int>(kSelectableAimBones.size()); ++i)
        {
            const bool selected = i == currentIndex;
            if (ImGui::Selectable(BoneToString(kSelectableAimBones[i]), selected))
            {
                bone = kSelectableAimBones[i];
                changed = true;
            }

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
        return changed;
    }

}

void RenderInputDeviceWindow(bool* pOpen, float backgroundAlpha, const std::function<void()>& onConfigChanged)
{
    enum class InputDevicePage : int { Connection, Aim, Diagnostics };
    static InputDevicePage activePage = InputDevicePage::Connection;
    static InputDeviceType selectedDevice = InputDeviceType::Makcu;

    if (!pOpen || !*pOpen)
        return;

    static constexpr ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;

    static bool portsScanned = false;
    static std::vector<MakcuSerialPort> serialPorts;
    static std::vector<FerrumSerialPort> ferrumSerialPorts;

    if (!portsScanned)
    {
        serialPorts = MakcuController::EnumerateSerialPorts();
        ferrumSerialPorts = FerrumController::EnumerateSerialPorts();
        portsScanned = true;
    }

    const InputDeviceType connectedDevice = inputDevice.GetConnectedType();
    if (connectedDevice != InputDeviceType::None)
        selectedDevice = connectedDevice;
    else if (ferrumConfig.connectOnStartup && !makcuConfig.connectOnStartup)
        selectedDevice = InputDeviceType::Ferrum;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + 40.0f, viewport->Pos.y + 40.0f),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(ImVec2(800.0f, 640.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(620.0f, 450.0f),
        ImVec2(viewport->Size.x - 40.0f, viewport->Size.y - 40.0f)
    );

    ImGui::SetNextWindowBgAlpha(backgroundAlpha);

    if (!ImGui::Begin("MAKCU / FERRUM", pOpen, windowFlags))
    {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##inputDeviceNavigation", ImVec2(0.0f, 42.0f), false);
    const auto nav = [&](const char* icon, const char* label, InputDevicePage page)
    {
        if (menuLayout::TopTabButton(icon, label, activePage == page))
            activePage = page;
        ImGui::SameLine();
    };
    nav(ICON_FA_PLUG, "Connection", InputDevicePage::Connection);
    nav(ICON_FA_CROSSHAIRS, "Aim", InputDevicePage::Aim);
    nav(ICON_FA_BUG, "Diagnostics", InputDevicePage::Diagnostics);
    ImGui::EndChild();
    ImGui::BeginChild("##inputDeviceContent", ImVec2(0.0f, 0.0f), false);
    menuLayout::PushContentInset();

        // Connection
        if (activePage == InputDevicePage::Connection)
        {
            bool configChanged = false;

            ImGui::SeparatorText("Device");
            constexpr float deviceButtonWidth = 190.0f;
            constexpr float deviceButtonGap = 16.0f;
            const float selectorWidth = (deviceButtonWidth * 2.0f) + deviceButtonGap;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - selectorWidth) * 0.5f));

            const auto deviceButton = [&](const char* label, InputDeviceType device)
            {
                const bool selected = selectedDevice == device;
                const bool lockedByConnection = connectedDevice != InputDeviceType::None && connectedDevice != device;
                ImGui::BeginDisabled(lockedByConnection);
                if (selected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.12f, 0.14f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.16f, 0.18f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(deviceButtonWidth, 42.0f)) && !lockedByConnection)
                    selectedDevice = device;
                if (selected)
                    ImGui::PopStyleColor(2);
                ImGui::EndDisabled();
            };

            deviceButton("MAKCU", InputDeviceType::Makcu);
            ImGui::SameLine(0.0f, deviceButtonGap);
            deviceButton("FERRUM", InputDeviceType::Ferrum);

            if (connectedDevice != InputDeviceType::None)
                ImGui::TextDisabled("%s is connected. Disconnect it before selecting another device.", inputDevice.GetConnectedName());

            ImGui::Spacing();

            if (selectedDevice == InputDeviceType::Makcu)
            {

            ImGui::SeparatorText("Serial Port");

            const std::string selectedPreview =
                GetSelectedPortPreview(
                    serialPorts,
                    makcuConfig.comPort
                );

            const std::string selectedPort = NormalizeComPort(makcuConfig.comPort);

            const bool hasSelectedPort = !selectedPort.empty();

            ImGui::SetNextItemWidth(310.0f);

            if (ImGui::BeginCombo(
                "COM Port",
                selectedPreview.c_str()
            ))
            {
                for (const MakcuSerialPort& port : serialPorts)
                {
                    const bool selected = port.portName == selectedPort;

                    const std::string label = BuildPortLabel(port);

                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        CopyStringToFixedBuffer(
                            makcuConfig.comPort,
                            IM_ARRAYSIZE(makcuConfig.comPort),
                            port.portName
                        );

                        configChanged = true;
                    }

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }

            ImGui::Spacing();

            if (ImGui::Button("Scan Ports"))
            {
                serialPorts = MakcuController::EnumerateSerialPorts();
            }

            if (serialPorts.empty())
            {
                ImGui::TextDisabled(
                    "No active serial ports were found."
                );
            }
            else
            {
                const std::size_t makcuCandidates =
                    static_cast<std::size_t>(
                        std::count_if(
                            serialPorts.begin(),
                            serialPorts.end(),
                            [](const MakcuSerialPort& port)
                            {
                                return port.isMakcuCandidate;
                            }
                        )
                        );

                if (makcuCandidates > 0)
                {
                    ImGui::TextColored(
                        ImVec4(0.20f, 1.00f, 0.20f, 1.00f),
                        "%zu MAKCU USB-serial found",
                        makcuCandidates
                    );
                }
            }

            if (hasSelectedPort)
            {
                ImGui::Spacing();
                ImGui::SeparatorText("Connection Preferences");

                if (ImGui::Checkbox("Connect on startup", &makcuConfig.connectOnStartup))
                {
                    if (makcuConfig.connectOnStartup)
                        ferrumConfig.connectOnStartup = false;
                    configChanged = true;
                }

                ImGui::TextDisabled(
                    "Attempts to connect to %s when the application starts.",
                    selectedPort.c_str()
                );
            }
            else
            {
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "Select a COM port to configure automatic connection."
                );
            }

            if (configChanged && onConfigChanged)
                onConfigChanged();

            ImGui::Spacing();
            ImGui::SeparatorText("Native Connection");

            ImGui::TextUnformatted(
                "Automatic: 4Mbaud probe, then 115200"
            );

            ImGui::TextDisabled(
                "Baud rate is handled internally."
            );

            ImGui::Spacing();

            const bool connected = makcu.IsConnected();

            if (!connected)
            {
                ImGui::BeginDisabled(!hasSelectedPort);

                if (ImGui::Button(
                    "Connect",
                    ImVec2(150.0f, 30.0f)
                ))
                {
                    inputDevice.ConnectMakcu();
                }

                ImGui::EndDisabled();
            }
            else
            {
                if (ImGui::Button(
                    "Disconnect",
                    ImVec2(150.0f, 30.0f)
                ))
                {
                    makcu.Disconnect();
                }
            }

            ImGui::SameLine();

            if (connected)
            {
                ImGui::TextColored(
                    ImVec4(0.20f, 1.00f, 0.20f, 1.00f),
                    "Connected"
                );
            }
            else if (!hasSelectedPort)
            {
                ImGui::TextDisabled(
                    "Select a COM port"
                );
            }
            else
            {
                ImGui::TextColored(
                    ImVec4(1.00f, 0.35f, 0.35f, 1.00f),
                    "Disconnected"
                );
            }

            }
            else
            {
                ImGui::SeparatorText("Serial Port");

                const std::string selectedPreview = GetSelectedPortPreview(ferrumSerialPorts, ferrumConfig.comPort);
                const std::string selectedPort = NormalizeComPort(ferrumConfig.comPort);
                const bool hasSelectedPort = !selectedPort.empty();

                ImGui::SetNextItemWidth(310.0f);
                if (ImGui::BeginCombo("COM Port", selectedPreview.c_str()))
                {
                    for (const FerrumSerialPort& port : ferrumSerialPorts)
                    {
                        const bool selected = port.portName == selectedPort;
                        const std::string label = BuildPortLabel(port);
                        if (ImGui::Selectable(label.c_str(), selected))
                        {
                            CopyStringToFixedBuffer(ferrumConfig.comPort, IM_ARRAYSIZE(ferrumConfig.comPort), port.portName);
                            configChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::Spacing();
                if (ImGui::Button("Scan Ports"))
                    ferrumSerialPorts = FerrumController::EnumerateSerialPorts();

                if (ferrumSerialPorts.empty())
                {
                    ImGui::TextDisabled("No active serial ports were found.");
                }
                else
                {
                    const std::size_t ferrumCandidates = static_cast<std::size_t>(std::count_if(ferrumSerialPorts.begin(), ferrumSerialPorts.end(),
                        [](const FerrumSerialPort& port) { return port.isFerrumCandidate; }));
                    if (ferrumCandidates > 0)
                    {
                        ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.20f, 1.00f), "%zu Ferrum USB-serial found", ferrumCandidates);
                    }
                }

                if (hasSelectedPort)
                {
                    ImGui::Spacing();
                    ImGui::SeparatorText("Connection Preferences");
                    if (ImGui::Checkbox("Connect on startup", &ferrumConfig.connectOnStartup))
                    {
                        if (ferrumConfig.connectOnStartup)
                            makcuConfig.connectOnStartup = false;
                        configChanged = true;
                    }
                    ImGui::TextDisabled("Attempts to connect to %s when the application starts.", selectedPort.c_str());
                }
                else
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Select a COM port to configure automatic connection.");
                }

                if (configChanged && onConfigChanged)
                    onConfigChanged();

                ImGui::Spacing();
                ImGui::SeparatorText("Ferrum Connection");
                ImGui::TextUnformatted("Automatic Ferrum probe: 115200, 3M, then 4Mbaud");
                ImGui::TextDisabled("Connection is verified with km.version().");
                ImGui::Spacing();

                const bool connected = ferrum.IsConnected();
                if (!connected)
                {
                    ImGui::BeginDisabled(!hasSelectedPort || makcu.IsConnected());
                    if (ImGui::Button("Connect", ImVec2(150.0f, 30.0f)))
                        inputDevice.ConnectFerrum();
                    ImGui::EndDisabled();
                }
                else if (ImGui::Button("Disconnect", ImVec2(150.0f, 30.0f)))
                {
                    ferrum.Disconnect();
                }

                ImGui::SameLine();
                if (connected)
                    ImGui::TextColored(ImVec4(0.20f, 1.00f, 0.20f, 1.00f), "Connected");
                else if (!hasSelectedPort)
                    ImGui::TextDisabled("Select a COM port");
                else
                    ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.35f, 1.00f), "Disconnected");
            }

        }

        // Settings
        if (activePage == InputDevicePage::Aim)
        {
            bool configChanged = false;
            const bool configureFerrum = selectedDevice == InputDeviceType::Ferrum;
            float* calibrationX = configureFerrum ? &ferrum.mouseUnitsPerScreenPixelX : &makcu.mouseUnitsPerScreenPixelX;
            float* calibrationY = configureFerrum ? &ferrum.mouseUnitsPerScreenPixelY : &makcu.mouseUnitsPerScreenPixelY;
            const char* deviceName = configureFerrum ? "Ferrum" : "MAKCU";

            ImGui::SeparatorText("Aim Settings");

            configChanged |= ImGui::Checkbox(
                "Enable aim",
                &aimGlobals::aimEnabled
            );

            ImGui::BeginDisabled(!aimGlobals::aimEnabled);
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

            configChanged |= ImGui::Checkbox(
                "Prediction",
                &aimGlobals::predictionEnabled
            );
            ShowSettingsTooltip(
                "Lead moving targets and compensate for bullet drop using the chambered round, weapon/attachment velocity modifiers, and the target's live velocity."
            );

            if (aimGlobals::predictionEnabled)
            {
                const PlayerSnapshot predictionSnapshot =
                    registeredPlayers.getCacheSnapshot();
                const auto localPlayer = std::find_if(
                    predictionSnapshot->begin(),
                    predictionSnapshot->end(),
                    [](const Player& player)
                    {
                        return player.isLocal;
                    });

                if (localPlayer != predictionSnapshot->end() &&
                    localPlayer->observedHandsInfo.ballistics.IsValid())
                {
                    const BallisticsInfo& ballistics =
                        localPlayer->observedHandsInfo.ballistics;

                    ImGui::TextDisabled(
                        "Ballistics: %.0f m/s | %.2f g | BC %.3f | %.2f mm",
                        ballistics.bulletSpeed,
                        ballistics.bulletMassGrams,
                        ballistics.ballisticCoefficient,
                        ballistics.bulletDiameterMillimeters
                    );
                }
                else
                {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.55f, 0.31f, 1.0f),
                        "Prediction waiting for valid local weapon/ammo data"
                    );
                }

                const std::optional<TargetResult> activeTarget =
                    readOnlyAim.getActiveTarget();

                if (activeTarget.has_value() &&
                    activeTarget->player.velocityValid &&
                    activeTarget->player.lastVelocityUpdate !=
                        std::chrono::steady_clock::time_point{} &&
                    std::chrono::steady_clock::now() -
                        activeTarget->player.lastVelocityUpdate <=
                        std::chrono::milliseconds(250))
                {
                    ImGui::TextDisabled(
                        "Target velocity: %.2f m/s",
                        glm::length(activeTarget->player.velocity)
                    );
                }
            }

            configChanged |= ImGui::DragFloat(
                "Aim FOV",
                &aimGlobals::aimFOV,
                1.0f,
                1.0f,
                200.0f,
                "%.0f px"
            );
            ShowSettingsTooltip(
                "Maximum screen-pixel radius used to find a target from the projected fireport endpoint."
            );

            ImGui::SeparatorText("Fireport Reference");
            const FireportPose fireport = g_fireport.snapshot();

            if (fireport.aimRefOk && fireport.pathUsed)
            {
                ImGui::TextColored(
                    ImVec4(0.35f, 0.90f, 0.45f, 1.0f),
                    "Fireport tracker: READY | Path: %s",
                    fireport.pathUsed);
            }
            else
            {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.55f, 0.31f, 1.0f),
                    "Fireport tracker: RUNNING | Waiting for a valid barrel ray");
            }

            ImGui::TextDisabled("%s uses the projected fireport endpoint. Target movement pauses while it is unavailable.", deviceName);

            configChanged |= ImGui::Checkbox(
                "Show aim FOV ring",
                &aimGlobals::showAimFovRing
            );
            ShowSettingsTooltip("Draw the configured FOV circle around the projected fireport endpoint while aim is enabled and an input device is connected.");

            configChanged |= ImGui::DragInt(
                "Aim distance",
                &aimGlobals::aimDistance,
                1.0f,
                1,
                2000,
                "%d m"
            );
            ShowSettingsTooltip(
                "Maximum world distance at which targets may be selected."
            );

            configChanged |= ImGui::DragFloat(
                "Aim smoothing",
                &aimGlobals::aimSmooth,
                0.1f,
                1.0f,
                100.0f,
                "%.1f"
            );
            ShowSettingsTooltip(
                "Response divisor. Lower values react more sharply; the separate speed limit prevents large jumps."
            );

            configChanged |= ImGui::DragFloat(
                "Aim speed",
                &aimGlobals::aimSpeedPixelsPerSecond,
                25.0f,
                50.0f,
                5000.0f,
                "%.0f px/s"
            );
            ShowSettingsTooltip(
                "Maximum aim movement in screen pixels per second. Lower this to slow the approach without changing calibration."
            );

            configChanged |= ImGui::DragFloat(
                "Aim deadzone",
                &aimGlobals::aimDeadzonePixels,
                0.25f,
                0.1f,
                25.0f,
                "%.1f px"
            );
            ShowSettingsTooltip(
                "Aim stops correcting when the desired point is inside this radius, allowing a small amount of float."
            );

            configChanged |= ImGui::DragFloat(
                "Aim offset X",
                &aimGlobals::aimOffsetX,
                0.5f,
                -200.0f,
                200.0f,
                "%.1f px"
            );
            ShowSettingsTooltip(
                "Horizontal offset applied to the desired bone point. Positive values move the point right."
            );

            configChanged |= ImGui::DragFloat(
                "Aim offset Y (positive raises)",
                &aimGlobals::aimOffsetY,
                0.5f,
                -200.0f,
                200.0f,
                "%.1f px"
            );
            ShowSettingsTooltip(
                "Vertical offset applied to the desired bone point. Positive values raise the aim point."
            );

            ImGui::Spacing();
            ImGui::SeparatorText("Mouse Calibration");

            configChanged |= ImGui::DragFloat(
                "X units per screen pixel",
                calibrationX,
                0.001f,
                0.001f,
                5.0f,
                "%.4f"
            );
            ShowSettingsTooltip("Horizontal conversion factor for the selected device: screen pixels multiplied by this value become hardware movement units.");

            configChanged |= ImGui::DragFloat(
                "Y units per screen pixel",
                calibrationY,
                0.001f,
                0.001f,
                5.0f,
                "%.4f"
            );
            ShowSettingsTooltip("Vertical conversion factor for the selected device: screen pixels multiplied by this value become hardware movement units.");

            if (ImGui::Button("Reset mouse calibration"))
            {
                *calibrationX = 1.0f;
                *calibrationY = 1.0f;
                configChanged = true;
            }
            ShowSettingsTooltip(
                "Reset both calibration values to 1.0000. Use calibration for hardware sensitivity differences, not aim speed."
            );

            ImGui::Spacing();
            ImGui::SeparatorText("Target Selection");

            static constexpr const char* targetModeOptions[] =
            {
                "FOV - closest to fireport",
                "CQB - closest to local player"
            };

            int targetModeIndex = static_cast<int>(aimGlobals::targetMode);

            targetModeIndex = std::clamp(
                targetModeIndex,
                0,
                IM_ARRAYSIZE(targetModeOptions) - 1
            );

            if (ImGui::Combo(
                "Target mode",
                &targetModeIndex,
                targetModeOptions,
                IM_ARRAYSIZE(targetModeOptions)
            ))
            {
                aimGlobals::targetMode =
                    static_cast<TargetMode>(
                        targetModeIndex
                        );

                configChanged = true;
            }
            ShowSettingsTooltip(
                "FOV selects the target nearest the fireport endpoint. CQB selects the nearest target in world distance."
            );

            configChanged |= ImGui::Checkbox(
                "Lock target while key is held",
                &aimGlobals::targetLock
            );
            ShowSettingsTooltip(
                "When enabled, the first valid target remains selected until the key is released or the target becomes invalid."
            );

            ImGui::Spacing();
            ImGui::SeparatorText("Target Bone");

            configChanged |= ImGui::Checkbox(
                "Closest Bone",
                &aimGlobals::aimClosestBoneToFireport);
            ShowSettingsTooltip(
                "Uses the valid player bone nearest the projected fireport endpoint. Disable it to use the selected AI and PMC bones below."
            );

            ImGui::BeginDisabled(aimGlobals::aimClosestBoneToFireport);
            configChanged |= ShowBoneSelectionBox(
                aimGlobals::aiBone,
                "AI target bone");
            ShowSettingsTooltip(
                "Bone used for AI and Scav targets when closest-bone selection is disabled."
            );

            configChanged |= ShowBoneSelectionBox(
                aimGlobals::pmcBone,
                "PMC target bone");
            ShowSettingsTooltip(
                "Bone used for PMC targets when closest-bone selection is disabled."
            );
            ImGui::EndDisabled();

            ImGui::PopItemWidth();
            ImGui::EndDisabled();

            if (configChanged && onConfigChanged)
                onConfigChanged();

        }

        // Debug
        if (activePage == InputDevicePage::Diagnostics)
        {
            const bool showFerrum = selectedDevice == InputDeviceType::Ferrum;
            const MakcuDiagnostics makcuDiagnostics = makcu.GetDiagnostics();
            const FerrumDiagnostics ferrumDiagnostics = ferrum.GetDiagnostics();
            const bool connected = showFerrum ? ferrumDiagnostics.connected : makcuDiagnostics.connected;
            const std::string connectedPort = showFerrum ? ferrumDiagnostics.connectedPort : makcuDiagnostics.connectedPort;
            const std::uint32_t connectedBaudRate = showFerrum ? ferrumDiagnostics.connectedBaudRate : makcuDiagnostics.connectedBaudRate;
            const std::string firmwareVersion = showFerrum ? ferrumDiagnostics.firmwareVersion : makcuDiagnostics.firmwareVersion;
            const std::string lastReply = showFerrum ? ferrumDiagnostics.lastReply : makcuDiagnostics.lastReply;
            const std::string lastError = showFerrum ? ferrumDiagnostics.lastError : makcuDiagnostics.lastError;
            const char* deviceName = showFerrum ? "Ferrum" : "MAKCU";
            const float calibrationX = showFerrum ? ferrum.mouseUnitsPerScreenPixelX : makcu.mouseUnitsPerScreenPixelX;
            const float calibrationY = showFerrum ? ferrum.mouseUnitsPerScreenPixelY : makcu.mouseUnitsPerScreenPixelY;

            const std::optional<TargetResult> bestTarget =
                readOnlyAim.getLiveTarget();

            const std::optional<TargetResult> activeTarget =
                readOnlyAim.getActiveTarget();

            ImGui::SeparatorText("Aim State");

            ImGui::Text(
                "Aim enabled: %s",
                aimGlobals::aimEnabled
                ? "true"
                : "false"
            );

            ImGui::Text(
                "Target lock: %s",
                aimGlobals::targetLock
                ? "true"
                : "false"
            );

            ImGui::Text(
                "Target mode: %s",
                aimGlobals::targetMode == TargetMode::CQB
                ? "CQB"
                : "FOV"
            );

            ImGui::Text(
                "Aim FOV: %.0f px",
                aimGlobals::aimFOV
            );

            ImGui::Text(
                "Aim distance: %d m",
                aimGlobals::aimDistance
            );

            ImGui::Text("Aim reference: Fireport (required)");

            ImGui::Text(
                "Aim smoothing: %.2f",
                aimGlobals::aimSmooth
            );

            ImGui::Text(
                "Aim speed/deadzone: %.0f px/s | %.1f px",
                aimGlobals::aimSpeedPixelsPerSecond,
                aimGlobals::aimDeadzonePixels
            );

            ImGui::Text(
                "Aim offset: X %.1f px | Y %.1f px",
                aimGlobals::aimOffsetX,
                aimGlobals::aimOffsetY
            );

            ImGui::Text(
                "Mouse calibration: X %.4f | Y %.4f",
                calibrationX,
                calibrationY
            );

            ImGui::Text(
                "Closest fireport bone: %s",
                aimGlobals::aimClosestBoneToFireport ? "Enabled" : "Disabled");

            if (!aimGlobals::aimClosestBoneToFireport)
            {
                ImGui::Text(
                    "AI / Scav bone: %s | PMC bone: %s",
                    BoneToString(aimGlobals::aiBone),
                    BoneToString(aimGlobals::pmcBone));
            }

            ImGui::Spacing();

            DrawTargetDebugBlock(
                "Best Target (Live)",
                bestTarget
            );

            ImGui::Spacing();

            DrawTargetDebugBlock(
                "Active Target",
                activeTarget
            );

            ImGui::Spacing();
            ImGui::SeparatorText(showFerrum ? "Ferrum Runtime" : "MAKCU Runtime");

            ImGui::Text(
                "Connection state: %s",
                connected
                ? "Connected"
                : "Disconnected"
            );

            if (connected)
            {
                ImGui::TextColored(
                    ImVec4(0.20f, 1.00f, 0.20f, 1.00f),
                    "Connected: %s @ %u baud",
                    connectedPort.c_str(),
                    connectedBaudRate
                );
            }
            else
            {
                ImGui::TextColored(
                    ImVec4(1.00f, 0.35f, 0.35f, 1.00f),
                    "No active %s connection",
                    deviceName
                );
            }

            ImGui::Text(
                "Firmware: %s",
                firmwareVersion.empty()
                ? "Not queried"
                : firmwareVersion.c_str()
            );

            if (ImGui::Button(
                "Refresh Diagnostics",
                ImVec2(170.0f, 28.0f)
            ))
            {
                if (connected)
                {
                    if (showFerrum)
                        ferrum.RefreshDiagnostics();
                    else
                        makcu.RefreshDiagnostics();
                }
            }

            static std::string mouseTestResult;

            ImGui::Spacing();
            ImGui::SeparatorText("Mouse Movement Test");
            ImGui::TextDisabled(
                "Sends one randomized M path: roughly 260-340 by 130-170 hardware units."
            );

            ImGui::BeginDisabled(!connected || aimGlobals::aimEnabled);
            if (ImGui::Button(
                "Test randomized M movement",
                ImVec2(220.0f, 28.0f)
            ))
            {
                const bool testSucceeded = showFerrum ? ferrum.TestMouseMovement() : makcu.TestMouseMovement();
                mouseTestResult = testSucceeded
                    ? "M movement test completed"
                    : "M movement test failed; see Last Error";
            }
            ImGui::EndDisabled();
            ShowSettingsTooltip(
                "Moves the mouse only after this button is clicked. Disable aim first so the test has exclusive access to the device."
            );

            if (aimGlobals::aimEnabled)
            {
                ImGui::TextDisabled("Disable aim to run the mouse movement test.");
            }

            if (!mouseTestResult.empty())
            {
                const ImVec4 resultColour = mouseTestResult.ends_with("completed")
                    ? ImVec4(0.20f, 1.00f, 0.20f, 1.0f)
                    : ImVec4(1.00f, 0.35f, 0.35f, 1.0f);

                ImGui::TextColored(resultColour, "%s", mouseTestResult.c_str());
            }

            if (!lastError.empty())
            {
                ImGui::Spacing();
                ImGui::SeparatorText("Last Error");

                ImGui::TextWrapped(
                    "%s",
                    lastError.c_str()
                );
            }

            if (ImGui::CollapsingHeader(
                "Last Device Reply",
                ImGuiTreeNodeFlags_DefaultOpen
            ))
            {
                ImGui::TextWrapped(
                    "%s",
                    lastReply.empty()
                    ? "No device reply yet"
                    : lastReply.c_str()
                );
            }

        }

    menuLayout::PopContentInset();
    ImGui::EndChild();

    ImGui::End();
}

