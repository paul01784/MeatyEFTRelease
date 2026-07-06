#include "headers/readOnlyAim.h"
#include "headers/camera.h"
#include "headers/fireport.h"
#include "../app/globals.h"
#include <limits>
#include <cmath>
#include "../app/makcu.h"

ReadOnlyAim readOnlyAim;

AimReferencePoint ReadOnlyAim::resolveAimReference() const
{
    const glm::vec2 screenCentre(espGlobals::gameRes.x * 0.5f, espGlobals::gameRes.y * 0.5f);
    if (aimGlobals::aimReference != AimReference::Fireport)
        return {screenCentre, true, false};

    const FireportPose pose = g_fireport.snapshot();
    if (pose.aimRefOk)
        return {pose.screenEnd, true, true};

    return {screenCentre, false, true};
}

std::optional<TargetResult> ReadOnlyAim::BuildTargetResult(const PlayerCache& entity, float maxDistance,
                                                             float fovRadiusPx, const glm::vec2& aimRef) const
{
    if (entity.isDead || entity.hasExfiled)
        return std::nullopt;

    if (!mainGame.localGroupId.empty() && entity.groupId == mainGame.localGroupId)
        return std::nullopt;

    const glm::vec3 worldDelta = entity.location - mainGame.localLocation;

    const float worldDistanceSq = glm::dot(worldDelta, worldDelta);
    const float maxDistanceSq = maxDistance * maxDistance;

    if (!std::isfinite(worldDistanceSq) || worldDistanceSq > maxDistanceSq)
        return std::nullopt;

    const boneListIndexes selectedBone = entity.isAi ? aimGlobals::aiBone : aimGlobals::pmcBone;

    glm::vec3 selectedBoneWorldPos{};
    if (!GetSelectedBonePosition(entity, selectedBone, selectedBoneWorldPos))
        return std::nullopt;

    glm::vec2 selectedBoneScreenPos{};
    if (!Utils::Camera::world_to_screen(selectedBoneWorldPos, &selectedBoneScreenPos))
        return std::nullopt;

    if (!std::isfinite(selectedBoneScreenPos.x) || !std::isfinite(selectedBoneScreenPos.y))
        return std::nullopt;

    const glm::vec2 screenDelta = selectedBoneScreenPos - aimRef;
    const float screenDistanceSq = glm::dot(screenDelta, screenDelta);
    const float fovRadiusSq = fovRadiusPx * fovRadiusPx;

    if (!std::isfinite(screenDistanceSq) || screenDistanceSq > fovRadiusSq)
        return std::nullopt;

    TargetResult result{};
    result.player = entity;
    result.selectedBone = selectedBone;
    result.boneWorldPos = selectedBoneWorldPos;
    result.screenPos = selectedBoneScreenPos;
    result.worldDistanceSq = worldDistanceSq;
    result.screenDistanceSq = screenDistanceSq;
    return result;
}

bool ReadOnlyAim::GetSelectedBonePosition(const PlayerCache& entity, boneListIndexes selectedBone,
                                          glm::vec3& outPosition) const
{
    const size_t boneIndex = static_cast<size_t>(selectedBone);

    if (boneIndex >= entity.bonePositions.size())
        return false;

    const glm::vec3& position = entity.bonePositions[boneIndex];

    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return false;

    if (position == glm::vec3{})
        return false;

    outPosition = position;
    return true;
}

std::optional<TargetResult> ReadOnlyAim::FindBestTarget(const std::vector<PlayerCache>& snapshot, TargetMode mode,
                                                        float maxDistance, float fovRadiusPx,
                                                        const glm::vec2& aimRef) const
{
    std::optional<TargetResult> bestTarget;

    for (const PlayerCache& entity : snapshot) {
        const auto candidate = BuildTargetResult(entity, maxDistance, fovRadiusPx, aimRef);
        if (!candidate.has_value())
            continue;

        if (!bestTarget.has_value()) {
            bestTarget = candidate;
            continue;
        }

        bool replace = false;
        switch (mode) {
        case TargetMode::FOV:
            replace = candidate->screenDistanceSq < bestTarget->screenDistanceSq;
            break;
        case TargetMode::CQB: {
            constexpr float distanceTieThresholdSq = 0.01f;
            if (candidate->worldDistanceSq < bestTarget->worldDistanceSq)
                replace = true;
            else if (std::fabs(candidate->worldDistanceSq - bestTarget->worldDistanceSq) < distanceTieThresholdSq)
                replace = candidate->screenDistanceSq < bestTarget->screenDistanceSq;
            break;
        }
        default:
            break;
        }

        if (replace)
            bestTarget = candidate;
    }

    return bestTarget;
}

std::optional<TargetResult> ReadOnlyAim::RefreshTargetByInstance(const std::vector<PlayerCache>& snapshot,
                                                                 uint64_t instance, float maxDistance,
                                                                 float fovRadiusPx, const glm::vec2& aimRef) const
{
    if (!instance)
        return std::nullopt;

    for (const PlayerCache& entity : snapshot) {
        if (entity.instance != instance)
            continue;
        return BuildTargetResult(entity, maxDistance, fovRadiusPx, aimRef);
    }

    return std::nullopt;
}

