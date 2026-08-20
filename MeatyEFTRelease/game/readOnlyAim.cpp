#include "headers/readOnlyAim.h"
#include "headers/camera.h"
#include "headers/fireport.h"
#include "../app/globals.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <cmath>
#include "../app/makcu.h"

ReadOnlyAim readOnlyAim;

namespace
{
    glm::vec2 ApplyAimOffset(const glm::vec2& screenPosition)
    {
        const float offsetX = std::isfinite(aimGlobals::aimOffsetX)
            ? std::clamp(aimGlobals::aimOffsetX, -1000.0f, 1000.0f)
            : 0.0f;
        const float offsetY = std::isfinite(aimGlobals::aimOffsetY)
            ? std::clamp(aimGlobals::aimOffsetY, -1000.0f, 1000.0f)
            : 0.0f;

        // Screen Y grows downwards, so a positive Y offset means "raise aim".
        return {screenPosition.x + offsetX, screenPosition.y - offsetY};
    }
}

AimReferencePoint ReadOnlyAim::resolveAimReference() const
{
    const glm::vec2 screenCentre(espGlobals::gameRes.x * 0.5f, espGlobals::gameRes.y * 0.5f);
    if (aimGlobals::aimReference != AimReference::Fireport)
        return {screenCentre, true, false};

    const FireportPose pose = g_fireport.snapshot();
    if (pose.aimRefOk)
        return {pose.screenEnd, true, true, false};

    // Some weapons do not expose a usable fireport, and the projected ray
    // can also leave the screen. Keep aim usable by falling back to the
    // crosshair reference until a valid fireport is available again
    return {screenCentre, true, true, true};
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

    const glm::vec2 adjustedScreenPos = ApplyAimOffset(selectedBoneScreenPos);
    const glm::vec2 screenDelta = adjustedScreenPos - aimRef;
    const float screenDistanceSq = glm::dot(screenDelta, screenDelta);
    const float fovRadiusSq = fovRadiusPx * fovRadiusPx;

    if (!std::isfinite(screenDistanceSq) || screenDistanceSq > fovRadiusSq)
        return std::nullopt;

    TargetResult result{};
    result.player = entity;
    result.selectedBone = selectedBone;
    result.boneWorldPos = selectedBoneWorldPos;
    result.screenPos = adjustedScreenPos;
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
    m_moveRemainder = {};
    m_lastMoveTime = {};
}

void ReadOnlyAim::aimTask()
{
    if (!makcu.IsConnected())
    {
        ClearTargetState(false);
        return;
    }

    if (!aimGlobals::aimEnabled)
    {
        ClearTargetState(false);
        return;
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
    const bool fireportReady = aimRefPoint.valid;

    const PlayerCacheSnapshot snapshotHandle = players.getCacheSnapshot();
    const PlayerCacheCollection& snapshot = *snapshotHandle;
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
    else
    {
        m_moveRemainder = {};
        m_lastMoveTime = {};
    }
}

bool ReadOnlyAim::MoveToTargetBone(const TargetResult& target, const glm::vec2& aimRef)
{
    if (!aimGlobals::aimEnabled || !makcu.IsConnected())
    {
        m_moveRemainder = {};
        m_lastMoveTime = {};
        return false;
    }

    const float errorX = target.screenPos.x - aimRef.x;
    const float errorY = target.screenPos.y - aimRef.y;

    if (!std::isfinite(errorX) || !std::isfinite(errorY))
        return false;

    const float errorDistanceSq = (errorX * errorX) + (errorY * errorY);
    if (!std::isfinite(errorDistanceSq))
        return false;

    const float deadZonePixels = std::isfinite(aimGlobals::aimDeadzonePixels)
        ? std::clamp(aimGlobals::aimDeadzonePixels, 0.1f, 1000.0f)
        : 1.0f;
    if (errorDistanceSq <= (deadZonePixels * deadZonePixels))
    {
        m_moveRemainder = {};
        m_lastMoveTime = {};
        return false;
    }

    const float smooth = std::isfinite(aimGlobals::aimSmooth)
        ? std::clamp(aimGlobals::aimSmooth, 1.0f, 1000.0f)
        : 1.0f;
    const float speedPixelsPerSecond = std::isfinite(aimGlobals::aimSpeedPixelsPerSecond)
        ? std::clamp(aimGlobals::aimSpeedPixelsPerSecond, 1.0f, 10000.0f)
        : 1200.0f;

    const auto now = std::chrono::steady_clock::now();
    float deltaSeconds = 0.01f;
    if (m_lastMoveTime != std::chrono::steady_clock::time_point{})
    {
        deltaSeconds = std::chrono::duration<float>(now - m_lastMoveTime).count();
        if (!std::isfinite(deltaSeconds))
            deltaSeconds = 0.01f;
    }
    m_lastMoveTime = now;

    // Limit the effect of a stalled task
    deltaSeconds = std::clamp(deltaSeconds, 0.001f, 0.05f);

    const float errorDistance = std::sqrt(errorDistanceSq);
    const float responseDistance = errorDistance / smooth;
    const float maxStepPixels = speedPixelsPerSecond * deltaSeconds;
    const float stepDistance = std::min(responseDistance, maxStepPixels);

    if (!std::isfinite(stepDistance) || stepDistance <= 0.0f)
        return false;

    const glm::vec2 screenMove = glm::vec2(errorX, errorY) * (stepDistance / errorDistance);
    const float calibrationX = std::isfinite(makcu.mouseUnitsPerScreenPixelX)
        ? std::max(0.001f, makcu.mouseUnitsPerScreenPixelX)
        : 1.0f;
    const float calibrationY = std::isfinite(makcu.mouseUnitsPerScreenPixelY)
        ? std::max(0.001f, makcu.mouseUnitsPerScreenPixelY)
        : 1.0f;

    glm::vec2 hardwareMove = {
        screenMove.x * calibrationX + m_moveRemainder.x,
        screenMove.y * calibrationY + m_moveRemainder.y
    };

    constexpr float maxMovePerCommand = 127.0f;
    hardwareMove.x = std::clamp(hardwareMove.x, -maxMovePerCommand, maxMovePerCommand);
    hardwareMove.y = std::clamp(hardwareMove.y, -maxMovePerCommand, maxMovePerCommand);

    const int dx = static_cast<int>(std::lround(hardwareMove.x));
    const int dy = static_cast<int>(std::lround(hardwareMove.y));
    const glm::vec2 nextRemainder = hardwareMove - glm::vec2(dx, dy);

    if (dx == 0 && dy == 0)
    {
        m_moveRemainder = nextRemainder;
        return false;
    }

    if (!makcu.Move(dx, dy, 10))
        return false;

    m_moveRemainder = nextRemainder;
    return true;
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
