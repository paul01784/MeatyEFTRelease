#include "Ferrum.h"

#include <SetupAPI.h>
#include <devguid.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>
#include <random>
#include <string_view>
#include <thread>
#include <utility>

#pragma comment(lib, "setupapi.lib")

namespace
{
    constexpr std::array<std::uint32_t, 3> kFerrumBaudRates = {115200, 3000000, 4000000};
    constexpr std::string_view kVersionCommand = "km.version()";
    constexpr std::string_view kVersionReply = "kmbox: Ferrum";

    std::string ToUpperAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        return value;
    }

    bool ContainsInsensitive(const std::string& value, std::string_view needle)
    {
        return ToUpperAscii(value).find(ToUpperAscii(std::string(needle))) != std::string::npos;
    }

    bool IsComPortName(const std::string& value)
    {
        if (value.size() < 4)
            return false;

        const std::string upper = ToUpperAscii(value);
        if (upper.rfind("COM", 0) != 0)
            return false;

        return std::all_of(upper.begin() + 3, upper.end(), [](unsigned char character) { return std::isdigit(character) != 0; });
    }

    std::string NormalizeComPort(std::string value)
    {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
        value = first < last ? std::string(first, last) : std::string{};
        value = ToUpperAscii(value);
        return IsComPortName(value) ? value : std::string{};
    }

    int GetComPortNumber(const std::string& portName)
    {
        if (!IsComPortName(portName))
            return std::numeric_limits<int>::max();

        int result = 0;
        for (std::size_t index = 3; index < portName.size(); ++index)
            result = (result * 10) + (portName[index] - '0');
        return result;
    }

    std::string ExtractComPort(const std::string& value)
    {
        const std::string upper = ToUpperAscii(value);
        const std::size_t marker = upper.find("(COM");
        if (marker == std::string::npos)
            return {};

        const std::size_t end = upper.find(')', marker);
        if (end == std::string::npos)
            return {};

        return NormalizeComPort(upper.substr(marker + 1, end - marker - 1));
    }

    std::string ReadSetupApiProperty(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData, DWORD property)
    {
        DWORD requiredSize = 0;
        DWORD propertyType = 0;
        SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData, property, &propertyType, nullptr, 0, &requiredSize);
        if (requiredSize == 0)
            return {};

        std::vector<char> buffer(requiredSize + 1, '\0');
        if (!SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData, property, &propertyType, reinterpret_cast<PBYTE>(buffer.data()),
                                               static_cast<DWORD>(buffer.size()), &requiredSize))
        {
            return {};
        }

        return std::string(buffer.data());
    }

    std::string ReadRegistryPortName(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData)
    {
        HKEY deviceKey = SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (deviceKey == INVALID_HANDLE_VALUE)
            return {};

        char portBuffer[64]{};
        DWORD valueType = 0;
        DWORD valueSize = sizeof(portBuffer);
        const LONG result = RegQueryValueExA(deviceKey, "PortName", nullptr, &valueType, reinterpret_cast<LPBYTE>(portBuffer), &valueSize);
        RegCloseKey(deviceKey);

        if (result != ERROR_SUCCESS || valueType != REG_SZ)
            return {};

        return NormalizeComPort(portBuffer);
    }

    const char* GetButtonCommandName(FerrumMouseButton button)
    {
        switch (button)
        {
        case FerrumMouseButton::Left:
            return "left";
        case FerrumMouseButton::Right:
            return "right";
        case FerrumMouseButton::Middle:
            return "middle";
        case FerrumMouseButton::Ms1:
            return "side1";
        case FerrumMouseButton::Ms2:
            return "side2";
        default:
            return nullptr;
        }
    }
}

FerrumController ferrum{};
FerrumConfig ferrumConfig{};

FerrumController::~FerrumController()
{
    Disconnect();
}

bool FerrumController::Connect(const FerrumConfig& config)
{
    return Connect(config.comPort);
}

bool FerrumController::Connect(const char* comPort)
{
    std::scoped_lock lock(mutex_);
    const std::string normalizedPort = NormalizeComPort(comPort ? comPort : "");
    if (normalizedPort.empty())
    {
        SetErrorLocked("Select a valid COM port first");
        return false;
    }

    ClosePortLocked();
    firmwareVersion_.clear();
    lastReply_.clear();
    lastError_.clear();

    for (const std::uint32_t baudRate : kFerrumBaudRates)
    {
        if (!OpenPortLocked(normalizedPort.c_str(), baudRate))
            continue;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        PurgeComm(serialHandle_, PURGE_RXABORT | PURGE_RXCLEAR);

        if (ProbeVersionLocked(350))
            return true;

        ClosePortLocked();
    }

    ClosePortLocked();
    SetErrorLocked("Ferrum did not respond at 115200, 3000000, or 4000000 baud");
    return false;
}