void ReadOnlyAim::ClearTargetState(bool keyIsHeld)
{
    std::unique_lock lock(m_targetMutex);
    m_liveTarget.reset();
    m_activeTarget.reset();
    m_wasKeyHeld = keyIsHeld;
}

void ReadOnlyAim::aimTask()
{
    if (!makcu.IsConnected())
        return;

    // Small sleep for moment testing
    if (espGlobals::drawFireportLine)
    {
        Sleep(20);
        g_fireport.update(mainGame.localPlayerPtr);
    }

    const bool keyIsHeld = mem.GetKeyboard()->IsKeyDown(static_cast<int>(keyGlobals::aimKey));

    if (!camera.cameraPointersReady()) {
        ClearTargetState(keyIsHeld);
        return;
    }

    //g_fireport.update(mainGame.localPlayerPtr);
    const AimReferencePoint aimRefPoint = resolveAimReference();
    const glm::vec2 aimRef = aimRefPoint.valid ? aimRefPoint.pos
                                               : glm::vec2(espGlobals::gameRes.x * 0.5f, espGlobals::gameRes.y * 0.5f);
    const bool fireportReady = aimRefPoint.valid || aimGlobals::aimReference != AimReference::Fireport;

    const std::vector<PlayerCache> snapshot = players.getCacheSnapshot();
    if (snapshot.empty()) {
        ClearTargetState(keyIsHeld);
        return;
    }

    const float maxDistance = static_cast<float>(aimGlobals::aimDistance);
    const float fovRadius = aimGlobals::aimFOV;
    const bool targetLockEnabled = aimGlobals::targetLock;

    const auto liveTarget =
        fireportReady ? FindBestTarget(snapshot, aimGlobals::targetMode, maxDistance, fovRadius, aimRef) : std::nullopt;

    std::optional<TargetResult> previousActiveTarget;
    bool wasKeyHeld = false;
    {
        std::shared_lock lock(m_targetMutex);
        previousActiveTarget = m_activeTarget;
        wasKeyHeld = m_wasKeyHeld;
    }

    const bool keyJustPressed = keyIsHeld && !wasKeyHeld;
    std::optional<TargetResult> newActiveTarget;

    if (!keyIsHeld) {
        newActiveTarget.reset();
    } else if (!targetLockEnabled) {
        newActiveTarget = liveTarget;
    } else {
        if (keyJustPressed || !previousActiveTarget.has_value())
            newActiveTarget = liveTarget;
        else
            newActiveTarget =
                RefreshTargetByInstance(snapshot, previousActiveTarget->player.instance, maxDistance, fovRadius, aimRef);
    }

    std::optional<TargetResult> targetToMove;
    {
        std::unique_lock lock(m_targetMutex);
        m_liveTarget = liveTarget;
        m_activeTarget = newActiveTarget;
        m_wasKeyHeld = keyIsHeld;
        targetToMove = m_activeTarget;
    }

    if (keyIsHeld && fireportReady && targetToMove.has_value())
        MoveToTargetBone(*targetToMove, aimRef);
}

bool ReadOnlyAim::MoveToTargetBone(const TargetResult& target, const glm::vec2& aimRef)
{
    if (!aimGlobals::aimEnabled || !makcu.IsConnected())
        return false;

    const float errorX = target.screenPos.x - aimRef.x;
    const float errorY = target.screenPos.y - aimRef.y;

    if (!std::isfinite(errorX) || !std::isfinite(errorY))
        return false;

    constexpr float deadZonePixels = 1.0f;
    if ((errorX * errorX) + (errorY * errorY) <= (deadZonePixels * deadZonePixels))
        return false;

    const float smooth = std::max(1.0f, aimGlobals::aimSmooth);

    float moveX = (errorX / smooth) * makcu.mouseUnitsPerScreenPixelX;
    float moveY = (errorY / smooth) * makcu.mouseUnitsPerScreenPixelY;

    constexpr float maxMovePerTick = 127.0f;
    moveX = std::clamp(moveX, -maxMovePerTick, maxMovePerTick);
    moveY = std::clamp(moveY, -maxMovePerTick, maxMovePerTick);

    const int dx = static_cast<int>(std::lround(moveX));
    const int dy = static_cast<int>(std::lround(moveY));

    if (dx == 0 && dy == 0)
        return false;

    return makcu.Move(dx, dy, 10);
}

std::optional<TargetResult> ReadOnlyAim::GetLiveTarget() const
{
    std::shared_lock lock(m_targetMutex);
    return m_liveTarget;
}

std::optional<TargetResult> ReadOnlyAim::GetActiveTarget() const
{
    std::shared_lock lock(m_targetMutex);
    return m_activeTarget;
}
