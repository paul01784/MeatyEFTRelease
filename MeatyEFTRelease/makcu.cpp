#include "app/makcu.h"

#include "external/imgui/imgui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

#include <SetupAPI.h>
#include <devguid.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>
#include <string_view>
#include <thread>
#include <utility>
#include "app/render.h"
#include "game/headers/readOnlyAim.h"
#include "game/headers/fireport.h"

#pragma comment(lib, "setupapi.lib")

namespace
{
    constexpr std::uint32_t kBootstrapBaudRate = 115200;
    constexpr std::uint32_t kNativeBaudRate = 4000000;

    constexpr std::array<std::uint8_t, 9> kNativeBaudSwitchFrame = {
        0xDE,
        0xAD,
        0x05,
        0x00,
        0xA5,
        0x00,
        0x09,
        0x3D,
        0x00
    };

    constexpr std::string_view kVersionCommand = "km.version()";
    constexpr std::string_view kVersionReplyPrefix = "km.MAKCU";

    bool StartsWith(const std::string& value, std::string_view prefix)
    {
        return value.size() >= prefix.size() &&
            value.compare(0, prefix.size(), prefix) == 0;
    }

    void TrimAscii(std::string& value)
    {
        const auto first = std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(character) != 0;
            }
        );

        if (first == value.end())
        {
            value.clear();
            return;
        }

        const auto last = std::find_if_not(
            value.rbegin(),
            value.rend(),
            [](unsigned char character)
            {
                return std::isspace(character) != 0;
            }
        ).base();

        value.assign(first, last);
    }

    std::string ToUpperAscii(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::toupper(character));
            }
        );

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

        return std::all_of(
            upper.begin() + 3,
            upper.end(),
            [](unsigned char character)
            {
                return std::isdigit(character) != 0;
            }
        );
    }

    std::string NormalizeComPort(std::string value)
    {
        TrimAscii(value);
        value = ToUpperAscii(value);

        if (!IsComPortName(value))
            return {};

        return value;
    }

    int GetComPortNumber(const std::string& portName)
    {
        if (!IsComPortName(portName))
            return std::numeric_limits<int>::max();

        int result = 0;

        for (std::size_t index = 3; index < portName.size(); ++index)
        {
            result = (result * 10) + (portName[index] - '0');
        }

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

        SetupDiGetDeviceRegistryPropertyA(
            deviceInfoSet,
            &deviceInfoData,
            property,
            &propertyType,
            nullptr,
            0,
            &requiredSize
        );

        if (requiredSize == 0)
            return {};

        std::vector<char> buffer(requiredSize + 1, '\0');

        if (!SetupDiGetDeviceRegistryPropertyA(deviceInfoSet, &deviceInfoData, property, &propertyType, reinterpret_cast<PBYTE>(buffer.data()), static_cast<DWORD>(buffer.size()), &requiredSize))
        {
            return {};
        }

        return std::string(buffer.data());
    }

    std::string ReadRegistryPortName(HDEVINFO deviceInfoSet, SP_DEVINFO_DATA& deviceInfoData)
    {
        HKEY deviceKey = SetupDiOpenDevRegKey(
            deviceInfoSet,
            &deviceInfoData,
            DICS_FLAG_GLOBAL,
            0,
            DIREG_DEV,
            KEY_READ
        );

        if (deviceKey == INVALID_HANDLE_VALUE)
            return {};

        char portBuffer[64]{};
        DWORD valueType = 0;
        DWORD valueSize = sizeof(portBuffer);

        const LONG result = RegQueryValueExA(
            deviceKey,
            "PortName",
            nullptr,
            &valueType,
            reinterpret_cast<LPBYTE>(portBuffer),
            &valueSize
        );

        RegCloseKey(deviceKey);

        if (result != ERROR_SUCCESS || valueType != REG_SZ)
            return {};

        return NormalizeComPort(portBuffer);
    }

    const char* GetButtonCommandName(MakcuMouseButton button)
    {
        switch (button)
        {
        case MakcuMouseButton::Left:
            return "left";

        case MakcuMouseButton::Right:
            return "right";

        case MakcuMouseButton::Middle:
            return "middle";

        case MakcuMouseButton::Ms1:
            return "ms1";

        case MakcuMouseButton::Ms2:
            return "ms2";

        default:
            return nullptr;
        }
    }

    void CopyStringToFixedBuffer(char* destination, std::size_t destinationSize, const std::string& source)
    {
        if (!destination || destinationSize == 0)
            return;

        const std::size_t copyCount = std::min(
            destinationSize - 1,
            source.size()
        );

        std::memcpy(destination, source.data(), copyCount);
        destination[copyCount] = '\0';
    }

    std::string BuildPortLabel(const MakcuSerialPort& port)
    {
        std::string label = port.portName;

        if (!port.friendlyName.empty())
        {
            label += "  -  ";
            label += port.friendlyName;
        }

        if (port.isMakcuCandidate)
            label += "  [MAKCU candidate]";

        return label;
    }

    std::string GetSelectedPortPreview(const std::vector<MakcuSerialPort>& ports, const char* configuredPort)
    {
        const std::string normalizedPort = NormalizeComPort(configuredPort ? configuredPort : "");

        for (const MakcuSerialPort& port : ports)
        {
            if (port.portName == normalizedPort)
                return BuildPortLabel(port);
        }

        if (!normalizedPort.empty())
            return normalizedPort + "  -  saved selection";

        return "Select a COM port";
    }
}