void FerrumController::Disconnect()
{
    std::scoped_lock lock(mutex_);
    ClosePortLocked();
}

bool FerrumController::IsConnected() const
{
    std::scoped_lock lock(mutex_);
    return serialHandle_ != INVALID_HANDLE_VALUE && !connectedPort_.empty() && connectedBaudRate_ != 0;
}

FerrumDiagnostics FerrumController::GetDiagnostics() const
{
    std::scoped_lock lock(mutex_);
    FerrumDiagnostics diagnostics{};
    diagnostics.connected = serialHandle_ != INVALID_HANDLE_VALUE && !connectedPort_.empty() && connectedBaudRate_ != 0;
    diagnostics.connectedPort = connectedPort_;
    diagnostics.connectedBaudRate = connectedBaudRate_;
    diagnostics.firmwareVersion = firmwareVersion_;
    diagnostics.lastReply = lastReply_;
    diagnostics.lastError = lastError_;
    return diagnostics;
}

std::vector<FerrumSerialPort> FerrumController::EnumerateSerialPorts()
{
    std::vector<FerrumSerialPort> ports;
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);

    if (deviceInfoSet != INVALID_HANDLE_VALUE)
    {
        for (DWORD deviceIndex = 0;; ++deviceIndex)
        {
            SP_DEVINFO_DATA deviceInfoData{};
            deviceInfoData.cbSize = sizeof(deviceInfoData);
            if (!SetupDiEnumDeviceInfo(deviceInfoSet, deviceIndex, &deviceInfoData))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;
                continue;
            }

            std::string friendlyName = ReadSetupApiProperty(deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME);
            if (friendlyName.empty())
                friendlyName = ReadSetupApiProperty(deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC);

            const std::string hardwareId = ReadSetupApiProperty(deviceInfoSet, deviceInfoData, SPDRP_HARDWAREID);
            std::string portName = ExtractComPort(friendlyName);
            if (portName.empty())
                portName = ReadRegistryPortName(deviceInfoSet, deviceInfoData);
            if (portName.empty())
                continue;

            FerrumSerialPort port{};
            port.portName = std::move(portName);
            port.friendlyName = std::move(friendlyName);
            port.hardwareId = hardwareId;
            port.isFerrumCandidate = ContainsInsensitive(hardwareId, "VID_10C4") || ContainsInsensitive(port.friendlyName, "SILICON LABS") ||
                                     ContainsInsensitive(port.friendlyName, "CP210");
            ports.push_back(std::move(port));
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }

    std::sort(ports.begin(), ports.end(), [](const FerrumSerialPort& left, const FerrumSerialPort& right)
    {
        const int leftNumber = GetComPortNumber(left.portName);
        const int rightNumber = GetComPortNumber(right.portName);
        return leftNumber != rightNumber ? leftNumber < rightNumber : left.portName < right.portName;
    });

    ports.erase(std::unique(ports.begin(), ports.end(), [](const FerrumSerialPort& left, const FerrumSerialPort& right)
    {
        return left.portName == right.portName;
    }), ports.end());

    return ports;
}

bool FerrumController::RefreshDiagnostics()
{
    std::scoped_lock lock(mutex_);
    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("Ferrum is not connected");
        return false;
    }
    return ProbeVersionLocked(500);
}

bool FerrumController::SendCommand(const std::string& command, std::string* reply, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);
    return ExecuteAsciiLocked(command, reply, timeoutMs);
}

bool FerrumController::Move(int dx, int dy, std::uint32_t timeoutMs)
{
    if (dx < -32768 || dx > 32767 || dy < -32768 || dy > 32767)
    {
        std::scoped_lock lock(mutex_);
        SetErrorLocked("Ferrum move coordinates are outside int16 range");
        return false;
    }

    std::scoped_lock lock(mutex_);
    return ExecuteAsciiLocked("km.move(" + std::to_string(dx) + "," + std::to_string(dy) + ")", nullptr, timeoutMs);
}

bool FerrumController::TestMouseMovement()
{
    std::random_device randomDevice;
    std::mt19937 randomEngine(randomDevice());
    std::uniform_int_distribution<int> widthDistribution(260, 340);
    std::uniform_int_distribution<int> heightDistribution(130, 170);
    std::uniform_int_distribution<int> originDistribution(-90, 90);

    const int width = widthDistribution(randomEngine);
    const int height = heightDistribution(randomEngine);
    const int halfWidth = width / 2;
    const int leftWidth = width - halfWidth;
    const int originX = originDistribution(randomEngine);
    const int originY = originDistribution(randomEngine);
    const std::array<std::pair<int, int>, 5> moves = {
        std::pair{originX - halfWidth, originY + height / 2}, std::pair{0, -height}, std::pair{halfWidth, height},
        std::pair{leftWidth, -height}, std::pair{0, height}
    };

    for (const auto& [dx, dy] : moves)
    {
        if (!Move(dx, dy))
            return false;
    }
    return true;
}

