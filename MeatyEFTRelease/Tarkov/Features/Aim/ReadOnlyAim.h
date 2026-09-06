#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <optional>
#include <chrono>
#include <glm/glm.hpp>
#include "Ballistics.h"
#include "../../GameWorld/RegisteredPlayers.h"
#include "../../../UI/render.h"

struct TargetResult
{
    Player player{};

    boneListIndexes selectedBone = boneListIndexes::Head;

    glm::vec3 boneWorldPos{};
    glm::vec2 screenPos{};

    float worldDistanceSq = FLT_MAX;
    float screenDistanceSq = FLT_MAX;
};

struct AimReferencePoint
{
    glm::vec2 pos{};
    bool valid = false;
};

struct AimPredictionContext
{
    bool enabled = false;
    glm::vec3 sourcePosition{};
    BallisticsInfo ballistics{};
};

class ReadOnlyAim
{
public:
    void aimTask();

    bool moveToTargetBone(const TargetResult& target, const glm::vec2& aimRef);

    std::optional<TargetResult> getLiveTarget() const;
    std::optional<TargetResult> getActiveTarget() const;

    AimReferencePoint resolveAimReference() const;

private:
    std::optional<TargetResult> buildTargetResult(const Player& entity, float maxDistance, float fovRadiusPx,
                                                  const glm::vec2& aimRef,
                                                  const AimPredictionContext& prediction,
                                                  bool useClosestBoneToFireport) const;

    bool getSelectedBonePosition(const Player& entity, boneListIndexes selectedBone, glm::vec3& outPosition) const;

    bool getClosestBoneToAimReference(const Player& entity, const glm::vec2& aimRef,
                                      boneListIndexes& outBone, glm::vec3& outPosition) const;

    std::optional<TargetResult> findBestTarget(const std::vector<Player>& snapshot, TargetMode mode,
                                               float maxDistance, float fovRadiusPx, const glm::vec2& aimRef,
                                               const AimPredictionContext& prediction,
                                               bool useClosestBoneToFireport) const;

    std::optional<TargetResult> refreshTargetByInstance(const std::vector<Player>& snapshot, uint64_t instance,
                                                        float maxDistance, float fovRadiusPx,
                                                        const glm::vec2& aimRef,
                                                        const AimPredictionContext& prediction,
                                                        bool useClosestBoneToFireport) const;

    void clearTargetState(bool keyIsHeld);

private:
    mutable std::shared_mutex m_targetMutex;

    std::optional<TargetResult> m_liveTarget;
    std::optional<TargetResult> m_activeTarget;

    bool m_wasKeyHeld = false;
    glm::vec2 m_moveRemainder{};
    std::chrono::steady_clock::time_point m_lastMoveTime{};

};

extern ReadOnlyAim readOnlyAim;
