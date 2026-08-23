#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include "../game/headers/maingame.h"
#include "../game/headers/players.h"

namespace AimLineTargeting
{
    inline constexpr float kMinAngleDegrees = 1.0f;
    inline constexpr float kMaxAngleDegrees = 20.0f;

    inline bool IsValidTarget(const PlayerCache& player)
    {
        return !player.isDead &&
            !player.hasExfiled &&
            !player.isZombie;
    }

    inline bool HasValidLocation(const glm::vec3& location)
    {
        return location.x != 0.0f ||
            location.y != 0.0f ||
            location.z != 0.0f;
    }

    inline float FacingDot(const PlayerCache& source, const glm::vec3& targetLocation)
    {
        const float targetX = targetLocation.x - source.location.x;
        const float targetZ = targetLocation.z - source.location.z;
        const float targetDistanceSquared =
            (targetX * targetX) + (targetZ * targetZ);

        if (targetDistanceSquared < 0.0001f)
            return -1.0f;

        constexpr float radiansPerDegree = 0.01745329251994329577f;
        const float rotationRadians =
            source.rotation.x * radiansPerDegree;

        // The radar's Y axis is inverted from the world Z axis.
        const float facingX = std::cos(rotationRadians);
        const float facingZ = -std::sin(rotationRadians);
        const float inverseTargetDistance =
            1.0f / std::sqrt(targetDistanceSquared);

        return (facingX * targetX + facingZ * targetZ) *
            inverseTargetDistance;
    }

    inline bool IsFacingTarget(const PlayerCache& source, const glm::vec3& targetLocation, float targetAngleDegrees)
    {
        constexpr float radiansPerDegree = 0.01745329251994329577f;
        const float clampedAngle = std::clamp(targetAngleDegrees, kMinAngleDegrees, kMaxAngleDegrees);
        const float minimumDot = std::cos(clampedAngle * radiansPerDegree);

        return FacingDot(source, targetLocation) >= minimumDot;
    }

    inline bool FindLookedAtTarget(const PlayerCache& source, const PlayerCacheCollection& players, const glm::vec3& localLocation, std::string_view localGroupId, float targetAngleDegrees, glm::vec3& targetLocation, bool& targetIsLocal)
    {
        targetIsLocal = false;

        if (source.isLocal ||
            !IsValidTarget(source) ||
            !HasValidLocation(source.location) ||
            !HasValidLocation(localLocation))
            return false;

        constexpr float radiansPerDegree = 0.01745329251994329577f;
        const float clampedAngle = std::clamp(targetAngleDegrees, kMinAngleDegrees, kMaxAngleDegrees);
        const float minimumDot = std::cos(clampedAngle * radiansPerDegree);

        float bestDot = minimumDot;
        bool foundTarget = false;

        const auto considerTarget = [&](const glm::vec3& candidateLocation, bool candidateIsLocal)
            {
                const float facingDot = FacingDot(source, candidateLocation);
                if (facingDot < bestDot)
                    return;

                bestDot = facingDot;
                targetLocation = candidateLocation;
                targetIsLocal = candidateIsLocal;
                foundTarget = true;
            };

        // Local is always eligible, regardless of whether they have a group.
        considerTarget(localLocation, true);

        // An empty group id is not a real group and must never match everyone.
        if (localGroupId.empty())
            return foundTarget;

        for (const PlayerCache& candidate : players)
        {
            if (candidate.isLocal ||
                candidate.instance == source.instance ||
                !IsValidTarget(candidate) ||
                !HasValidLocation(candidate.location) ||
                candidate.groupId != localGroupId)
            {
                continue;
            }

            considerTarget(candidate.location, false);
        }

        return foundTarget;
    }

    inline bool IsLocalBeingLookedAt(
        const PlayerCacheCollection& players,
        bool playerSourcesOnly)
    {
        for (const PlayerCache& player : players)
        {
            if (playerSourcesOnly &&
                (player.isAi ||
                    !(player.isPlayer || player.isPlayerScav)))
            {
                continue;
            }

            if (!player.isLocal &&
                player.aimLineTargetConfirmed &&
                player.aimLineTargetIsLocal)
            {
                return true;
            }
        }

        return false;
    }
}