MakcuController makcu{};
MakcuConfig makcuConfig{};

MakcuController::~MakcuController()
{
    Disconnect();
}

bool MakcuController::Connect(const MakcuConfig& config)
{
    return Connect(config.comPort);
}

bool MakcuController::Connect(const char* comPort)
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

    // First try the normal native speed
    if (OpenPortLocked(normalizedPort.c_str(), kNativeBaudRate))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        PurgeComm(
            serialHandle_,
            PURGE_RXABORT | PURGE_RXCLEAR
        );

        if (ProbeVersionLocked(500))
            return true;

        ClosePortLocked();
    }

    // Device may still be running at bootstrap baud
    if (!OpenPortLocked(normalizedPort.c_str(), kBootstrapBaudRate))
        return false;

    if (!SwitchToNativeBaudLocked())
    {
        ClosePortLocked();
        return false;
    }

    if (!ProbeVersionLocked(500))
    {
        ClosePortLocked();
        return false;
    }

    return true;
}

void MakcuController::Disconnect()
{
    std::scoped_lock lock(mutex_);
    ClosePortLocked();
}

bool MakcuController::IsConnected() const
{
    std::scoped_lock lock(mutex_);

    return serialHandle_ != INVALID_HANDLE_VALUE &&
        !connectedPort_.empty() &&
        connectedBaudRate_ == kNativeBaudRate;
}

MakcuDiagnostics MakcuController::GetDiagnostics() const
{
    std::scoped_lock lock(mutex_);

    MakcuDiagnostics diagnostics{};

    diagnostics.connected =
        serialHandle_ != INVALID_HANDLE_VALUE &&
        !connectedPort_.empty() &&
        connectedBaudRate_ == kNativeBaudRate;

    diagnostics.connectedPort = connectedPort_;
    diagnostics.connectedBaudRate = connectedBaudRate_;
    diagnostics.firmwareVersion = firmwareVersion_;
    diagnostics.lastReply = lastReply_;
    diagnostics.lastError = lastError_;

    return diagnostics;
}

