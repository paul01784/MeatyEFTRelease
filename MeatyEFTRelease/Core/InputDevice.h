#pragma once

#include <cstdint>
#include <functional>

enum class InputDeviceType : std::uint8_t
{
    None,
    Makcu,
    Ferrum
};

enum class InputMouseButton : std::uint8_t
{
    Left,
    Right,
    Middle,
    Ms1,
    Ms2
};

class InputDeviceManager
{
public:
    [[nodiscard]] InputDeviceType GetConnectedType() const;
    [[nodiscard]] bool IsConnected() const;
    [[nodiscard]] const char* GetConnectedName() const;

    bool ConnectMakcu();
    bool ConnectFerrum();
    void Disconnect();

    bool Move(int dx, int dy, std::uint32_t timeoutMs = 100);
    bool Click(InputMouseButton button, std::uint32_t holdMs = 8, std::uint32_t timeoutMs = 100);

    [[nodiscard]] float GetMouseUnitsPerScreenPixelX() const;
    [[nodiscard]] float GetMouseUnitsPerScreenPixelY() const;
};

extern InputDeviceManager inputDevice;

void RenderInputDeviceWindow(bool* pOpen, float backgroundAlpha = 1.0f, const std::function<void()>& onConfigChanged = {});
void ConnectInputDeviceOnStartup();