bool FerrumController::Wheel(int delta, std::uint32_t timeoutMs)
{
    if (delta < -32768 || delta > 32767)
    {
        std::scoped_lock lock(mutex_);
        SetErrorLocked("Ferrum wheel delta is outside valid range");
        return false;
    }

    std::scoped_lock lock(mutex_);
    return ExecuteAsciiLocked("km.wheel(" + std::to_string(delta) + ")", nullptr, timeoutMs);
}

bool FerrumController::ButtonDown(FerrumMouseButton button, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);
    return SendButtonLocked(button, 1, timeoutMs);
}

bool FerrumController::ButtonUp(FerrumMouseButton button, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);
    return SendButtonLocked(button, 0, timeoutMs);
}

bool FerrumController::ButtonForceRelease(FerrumMouseButton button, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);
    return SendButtonLocked(button, 0, timeoutMs);
}

bool FerrumController::Click(FerrumMouseButton button, std::uint32_t holdMs, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);
    if (!SendButtonLocked(button, 1, timeoutMs))
        return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(std::clamp(holdMs, 1u, 1000u)));
    return SendButtonLocked(button, 0, timeoutMs);
}

bool FerrumController::OpenPortLocked(const char* comPort, std::uint32_t baudRate)
{
    ClosePortLocked();
    const std::string normalizedPort = NormalizeComPort(comPort ? comPort : "");
    if (normalizedPort.empty())
    {
        SetErrorLocked("Invalid COM port name");
        return false;
    }

    const std::string portPath = BuildPortPath(normalizedPort.c_str());
    serialHandle_ = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("Unable to open " + normalizedPort + ": " + GetWin32ErrorMessage(GetLastError()));
        return false;
    }

    if (!SetupComm(serialHandle_, 64 * 1024, 64 * 1024))
    {
        const DWORD error = GetLastError();
        ClosePortLocked();
        SetErrorLocked("SetupComm failed: " + GetWin32ErrorMessage(error));
        return false;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(serialHandle_, &dcb))
    {
        const DWORD error = GetLastError();
        ClosePortLocked();
        SetErrorLocked("GetCommState failed: " + GetWin32ErrorMessage(error));
        return false;
    }

    dcb.BaudRate = baudRate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;

    if (!SetCommState(serialHandle_, &dcb))
    {
        const DWORD error = GetLastError();
        ClosePortLocked();
        SetErrorLocked("SetCommState failed: " + GetWin32ErrorMessage(error));
        return false;
    }

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.WriteTotalTimeoutConstant = 100;
    if (!SetCommTimeouts(serialHandle_, &timeouts))
    {
        const DWORD error = GetLastError();
        ClosePortLocked();
        SetErrorLocked("SetCommTimeouts failed: " + GetWin32ErrorMessage(error));
        return false;
    }

    PurgeComm(serialHandle_, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
    connectedPort_ = normalizedPort;
    connectedBaudRate_ = baudRate;
    return true;
}

void FerrumController::ClosePortLocked()
{
    if (serialHandle_ != INVALID_HANDLE_VALUE)
    {
        PurgeComm(serialHandle_, PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
        CloseHandle(serialHandle_);
        serialHandle_ = INVALID_HANDLE_VALUE;
    }
    connectedPort_.clear();
    connectedBaudRate_ = 0;
}

bool FerrumController::ProbeVersionLocked(std::uint32_t timeoutMs)
{
    std::string response;
    if (!ExecuteAsciiLocked(std::string(kVersionCommand), &response, timeoutMs))
        return false;

    if (!ContainsInsensitive(response, kVersionReply))
    {
        SetErrorLocked("Unexpected Ferrum version response: " + response);
        return false;
    }

    firmwareVersion_ = response;
    lastError_.clear();
    return true;
}

bool FerrumController::ExecuteAsciiLocked(const std::string& command, std::string* reply, std::uint32_t timeoutMs)
{
    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("Ferrum is not connected");
        return false;
    }

    std::string framedCommand = command;
    TrimInPlace(framedCommand);
    if (framedCommand.empty())
    {
        SetErrorLocked("Ferrum command is empty");
        return false;
    }
    if (framedCommand.rfind("km.", 0) != 0)
    {
        SetErrorLocked("Ferrum commands must start with km.: " + framedCommand);
        return false;
    }

    if (framedCommand.size() < 2 || framedCommand.compare(framedCommand.size() - 2, 2, "\r\n") != 0)
        framedCommand += "\r\n";

    if (reply)
        PurgeComm(serialHandle_, PURGE_RXABORT | PURGE_RXCLEAR);

    if (!WriteAllLocked(framedCommand.data(), framedCommand.size()))
        return false;

    if (!reply)
    {
        lastError_.clear();
        return true;
    }

    return ReadLineLocked(*reply, timeoutMs);
}