std::vector<MakcuSerialPort> MakcuController::EnumerateSerialPorts()
{
    std::vector<MakcuSerialPort> ports;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsA(
        &GUID_DEVCLASS_PORTS,
        nullptr,
        nullptr,
        DIGCF_PRESENT
    );

    if (deviceInfoSet != INVALID_HANDLE_VALUE)
    {
        for (DWORD deviceIndex = 0;; ++deviceIndex)
        {
            SP_DEVINFO_DATA deviceInfoData{};
            deviceInfoData.cbSize = sizeof(deviceInfoData);

            if (!SetupDiEnumDeviceInfo(
                deviceInfoSet,
                deviceIndex,
                &deviceInfoData
            ))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;

                continue;
            }

            std::string friendlyName = ReadSetupApiProperty(deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME);

            if (friendlyName.empty())
            {
                friendlyName = ReadSetupApiProperty(deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC);
            }

            const std::string hardwareId = ReadSetupApiProperty(deviceInfoSet, deviceInfoData, SPDRP_HARDWAREID);

            std::string portName = ExtractComPort(friendlyName);

            if (portName.empty())
            {
                portName = ReadRegistryPortName(deviceInfoSet, deviceInfoData);
            }

            if (portName.empty())
                continue;

            MakcuSerialPort port{};
            port.portName = std::move(portName);
            port.friendlyName = std::move(friendlyName);
            port.hardwareId = hardwareId;
            port.isMakcuCandidate = ContainsInsensitive(hardwareId, "VID_1A86&PID_55D3");

            ports.push_back(std::move(port));
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }

    std::sort(
        ports.begin(),
        ports.end(),
        [](const MakcuSerialPort& left, const MakcuSerialPort& right)
        {
            const int leftNumber = GetComPortNumber(left.portName);
            const int rightNumber = GetComPortNumber(right.portName);

            if (leftNumber != rightNumber)
                return leftNumber < rightNumber;

            return left.portName < right.portName;
        }
    );

    ports.erase(
        std::unique(
            ports.begin(),
            ports.end(),
            [](const MakcuSerialPort& left, const MakcuSerialPort& right)
            {
                return left.portName == right.portName;
            }
        ),
        ports.end()
    );

    return ports;
}

bool MakcuController::RefreshDiagnostics()
{
    std::scoped_lock lock(mutex_);

    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("MAKCU is not connected");
        return false;
    }

    return ProbeVersionLocked(500);
}

bool MakcuController::SendCommand(const std::string& command, std::string* reply, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);

    return ExecuteAsciiLocked(command, reply, timeoutMs);
}

bool MakcuController::Move(int dx, int dy, std::uint32_t timeoutMs)
{
    if (dx < -32768 || dx > 32767 ||
        dy < -32768 || dy > 32767)
    {
        std::scoped_lock lock(mutex_);
        SetErrorLocked("MAKCU move coordinates are outside int16 range");
        return false;
    }

    std::scoped_lock lock(mutex_);

    const std::string command =
        "km.move(" + std::to_string(dx) + "," + std::to_string(dy) + ")";

    return ExecuteAsciiLocked(command, nullptr, timeoutMs);
}

bool MakcuController::Wheel(int delta, std::uint32_t timeoutMs)
{
    if (delta < -32768 || delta > 32767)
    {
        std::scoped_lock lock(mutex_);
        SetErrorLocked("MAKCU wheel delta is outside valid range");
        return false;
    }

    std::scoped_lock lock(mutex_);

    const std::string command = "km.wheel(" + std::to_string(delta) + ")";

    return ExecuteAsciiLocked(command, nullptr, timeoutMs);
}

bool MakcuController::ButtonDown(MakcuMouseButton button, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);

    return SendButtonLocked(button, 1, timeoutMs);
}

bool MakcuController::ButtonUp(MakcuMouseButton button, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);

    return SendButtonLocked(button, 0, timeoutMs);
}

bool MakcuController::ButtonForceRelease(MakcuMouseButton button, std::uint32_t timeoutMs)
{
    std::scoped_lock lock(mutex_);

    return SendButtonLocked(button, 2, timeoutMs);
}

