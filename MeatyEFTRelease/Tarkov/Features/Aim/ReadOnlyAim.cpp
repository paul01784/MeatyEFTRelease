#include "ReadOnlyAim.h"
#include "../../Unity/cameraManager.h"
#include "FireportTracker.h"
#include "../../../UI/globals.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include "../../../UI/makcu.h"

ReadOnlyAim readOnlyAim;

namespace
{
    constexpr std::array<boneListIndexes, 12> kAimBones =
    {
        boneListIndexes::Pelvis,
        boneListIndexes::Head,
        boneListIndexes::Neck,
        boneListIndexes::Spine,
        boneListIndexes::LForearm,
        boneListIndexes::LPalm,
        boneListIndexes::RForearm,
        boneListIndexes::RPalm,
        boneListIndexes::LThigh,
        boneListIndexes::LFoot,
        boneListIndexes::RThigh,
        boneListIndexes::RFoot
    };

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
    const FireportPose pose = g_fireport.snapshot();
    if (pose.aimRefOk)
        return {pose.screenEnd, true};

    return {};
}

std::optional<TargetResult> ReadOnlyAim::buildTargetResult(const Player& entity, float maxDistance, float fovRadiusPx, const glm::vec2& aimRef, const AimPredictionContext& prediction, bool useClosestBoneToFireport) const
{
    if (!Utils::valid_pointer(entity.instance) ||
        entity.instance == mainGame.localPlayerPtr ||
        entity.isLocal ||
        entity.isBTR ||
        entity.isInBTR ||
        entity.isZombie ||
        entity.isFriend ||
        entity.isDead ||
        entity.hasExfiled)
    {
        return std::nullopt;
    }

    if (!mainGame.localGroupId.empty() && entity.groupId == mainGame.localGroupId)
        return std::nullopt;

    const glm::vec3 worldDelta = entity.location - mainGame.localLocation;

    const float worldDistanceSq = glm::dot(worldDelta, worldDelta);
    const float maxDistanceSq = maxDistance * maxDistance;

    if (!std::isfinite(worldDistanceSq) || worldDistanceSq > maxDistanceSq)
        return std::nullopt;

    boneListIndexes selectedBone = entity.isAi ? aimGlobals::aiBone : aimGlobals::pmcBone;
    glm::vec3 selectedBoneWorldPos{};
    const bool foundSelectedBone = useClosestBoneToFireport
        ? getClosestBoneToAimReference(entity, aimRef, selectedBone, selectedBoneWorldPos)
        : getSelectedBonePosition(entity, selectedBone, selectedBoneWorldPos);

    if (!foundSelectedBone)
        return std::nullopt;

    if (prediction.enabled)
    {
        const BallisticSimulationResult simulation = BallisticsCalculator::Simulate(prediction.sourcePosition, selectedBoneWorldPos, prediction.ballistics);

        if (simulation.valid)
        {
            const bool freshVelocity = entity.velocityValid && entity.lastVelocityUpdate !=
                    std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() -
                    entity.lastVelocityUpdate <= std::chrono::milliseconds(250);

            if (freshVelocity)
            {
                selectedBoneWorldPos += entity.velocity * simulation.travelTime;
            }

            selectedBoneWorldPos.y += simulation.dropCompensation;
        }
    }

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

bool ReadOnlyAim::getSelectedBonePosition(const Player& entity, boneListIndexes selectedBone, glm::vec3& outPosition) const
{
    const size_t boneIndex = static_cast<size_t>(selectedBone);

    if (boneIndex >= entity.bonePositions.size() ||
        boneIndex >= entity.bonePtrs.size() ||
        !Utils::valid_pointer(entity.bonePtrs[boneIndex]))
    {
        return false;
    }

    const glm::vec3& position = entity.bonePositions[boneIndex];

    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return false;

    if (position == glm::vec3{})
        return false;

    outPosition = position;
    return true;
}

bool ReadOnlyAim::getClosestBoneToAimReference(const Player& entity, const glm::vec2& aimRef, boneListIndexes& outBone, glm::vec3& outPosition) const
{
    bool foundBone = false;
    float closestDistanceSq = std::numeric_limits<float>::max();

    for (const boneListIndexes bone : kAimBones)
    {
        glm::vec3 bonePosition{};
        if (!getSelectedBonePosition(entity, bone, bonePosition))
            continue;

        glm::vec2 boneScreenPosition{};
        if (!Utils::Camera::world_to_screen(bonePosition, &boneScreenPosition))
            continue;

        const glm::vec2 delta = boneScreenPosition - aimRef;
        const float distanceSq = glm::dot(delta, delta);

        if (!std::isfinite(distanceSq) || distanceSq >= closestDistanceSq)
            continue;

        closestDistanceSq = distanceSq;
        outBone = bone;
        outPosition = bonePosition;
        foundBone = true;
    }

    return foundBone;
}

std::optional<TargetResult> ReadOnlyAim::findBestTarget(const std::vector<Player>& snapshot, TargetMode mode, float maxDistance, float fovRadiusPx, const glm::vec2& aimRef, const AimPredictionContext& prediction, bool useClosestBoneToFireport) const
{
    std::optional<TargetResult> bestTarget;

    for (const Player& entity : snapshot) {
        const auto candidate = buildTargetResult(
            entity,
            maxDistance,
            fovRadiusPx,
            aimRef,
            prediction,
            useClosestBoneToFireport);
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

std::optional<TargetResult> ReadOnlyAim::refreshTargetByInstance(const std::vector<Player>& snapshot,uint64_t instance, float maxDistance, float fovRadiusPx, const glm::vec2& aimRef, const AimPredictionContext& prediction, bool useClosestBoneToFireport) const
{
    if (!instance)
        return std::nullopt;

    for (const Player& entity : snapshot) {
        if (entity.instance != instance)
            continue;
        return buildTargetResult(
            entity,
            maxDistance,
            fovRadiusPx,
            aimRef,
            prediction,
            useClosestBoneToFireport);
    }

    return std::nullopt;
}

void ReadOnlyAim::clearTargetState(bool keyIsHeld)
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
        clearTargetState(false);
        return;
    }

    if (!mem.IsDmaOperational())
    {
        clearTargetState(false);
        return;
    }

    if (!aimGlobals::aimEnabled)
    {
        clearTargetState(false);
        return;
    }

    auto* keyboard = mem.GetKeyboard();
    if (!keyboard)
    {
        clearTargetState(false);
        return;
    }

    const bool keyIsHeld = keyboard->IsKeyDown(static_cast<int>(keyGlobals::aimKey));

    const CameraManagerSnapshot cameraSnapshot = cameraManagerTest.snapshot();
    if (!cameraSnapshot || !cameraSnapshot->valid) {
        clearTargetState(keyIsHeld);
        return;
    }

    const AimReferencePoint aimRefPoint = resolveAimReference();
    if (!aimRefPoint.valid)
    {
        clearTargetState(keyIsHeld);
        return;
    }

    const glm::vec2 aimRef = aimRefPoint.pos;

    const PlayerSnapshot snapshotHandle = registeredPlayers.getCacheSnapshot();
    const PlayerCollection& snapshot = *snapshotHandle;
    if (snapshot.empty()) {
        clearTargetState(keyIsHeld);
        return;
    }

    const float maxDistance = std::clamp(static_cast<float>(aimGlobals::aimDistance), 1.0f, 2000.0f);
    const float fovRadius = std::isfinite(aimGlobals::aimFOV)
        ? std::clamp(aimGlobals::aimFOV, 1.0f, 200.0f)
        : 1.0f;
    const bool targetLockEnabled = aimGlobals::targetLock;
    const bool useClosestBoneToFireport = aimGlobals::aimClosestBoneToFireport;

    AimPredictionContext prediction{};

    if (aimGlobals::predictionEnabled)
    {
        const auto localPlayer = std::find_if(
            snapshot.begin(),
            snapshot.end(),
            [](const Player& player)
            {
                return player.isLocal;
            });

        if (localPlayer != snapshot.end() &&
            localPlayer->observedHandsInfo.ballistics.IsValid())
        {
            const FireportPose fireport = g_fireport.snapshot();

            prediction.enabled = true;
            prediction.sourcePosition = fireport.valid
                ? fireport.worldOrigin
                : mainGame.localLocation;
            prediction.ballistics = localPlayer->observedHandsInfo.ballistics;
        }
    }

    const auto liveTarget = findBestTarget(
        snapshot,
        aimGlobals::targetMode,
        maxDistance,
        fovRadius,
        aimRef,
        prediction,
        useClosestBoneToFireport);

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
                refreshTargetByInstance(
                    snapshot,
                    previousActiveTarget->player.instance,
                    maxDistance,
                    fovRadius,
                    aimRef,
                    prediction,
                    useClosestBoneToFireport);
    }

    std::optional<TargetResult> targetToMove;
    {
        std::unique_lock lock(m_targetMutex);
        m_liveTarget = liveTarget;
        m_activeTarget = newActiveTarget;
        m_wasKeyHeld = keyIsHeld;
        targetToMove = m_activeTarget;
    }

    if (keyIsHeld && targetToMove.has_value())
        moveToTargetBone(*targetToMove, aimRef);
    else
    {
        m_moveRemainder = {};
        m_lastMoveTime = {};
    }
}

bool ReadOnlyAim::moveToTargetBone(const TargetResult& target, const glm::vec2& aimRef)
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

    if (!makcu.Move(dx, dy, 25))
    {
        return false;
    }

    m_moveRemainder = nextRemainder;
    return true;
}

std::optional<TargetResult> ReadOnlyAim::getLiveTarget() const
{
    std::shared_lock lock(m_targetMutex);
    return m_liveTarget;
}

std::optional<TargetResult> ReadOnlyAim::getActiveTarget() const
{
    std::shared_lock lock(m_targetMutex);
    return m_activeTarget;
}
