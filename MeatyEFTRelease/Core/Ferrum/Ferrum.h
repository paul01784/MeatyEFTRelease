#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct FerrumConfig
{
    char comPort[32] = "";
    bool connectOnStartup = false;
    float mouseUnitsPerScreenPixelX = 1.0f;
    float mouseUnitsPerScreenPixelY = 1.0f;
};

struct FerrumSerialPort
{
    std::string portName;
    std::string friendlyName;
    std::string hardwareId;
    bool isFerrumCandidate = false;
};

struct FerrumDiagnostics
{
    bool connected = false;
    std::string connectedPort;
    std::uint32_t connectedBaudRate = 0;
    std::string firmwareVersion;
    std::string lastReply;
    std::string lastError;
};

enum class FerrumMouseButton : std::uint8_t
{
    Left,
    Right,
    Middle,
    Ms1,
    Ms2
};

class FerrumController
{
public:
    FerrumController() = default;
    ~FerrumController();

    FerrumController(const FerrumController&) = delete;
    FerrumController& operator=(const FerrumController&) = delete;

    bool Connect(const FerrumConfig& config);
    bool Connect(const char* comPort);
    void Disconnect();

    [[nodiscard]] bool IsConnected() const;
    [[nodiscard]] FerrumDiagnostics GetDiagnostics() const;

    static std::vector<FerrumSerialPort> EnumerateSerialPorts();

    bool RefreshDiagnostics();
    bool SendCommand(const std::string& command, std::string* reply = nullptr, std::uint32_t timeoutMs = 100);
    bool Move(int dx, int dy, std::uint32_t timeoutMs = 100);
    bool TestMouseMovement();
    bool Wheel(int delta, std::uint32_t timeoutMs = 100);
    bool ButtonDown(FerrumMouseButton button, std::uint32_t timeoutMs = 100);
    bool ButtonUp(FerrumMouseButton button, std::uint32_t timeoutMs = 100);
    bool ButtonForceRelease(FerrumMouseButton button, std::uint32_t timeoutMs = 100);
    bool Click(FerrumMouseButton button, std::uint32_t holdMs = 8, std::uint32_t timeoutMs = 100);

    float mouseUnitsPerScreenPixelX = 1.0f;
    float mouseUnitsPerScreenPixelY = 1.0f;

private:
    bool OpenPortLocked(const char* comPort, std::uint32_t baudRate);
    void ClosePortLocked();
    bool ProbeVersionLocked(std::uint32_t timeoutMs);
    bool ExecuteAsciiLocked(const std::string& command, std::string* reply, std::uint32_t timeoutMs);
    bool ReadLineLocked(std::string& reply, std::uint32_t timeoutMs);
    bool WriteAllLocked(const void* data, std::size_t size);
    bool SendButtonLocked(FerrumMouseButton button, int state, std::uint32_t timeoutMs);

    static std::string BuildPortPath(const char* comPort);
    static std::string GetWin32ErrorMessage(DWORD errorCode);
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
};

extern FerrumController ferrum;
extern FerrumConfig ferrumConfig;