bool MakcuController::OpenPortLocked(const char* comPort, std::uint32_t baudRate)
{
    ClosePortLocked();

    const std::string normalizedPort = NormalizeComPort(comPort ? comPort : "");

    if (normalizedPort.empty())
    {
        SetErrorLocked("Invalid COM port name");
        return false;
    }

    const std::string portPath = BuildPortPath(normalizedPort.c_str());

    serialHandle_ = CreateFileA(
        portPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();

        SetErrorLocked("Unable to open " + normalizedPort + ": " + GetWin32ErrorMessage(error));

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

    dcb.fErrorChar = FALSE;
    dcb.fNull = FALSE;

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
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;

    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;

    if (!SetCommTimeouts(serialHandle_, &timeouts))
    {
        const DWORD error = GetLastError();

        ClosePortLocked();

        SetErrorLocked("SetCommTimeouts failed: " + GetWin32ErrorMessage(error));

        return false;
    }

    PurgeComm(
        serialHandle_,
        PURGE_RXABORT |
        PURGE_RXCLEAR |
        PURGE_TXABORT |
        PURGE_TXCLEAR
    );

    connectedPort_ = normalizedPort;
    connectedBaudRate_ = baudRate;

    return true;
}

void MakcuController::ClosePortLocked()
{
    if (serialHandle_ != INVALID_HANDLE_VALUE)
    {
        PurgeComm(
            serialHandle_,
            PURGE_RXABORT |
            PURGE_RXCLEAR |
            PURGE_TXABORT |
            PURGE_TXCLEAR
        );

        CloseHandle(serialHandle_);
        serialHandle_ = INVALID_HANDLE_VALUE;
    }

    connectedPort_.clear();
    connectedBaudRate_ = 0;
}

bool MakcuController::SwitchToNativeBaudLocked()
{
    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("Cannot switch MAKCU baud rate because the port is closed");
        return false;
    }

    const std::string portName = connectedPort_;

    if (!WriteAllLocked(kNativeBaudSwitchFrame.data(), kNativeBaudSwitchFrame.size(), true))
    {
        return false;
    }

    // doc specifies 100ms before reopening
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ClosePortLocked();

    if (!OpenPortLocked(portName.c_str(), kNativeBaudRate))
    {
        return false;
    }

    // doc specifies a further 50ms settle period
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    PurgeComm(serialHandle_, PURGE_RXABORT | PURGE_RXCLEAR);

    return true;
}

bool MakcuController::ProbeVersionLocked(std::uint32_t timeoutMs)
{
    std::string response;

    if (!ExecuteAsciiLocked(std::string(kVersionCommand), &response, timeoutMs))
    {
        return false;
    }

    const std::size_t marker = response.find(kVersionReplyPrefix);

    if (marker == std::string::npos)
    {
        SetErrorLocked("Unexpected MAKCU version response: " + response);

        return false;
    }

    std::size_t lineEnd = response.find_first_of("\r\n", marker);

    if (lineEnd == std::string::npos)
        lineEnd = response.size();

    firmwareVersion_ = response.substr(
        marker,
        lineEnd - marker
    );

    lastError_.clear();

    return true;
}

bool MakcuController::ExecuteAsciiLocked(const std::string& command, std::string* reply, std::uint32_t timeoutMs)
{
    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("MAKCU is not connected");
        return false;
    }

    std::string framedCommand = command;
    TrimInPlace(framedCommand);

    if (framedCommand.empty())
    {
        SetErrorLocked("MAKCU command is empty");
        return false;
    }

    if (!StartsWith(framedCommand, "km."))
    {
        SetErrorLocked("Native MAKCU commands must start with km.: " + framedCommand);

        return false;
    }

    if (framedCommand.size() < 2 || framedCommand.compare(framedCommand.size() - 2, 2, "\r\n") != 0)
    {
        framedCommand += "\r\n";
    }

    if (!WriteAllLocked(
        framedCommand.data(),
        framedCommand.size(),
        false
    ))
    {
        return false;
    }

    std::string rawReply;
    rawReply.reserve(256);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (std::chrono::steady_clock::now() < deadline)
    {
        DWORD commErrors = 0;
        COMSTAT commStatus{};

        if (!ClearCommError(
            serialHandle_,
            &commErrors,
            &commStatus
        ))
        {
            const DWORD error = GetLastError();

            ClosePortLocked();

            SetErrorLocked("ClearCommError failed: " + GetWin32ErrorMessage(error));

            return false;
        }

        if (commErrors != 0)
        {
            ClosePortLocked();

            SetErrorLocked("MAKCU serial communication error: " + std::to_string(commErrors));

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

        if (!ReadFile(
            serialHandle_,
            buffer,
            bytesToRead,
            &bytesRead,
            nullptr
        ))
        {
            const DWORD error = GetLastError();

            ClosePortLocked();

            SetErrorLocked("Serial read failed: " + GetWin32ErrorMessage(error));

            return false;
        }

        if (bytesRead == 0)
            continue;

        rawReply.append(buffer, bytesRead);

        // responses complete at the >>> prompt
        if (rawReply.find(">>>") == std::string::npos)
            continue;

        const std::string cleanedReply = CleanReply(rawReply);

        lastReply_ = cleanedReply;
        lastError_.clear();

        if (reply)
            *reply = cleanedReply;

        return true;
    }

    lastReply_ = CleanReply(rawReply);

    ClosePortLocked();

    SetErrorLocked("Timed out waiting for MAKCU response to: " + command);

    return false;
}

