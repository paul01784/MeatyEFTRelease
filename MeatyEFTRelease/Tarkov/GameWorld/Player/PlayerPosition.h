#pragma once

#include "Player.h"

#include <cmath>

namespace PlayerPosition
{
    inline glm::vec3 getBestBasePosition(const Player& player)
    {
        const auto isUsablePosition = [](const glm::vec3& position)
        {
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z))
            {
                return false;
            }

            constexpr float epsilon = 0.001f;
            return std::fabs(position.x) >= epsilon ||
                std::fabs(position.y) >= epsilon ||
                std::fabs(position.z) >= epsilon;
        };

        const auto getBonePosition = [&player](boneListIndexes bone)
        {
            const int slot = static_cast<int>(bone);
            if (slot < 0 || static_cast<std::size_t>(slot) >= player.bonePositions.size())
                return glm::vec3(0.0f);

            return player.bonePositions[static_cast<std::size_t>(slot)];
        };

        const glm::vec3 base = getBonePosition(boneListIndexes::Base);
        const glm::vec3 leftFoot = getBonePosition(boneListIndexes::LFoot);
        const glm::vec3 rightFoot = getBonePosition(boneListIndexes::RFoot);

        if (isUsablePosition(base))
            return base;

        if (isUsablePosition(leftFoot) && isUsablePosition(rightFoot))
        {
            const float footSeparation = glm::distance(leftFoot, rightFoot);
            if (footSeparation > 0.01f && footSeparation <= 5.5f)
                return (leftFoot + rightFoot) * 0.5f;
        }

        if (isUsablePosition(leftFoot))
            return leftFoot;

        if (isUsablePosition(rightFoot))
            return rightFoot;

        return isUsablePosition(player.location) ? player.location : glm::vec3(0.0f);
    }
}