bool FerrumController::ReadLineLocked(std::string& reply, std::uint32_t timeoutMs)
{
    std::string rawReply;
    rawReply.reserve(128);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline)
    {
        DWORD commErrors = 0;
        COMSTAT commStatus{};
        if (!ClearCommError(serialHandle_, &commErrors, &commStatus))
        {
            const DWORD error = GetLastError();
            ClosePortLocked();
            SetErrorLocked("ClearCommError failed: " + GetWin32ErrorMessage(error));
            return false;
        }
        if (commErrors != 0)
        {
            ClosePortLocked();
            SetErrorLocked("Ferrum serial communication error: " + std::to_string(commErrors));
            return false;
        }
        if (commStatus.cbInQue == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        char buffer[128]{};
        DWORD bytesRead = 0;
        const DWORD bytesToRead = std::min<DWORD>(commStatus.cbInQue, static_cast<DWORD>(sizeof(buffer)));
        if (!ReadFile(serialHandle_, buffer, bytesToRead, &bytesRead, nullptr))
        {
            const DWORD error = GetLastError();
            ClosePortLocked();
            SetErrorLocked("Serial read failed: " + GetWin32ErrorMessage(error));
            return false;
        }

        rawReply.append(buffer, bytesRead);
        if (rawReply.find('\n') == std::string::npos)
            continue;

        TrimInPlace(rawReply);
        reply = rawReply;
        lastReply_ = rawReply;
        lastError_.clear();
        return true;
    }

    TrimInPlace(rawReply);
    lastReply_ = rawReply;
    SetErrorLocked("Timed out waiting for Ferrum response to: " + std::string(kVersionCommand));
    return false;
}

bool FerrumController::WriteAllLocked(const void* data, std::size_t size)
{
    if (!data || size == 0)
    {
        SetErrorLocked("Attempted to write an empty Ferrum command");
        return false;
    }
    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("Ferrum is not connected");
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t totalWritten = 0;
    while (totalWritten < size)
    {
        DWORD bytesWritten = 0;
        if (!WriteFile(serialHandle_, bytes + totalWritten, static_cast<DWORD>(size - totalWritten), &bytesWritten, nullptr))
        {
            const DWORD error = GetLastError();
            ClosePortLocked();
            SetErrorLocked("Serial write failed: " + GetWin32ErrorMessage(error));
            return false;
        }
        if (bytesWritten == 0)
        {
            SetErrorLocked("Serial write returned zero bytes");
            return false;
        }
        totalWritten += bytesWritten;
    }
    return true;
}

bool FerrumController::SendButtonLocked(FerrumMouseButton button, int state, std::uint32_t timeoutMs)
{
    const char* buttonName = GetButtonCommandName(button);
    if (!buttonName)
    {
        SetErrorLocked("Invalid Ferrum mouse button");
        return false;
    }
    if (state != 0 && state != 1)
    {
        SetErrorLocked("Invalid Ferrum button state");
        return false;
    }

    return ExecuteAsciiLocked("km." + std::string(buttonName) + "(" + std::to_string(state) + ")", nullptr, timeoutMs);
}

std::string FerrumController::BuildPortPath(const char* comPort)
{
    return "\\\\.\\" + std::string(comPort ? comPort : "");
}

std::string FerrumController::GetWin32ErrorMessage(DWORD errorCode)
{
    char* messageBuffer = nullptr;
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, errorCode,
                                        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&messageBuffer), 0, nullptr);
    std::string message = length && messageBuffer ? std::string(messageBuffer, length) : "Win32 error " + std::to_string(errorCode);
    if (messageBuffer)
        LocalFree(messageBuffer);
    TrimInPlace(message);
    return message;
}

void FerrumController::TrimInPlace(std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) != 0; });
    if (first == value.end())
    {
        value.clear();
        return;
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) { return std::isspace(character) != 0; }).base();
    value.assign(first, last);
}

void FerrumController::SetErrorLocked(const std::string& error)
{
    lastError_ = error;
}