bool MakcuController::WriteAllLocked(const void* data, std::size_t size, bool flushAfterWrite)
{
    if (!data || size == 0)
    {
        SetErrorLocked("Attempted to write an empty MAKCU command");
        return false;
    }

    if (serialHandle_ == INVALID_HANDLE_VALUE)
    {
        SetErrorLocked("MAKCU is not connected");
        return false;
    }

    const auto* bytes = static_cast<const char*>(data);

    std::size_t totalWritten = 0;

    while (totalWritten < size)
    {
        DWORD bytesWritten = 0;

        const DWORD bytesToWrite = static_cast<DWORD>(
            size - totalWritten
            );

        if (!WriteFile(
            serialHandle_,
            bytes + totalWritten,
            bytesToWrite,
            &bytesWritten,
            nullptr
        ))
        {
            const DWORD error = GetLastError();

            ClosePortLocked();

            SetErrorLocked("Serial write failed: " + GetWin32ErrorMessage(error));

            return false;
        }

        if (bytesWritten == 0)
        {
            ClosePortLocked();

            SetErrorLocked("Serial write returned zero bytes");
            return false;
        }

        totalWritten += bytesWritten;
    }

    if (flushAfterWrite && !FlushFileBuffers(serialHandle_))
    {
        const DWORD error = GetLastError();

        ClosePortLocked();

        SetErrorLocked("Serial flush failed: " + GetWin32ErrorMessage(error));

        return false;
    }

    return true;
}

bool MakcuController::SendButtonLocked(MakcuMouseButton button, int state, std::uint32_t timeoutMs)
{
    const char* buttonName = GetButtonCommandName(button);

    if (!buttonName)
    {
        SetErrorLocked("Invalid MAKCU mouse button");
        return false;
    }

    if (state < 0 || state > 2)
    {
        SetErrorLocked("Invalid MAKCU button state");
        return false;
    }

    const std::string command =
        "km." +
        std::string(buttonName) +
        "(" +
        std::to_string(state) +
        ")";

    return ExecuteAsciiLocked(command, nullptr, timeoutMs);
}

std::string MakcuController::BuildPortPath(const char* comPort)
{
    std::string port = comPort ? comPort : "";
    TrimInPlace(port);

    if (port.rfind("\\\\.\\", 0) == 0)
        return port;

    return "\\\\.\\" + port;
}

std::string MakcuController::GetWin32ErrorMessage(DWORD errorCode)
{
    LPSTR messageBuffer = nullptr;

    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr
    );

    if (length == 0 || !messageBuffer)
        return "Win32 error " + std::to_string(errorCode);

    std::string message(
        messageBuffer,
        length
    );

    LocalFree(messageBuffer);

    TrimInPlace(message);

    return message;
}

std::string MakcuController::CleanReply(std::string reply)
{
    const std::size_t prompt = reply.find(">>>");

    if (prompt != std::string::npos)
        reply.erase(prompt);

    TrimInPlace(reply);

    return reply;
}

void MakcuController::TrimInPlace(std::string& value)
{
    TrimAscii(value);
}

