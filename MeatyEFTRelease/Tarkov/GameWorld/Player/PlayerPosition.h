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

            constexpr float kMinimumWorldDistanceSquared = 0.01f;
            return glm::dot(position, position) >= kMinimumWorldDistanceSquared;
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
        const bool baseUsable = isUsablePosition(base);
        const bool feetUsable = isUsablePosition(leftFoot) && isUsablePosition(rightFoot);

        if (feetUsable)
        {
            const glm::vec3 feetCenter = (leftFoot + rightFoot) * 0.5f;
            if (!baseUsable || glm::distance(base, feetCenter) > 5.0f)
                return feetCenter;
        }

        if (baseUsable)
            return base;

        return glm::vec3(0.0f);
    }
}
