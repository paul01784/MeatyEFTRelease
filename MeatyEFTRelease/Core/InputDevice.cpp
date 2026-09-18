#include "InputDevice.h"

#include "Makcu/Makcu.h"
#include "Ferrum/Ferrum.h"

#include <cctype>
#include <string>

namespace
{
    bool HasValidComPort(const char* configuredPort)
    {
        const std::string port = configuredPort ? configuredPort : "";
        if (port.size() < 4 || std::toupper(static_cast<unsigned char>(port[0])) != 'C' || std::toupper(static_cast<unsigned char>(port[1])) != 'O' ||
            std::toupper(static_cast<unsigned char>(port[2])) != 'M')
        {
            return false;
        }

        for (std::size_t index = 3; index < port.size(); ++index)
        {
            if (std::isdigit(static_cast<unsigned char>(port[index])) == 0)
                return false;
        }
        return true;
    }
}

InputDeviceManager inputDevice{};

InputDeviceType InputDeviceManager::GetConnectedType() const
{
    if (makcu.IsConnected())
        return InputDeviceType::Makcu;
    if (ferrum.IsConnected())
        return InputDeviceType::Ferrum;
    return InputDeviceType::None;
}

bool InputDeviceManager::IsConnected() const
{
    return GetConnectedType() != InputDeviceType::None;
}

const char* InputDeviceManager::GetConnectedName() const
{
    switch (GetConnectedType())
    {
    case InputDeviceType::Makcu:
        return "MAKCU";
    case InputDeviceType::Ferrum:
        return "Ferrum";
    default:
        return "None";
    }
}

bool InputDeviceManager::ConnectMakcu()
{
    return !ferrum.IsConnected() && makcu.Connect(makcuConfig);
}

bool InputDeviceManager::ConnectFerrum()
{
    return !makcu.IsConnected() && ferrum.Connect(ferrumConfig);
}

void InputDeviceManager::Disconnect()
{
    if (makcu.IsConnected())
        makcu.Disconnect();
    if (ferrum.IsConnected())
        ferrum.Disconnect();
}

bool InputDeviceManager::Move(int dx, int dy, std::uint32_t timeoutMs)
{
    switch (GetConnectedType())
    {
    case InputDeviceType::Makcu:
        return makcu.Move(dx, dy, timeoutMs);
    case InputDeviceType::Ferrum:
        return ferrum.Move(dx, dy, timeoutMs);
    default:
        return false;
    }
}

bool InputDeviceManager::Click(InputMouseButton button, std::uint32_t holdMs, std::uint32_t timeoutMs)
{
    switch (GetConnectedType())
    {
    case InputDeviceType::Makcu:
        return makcu.Click(static_cast<MakcuMouseButton>(button), holdMs, timeoutMs);
    case InputDeviceType::Ferrum:
        return ferrum.Click(static_cast<FerrumMouseButton>(button), holdMs, timeoutMs);
    default:
        return false;
    }
}

float InputDeviceManager::GetMouseUnitsPerScreenPixelX() const
{
    return GetConnectedType() == InputDeviceType::Ferrum ? ferrum.mouseUnitsPerScreenPixelX : makcu.mouseUnitsPerScreenPixelX;
}

float InputDeviceManager::GetMouseUnitsPerScreenPixelY() const
{
    return GetConnectedType() == InputDeviceType::Ferrum ? ferrum.mouseUnitsPerScreenPixelY : makcu.mouseUnitsPerScreenPixelY;
}

void ConnectInputDeviceOnStartup()
{
    static bool startupConnectionAttempted = false;
    if (startupConnectionAttempted)
        return;

    if (inputDevice.IsConnected())
    {
        startupConnectionAttempted = true;
        return;
    }

    if (makcuConfig.connectOnStartup && ferrumConfig.connectOnStartup)
        ferrumConfig.connectOnStartup = false;

    if (makcuConfig.connectOnStartup && HasValidComPort(makcuConfig.comPort))
    {
        startupConnectionAttempted = true;
        inputDevice.ConnectMakcu();
        return;
    }

    if (ferrumConfig.connectOnStartup && HasValidComPort(ferrumConfig.comPort))
    {
        startupConnectionAttempted = true;
        inputDevice.ConnectFerrum();
    }
}