void MakcuController::SetErrorLocked(const std::string& error)
{
    lastError_ = error;
}

namespace
{
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

    bool DrawBoneIndexInput(const char* label, boneListIndexes& bone)
    {
        using BoneUnderlying =
            std::underlying_type_t<boneListIndexes>;

        int value = static_cast<int>(
            static_cast<BoneUnderlying>(bone)
            );

        if (!ImGui::InputInt(label, &value))
            return false;

        // Prevent invalid negative indexes.
        value = std::max(value, 0);

        bone = static_cast<boneListIndexes>(value);
        return true;
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

    

    std::vector<boneListIndexes> getAllBones() {
        return {
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
    }

    bool ShowBoneSelectionBox(boneListIndexes& bone, const char* label)
    {
        static const std::vector<boneListIndexes> bones = getAllBones();

        auto currentIt = std::find(
            bones.begin(),
            bones.end(),
            bone
        );

        int currentIndex = 0;

        if (currentIt != bones.end())
        {
            currentIndex = static_cast<int>(std::distance(bones.begin(), currentIt));
        }

        const char* preview = BoneToString(bones[currentIndex]);

        bool changed = false;

        if (ImGui::BeginCombo(label, preview))
        {
            for (int i = 0; i < static_cast<int>(bones.size()); ++i)
            {
                const bool selected = (i == currentIndex);
                const char* boneName = BoneToString(bones[i]);

                if (ImGui::Selectable(boneName, selected))
                {
                    bone = bones[i];
                    changed = true;
                }

                if (selected)
                    ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
        }

        return changed;
    }
}

void RenderMakcuWindow(bool* pOpen, float backgroundAlpha, const std::function<void()>& onConfigChanged)
{
    if (!pOpen || !*pOpen)
        return;

    static constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize;

    static bool portsScanned = false;
    static std::vector<MakcuSerialPort> serialPorts;

    if (!portsScanned)
    {
        serialPorts = MakcuController::EnumerateSerialPorts();
        portsScanned = true;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    constexpr float windowWidth = 420.0f;
    const float maxWindowHeight = viewport->Size.y - 20.0f;

    ImGui::SetNextWindowPos(
        ImVec2(
            (viewport->Pos.x + viewport->Size.x) - 479.0f,
            viewport->Pos.y + 10.0f
        )
    );

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(windowWidth, 0.0f),
        ImVec2(windowWidth, maxWindowHeight)
    );

    ImGui::SetNextWindowBgAlpha(backgroundAlpha);

    if (!ImGui::Begin("MAKCU", pOpen, windowFlags))
    {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar(
        "##makcuTabs",
        ImGuiTabBarFlags_FittingPolicyResizeDown
    ))
    {
        // Connection
        if (ImGui::BeginTabItem("Connection"))
        {
            bool configChanged = false;

            ImGui::SeparatorText("Serial Port");

            const std::string selectedPreview = GetSelectedPortPreview(
                serialPorts,
                makcuConfig.comPort
            );

            ImGui::SetNextItemWidth(310.0f);

            if (ImGui::BeginCombo(
                "COM Port",
                selectedPreview.c_str()
            ))
            {
                for (const MakcuSerialPort& port : serialPorts)
                {
                    const bool selected =
                        port.portName ==
                        NormalizeComPort(makcuConfig.comPort);

                    const std::string label = BuildPortLabel(port);

                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        CopyStringToFixedBuffer(
                            makcuConfig.comPort,
                            IM_ARRAYSIZE(makcuConfig.comPort),
                            port.portName
                        );

                        if (onConfigChanged)
                            onConfigChanged();
                    }

                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();

                ImGui::Spacing();
                ImGui::SeparatorText("Connection Preferences");

                configChanged |= ImGui::Checkbox(
                    "Connect on startup",
                    &makcuConfig.connectOnStartup
                );

                ImGui::TextDisabled(
                    "Attempts to connect to the selected COM port on startup."
                );

                if (configChanged && onConfigChanged)
                    onConfigChanged();


            }

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
                if (ImGui::Button(
                    "Connect",
                    ImVec2(150.0f, 30.0f)
                ))
                {
                    makcu.Connect(makcuConfig);
                }
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
            else
            {
                ImGui::TextColored(
                    ImVec4(1.00f, 0.35f, 0.35f, 1.00f),
                    "Disconnected"
                );
            }

            ImGui::EndTabItem();
        }

        // Settings
        if (ImGui::BeginTabItem("Settings"))
        {
            bool configChanged = false;

            ImGui::SeparatorText("Aim Settings");

            configChanged |= ImGui::Checkbox(
                "Enable aim",
                &aimGlobals::aimEnabled
            );

            ImGui::BeginDisabled(!aimGlobals::aimEnabled);

            configChanged |= ImGui::DragFloat(
                "Aim FOV",
                &aimGlobals::aimFOV,
                1.0f,
                1.0f,
                200.0f,
                "%.0f px"
            );

            static constexpr const char* aimReferenceOptions[] = {
                "Crosshair (screen centre)",
                "Fireport (barrel ray end)",
            };
            int aimReferenceIndex = static_cast<int>(aimGlobals::aimReference);
            aimReferenceIndex = std::clamp(aimReferenceIndex, 0, IM_ARRAYSIZE(aimReferenceOptions) - 1);
            if (ImGui::Combo("Aim reference", &aimReferenceIndex, aimReferenceOptions, IM_ARRAYSIZE(aimReferenceOptions))) {
                aimGlobals::aimReference = static_cast<AimReference>(aimReferenceIndex);
                configChanged = true;
            }

            configChanged |= ImGui::Checkbox("Show aim FOV ring", &aimGlobals::showAimFovRing);

            configChanged |= ImGui::DragFloat(
                "Fireport line length (m)",
                &aimGlobals::fireportLineLengthM,
                1.0f,
                25.0f,
                300.0f,
                "%.0f m"
            );

            configChanged |= ImGui::Checkbox("Draw fireport line (ESP)", &espGlobals::drawFireportLine);

            if (aimGlobals::aimReference == AimReference::Fireport) {
                ImGui::TextDisabled("FOV + aim move from barrel ray endpoint.");
                const FireportPose pose = g_fireport.snapshot();
                if (pose.valid && pose.pathUsed)
                    ImGui::TextDisabled("Fireport path: %s", pose.pathUsed);
                else
                    ImGui::TextColored(ImVec4(1.f, 0.55f, 0.31f, 1.f), "Fireport not resolved");
            }

            configChanged |= ImGui::DragInt(
                "Aim distance",
                &aimGlobals::aimDistance,
                1.0f,
                1,
                2000,
                "%d m"
            );

            configChanged |= ImGui::DragFloat(
                "Aim smoothing",
                &aimGlobals::aimSmooth,
                0.1f,
                1.0f,
                100.0f,
                "%.1f"
            );

            ImGui::TextDisabled(
                "FOV is the pixel radius from screen centre."
            );

            ImGui::Spacing();
            ImGui::SeparatorText("Mouse Calibration");

            configChanged |= ImGui::DragFloat(
                "X units per screen pixel",
                &makcu.mouseUnitsPerScreenPixelX,
                0.001f,
                0.001f,
                5.0f,
                "%.4f"
            );

            configChanged |= ImGui::DragFloat(
                "Y units per screen pixel",
                &makcu.mouseUnitsPerScreenPixelY,
                0.001f,
                0.001f,
                5.0f,
                "%.4f"
            );

            if (ImGui::Button("Reset mouse calibration"))
            {
                makcu.mouseUnitsPerScreenPixelX = 1.0f;
                makcu.mouseUnitsPerScreenPixelY = 1.0f;
                configChanged = true;
            }

            ImGui::TextDisabled(
                "Start at 1.0000. Raise a value if movement is too slow."
            );

            ImGui::TextDisabled(
                "X and Y may need slightly different values."
            );

            ImGui::Spacing();
            ImGui::SeparatorText("Target Selection");

            static constexpr const char* targetModeOptions[] =
            {
                "FOV - closest to screen centre",
                "CQB - closest to local player"
            };

            int targetModeIndex = static_cast<int>(
                aimGlobals::targetMode
                );

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
                    static_cast<TargetMode>(targetModeIndex);

                configChanged = true;
            }

            configChanged |= ImGui::Checkbox(
                "Lock target while key is held",
                &aimGlobals::targetLock
            );

            ImGui::TextDisabled(
                "When enabled, the first valid target remains selected\n"
                "until the key is released or it becomes invalid."
            );

            ImGui::Spacing();
            ImGui::SeparatorText("Aim Bones");

            configChanged |= ShowBoneSelectionBox(
                aimGlobals::aiBone,
                "AI target bone"
            );

            configChanged |= ShowBoneSelectionBox(
                aimGlobals::pmcBone,
                "PMC target bone"
            );

            ImGui::TextDisabled(
                "AI and PMC targets can use different bones."
            );

            ImGui::EndDisabled();

            // Your original Settings tab did not call this at the end.
            if (configChanged && onConfigChanged)
                onConfigChanged();

            ImGui::EndTabItem();
        }

        // Debug
        if (ImGui::BeginTabItem("Debug"))
        {
            const bool connected = makcu.IsConnected();

            const MakcuDiagnostics diagnostics =
                makcu.GetDiagnostics();

            const std::optional<TargetResult> bestTarget =
                readOnlyAim.GetLiveTarget();

            const std::optional<TargetResult> activeTarget =
                readOnlyAim.GetActiveTarget();

            ImGui::SeparatorText("Aim State");

            ImGui::Text(
                "Aim enabled: %s",
                aimGlobals::aimEnabled ? "true" : "false"
            );

            ImGui::Text(
                "Target lock: %s",
                aimGlobals::targetLock ? "true" : "false"
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

            ImGui::Text(
                "Aim smoothing: %.2f",
                aimGlobals::aimSmooth
            );

            ImGui::Text(
                "Mouse calibration: X %.4f | Y %.4f",
                makcu.mouseUnitsPerScreenPixelX,
                makcu.mouseUnitsPerScreenPixelY
            );

            ImGui::Text(
                "AI bone: %s (%d)",
                BoneToString(aimGlobals::aiBone),
                static_cast<int>(aimGlobals::aiBone)
            );

            ImGui::Text(
                "PMC bone: %s (%d)",
                BoneToString(aimGlobals::pmcBone),
                static_cast<int>(aimGlobals::pmcBone)
            );

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
            ImGui::SeparatorText("MAKCU Runtime");

            ImGui::Text(
                "Connection state: %s",
                connected ? "Connected" : "Disconnected"
            );

            if (diagnostics.connected)
            {
                ImGui::TextColored(
                    ImVec4(0.20f, 1.00f, 0.20f, 1.00f),
                    "Connected: %s @ %u baud",
                    diagnostics.connectedPort.c_str(),
                    diagnostics.connectedBaudRate
                );
            }
            else
            {
                ImGui::TextColored(
                    ImVec4(1.00f, 0.35f, 0.35f, 1.00f),
                    "No active MAKCU connection"
                );
            }

            ImGui::Text(
                "Firmware: %s",
                diagnostics.firmwareVersion.empty()
                ? "Not queried"
                : diagnostics.firmwareVersion.c_str()
            );

            if (ImGui::Button(
                "Refresh Diagnostics",
                ImVec2(170.0f, 28.0f)
            ))
            {
                if (connected)
                    makcu.RefreshDiagnostics();
            }

            if (!diagnostics.lastError.empty())
            {
                ImGui::Spacing();
                ImGui::SeparatorText("Last Error");

                ImGui::TextWrapped(
                    "%s",
                    diagnostics.lastError.c_str()
                );
            }

            if (ImGui::CollapsingHeader(
                "Last Device Reply",
                ImGuiTreeNodeFlags_DefaultOpen
            ))
            {
                ImGui::TextWrapped(
                    "%s",
                    diagnostics.lastReply.empty()
                    ? "No device reply yet"
                    : diagnostics.lastReply.c_str()
                );
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}