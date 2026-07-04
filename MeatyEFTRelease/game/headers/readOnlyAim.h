#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <optional>
#include <glm/glm.hpp>
#include "players.h"
#include "../../app/render.h"

struct TargetResult
{
    PlayerCache player{};

    boneListIndexes selectedBone = boneListIndexes::Head;

    glm::vec3 boneWorldPos{};
    glm::vec2 screenPos{};

    float worldDistanceSq = FLT_MAX;
    float screenDistanceSq = FLT_MAX;
};

struct AimReferencePoint
{
    glm::vec2 pos{};
    bool valid = true;
    bool fireportMode = false;
};

class ReadOnlyAim
{
public:
    void aimTask();

    bool MoveToTargetBone(const TargetResult& target, const glm::vec2& aimRef);

    std::optional<TargetResult> GetLiveTarget() const;
    std::optional<TargetResult> GetActiveTarget() const;

    AimReferencePoint resolveAimReference() const;

private:
    std::optional<TargetResult> BuildTargetResult(const PlayerCache& entity, float maxDistance, float fovRadiusPx,
                                                  const glm::vec2& aimRef) const;

    bool GetSelectedBonePosition(const PlayerCache& entity, boneListIndexes selectedBone, glm::vec3& outPosition) const;

    std::optional<TargetResult> FindBestTarget(const std::vector<PlayerCache>& snapshot, TargetMode mode,
                                               float maxDistance, float fovRadiusPx, const glm::vec2& aimRef) const;

    std::optional<TargetResult> RefreshTargetByInstance(const std::vector<PlayerCache>& snapshot, uint64_t instance,
                                                        float maxDistance, float fovRadiusPx,
                                                        const glm::vec2& aimRef) const;

    void ClearTargetState(bool keyIsHeld);

private:
    mutable std::shared_mutex m_targetMutex;

    std::optional<TargetResult> m_liveTarget;
    std::optional<TargetResult> m_activeTarget;

    bool m_wasKeyHeld = false;

};

extern ReadOnlyAim readOnlyAim;
