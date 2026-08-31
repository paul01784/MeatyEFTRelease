#pragma once

#include "PlayerRuntimeModel.h"

class PlayerClassifier
{
public:
    [[nodiscard]] static const PlayerRuntimeModel& classify(std::string_view className, bool isLocal) noexcept;
    [[nodiscard]] static const PlayerRuntimeModel& get(PlayerKind kind) noexcept;
    [[nodiscard]] static const PlayerRuntimeModel& get(const Player& player) noexcept;

};
