#pragma once

#include <glm/glm.hpp>

class Player;
enum class PlayerMarkerType : int;

namespace PlayerAppearance
{
    [[nodiscard]] glm::vec4 resolveColour(const Player& player);
    [[nodiscard]] PlayerMarkerType resolveMarkerType(const Player& player);
    void updateColour(Player& player);
}
