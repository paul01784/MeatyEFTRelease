#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct MakcuConfig
{
    char comPort[32] = "";
    bool connectOnStartup = false;
};

struct MakcuSerialPort
{
    std::string portName;       // COM7
    std::string friendlyName;   // USB-SERIAL CH340 (COM7)
    std::string hardwareId;     // USB\VID_1A86&PID_55D3
    bool isMakcuCandidate = false;
};

struct MakcuDiagnostics
{
    bool connected = false;

    std::string connectedPort;
    std::uint32_t connectedBaudRate = 0;

    std::string firmwareVersion;

    std::string lastReply;
    std::string lastError;
};

enum class MakcuMouseButton : std::uint8_t
{
    Left,
    Right,
    Middle,
    Ms1,
    Ms2
};

class MakcuController
{
public:
    MakcuController() = default;
    ~MakcuController();

    MakcuController(const MakcuController&) = delete;
    MakcuController& operator=(const MakcuController&) = delete;

    bool Connect(const MakcuConfig& config);
    bool Connect(const char* comPort);

    void Disconnect();

    bool IsConnected() const;
    MakcuDiagnostics GetDiagnostics() const;

    static std::vector<MakcuSerialPort> EnumerateSerialPorts();

    bool RefreshDiagnostics();

    bool SendCommand(const std::string& command, std::string* reply = nullptr, std::uint32_t timeoutMs = 100);

    bool Move(int dx, int dy, std::uint32_t timeoutMs = 100);

    bool Wheel(int delta, std::uint32_t timeoutMs = 100);

    bool ButtonDown(MakcuMouseButton button, std::uint32_t timeoutMs = 100);

    bool ButtonUp(MakcuMouseButton button, std::uint32_t timeoutMs = 100);

    bool ButtonForceRelease(MakcuMouseButton button, std::uint32_t timeoutMs = 100);

private:
    bool OpenPortLocked(const char* comPort, std::uint32_t baudRate);

    void ClosePortLocked();

    bool SwitchToNativeBaudLocked();

    bool ProbeVersionLocked(
        std::uint32_t timeoutMs
    );

    bool ExecuteAsciiLocked(const std::string& command, std::string* reply, std::uint32_t timeoutMs);

    bool WriteAllLocked(const void* data, std::size_t size, bool flushAfterWrite);

    bool SendButtonLocked(MakcuMouseButton button, int state, std::uint32_t timeoutMs);

    static std::string BuildPortPath(const char* comPort);

    static std::string GetWin32ErrorMessage(DWORD errorCode);

    static std::string CleanReply(std::string reply);

    static void TrimInPlace(std::string& value);

    void SetErrorLocked(const std::string& error);

private:
    mutable std::mutex mutex_;

    HANDLE serialHandle_ = INVALID_HANDLE_VALUE;

    std::string connectedPort_;
    std::uint32_t connectedBaudRate_ = 0;

    std::string firmwareVersion_;

    std::string lastReply_;
    std::string lastError_;

public:
    float mouseUnitsPerScreenPixelX = 1.f;
    float mouseUnitsPerScreenPixelY = 1.0f;
};

extern MakcuController makcu;
extern MakcuConfig makcuConfig;

// Our menu render function
void RenderMakcuWindow(bool* pOpen, float backgroundAlpha = 1.0f, const std::function<void()>& onConfigChanged = {});