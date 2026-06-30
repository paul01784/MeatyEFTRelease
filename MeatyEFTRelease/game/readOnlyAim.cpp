#include "headers/readOnlyAim.h"
#include "headers/camera.h"
#include <limits>
#include <cmath>
#include "../app/makcu.h"

ReadOnlyAim readOnlyAim;

std::optional<TargetResult> ReadOnlyAim::BuildTargetResult(const PlayerCache& entity, float maxDistance, float fovRadiusPx) const
{
    if (entity.isDead || entity.hasExfiled)
        return std::nullopt;

    // Skip localplayer groupId matching players :(
    if (!mainGame.localGroupId.empty() &&
        entity.groupId == mainGame.localGroupId)
    {
        return std::nullopt;
    }

    const glm::vec3 worldDelta = entity.location - mainGame.localLocation;

    const float worldDistanceSq = glm::dot(worldDelta, worldDelta);
    const float maxDistanceSq = maxDistance * maxDistance;

    if (!std::isfinite(worldDistanceSq) || worldDistanceSq > maxDistanceSq)
    {
        return std::nullopt;
    }

    const boneListIndexes selectedBone =
        entity.isAi
        ? aimGlobals::aiBone
        : aimGlobals::pmcBone;

    glm::vec3 selectedBoneWorldPos{};

    if (!GetSelectedBonePosition(entity, selectedBone, selectedBoneWorldPos))
    {
        return std::nullopt;
    }

    glm::vec2 selectedBoneScreenPos{};

    if (!Utils::Camera::world_to_screen(selectedBoneWorldPos, &selectedBoneScreenPos))
    {
        return std::nullopt;
    }

    if (!std::isfinite(selectedBoneScreenPos.x) || !std::isfinite(selectedBoneScreenPos.y))
    {
        return std::nullopt;
    }

    const glm::vec2 screenCentre( espGlobals::gameRes.x * 0.5f, espGlobals::gameRes.y * 0.5f);

    const glm::vec2 screenDelta = selectedBoneScreenPos - screenCentre;

    const float screenDistanceSq = glm::dot(screenDelta, screenDelta);

    const float fovRadiusSq = fovRadiusPx * fovRadiusPx;

    if (!std::isfinite(screenDistanceSq) || screenDistanceSq > fovRadiusSq)
    {
        return std::nullopt;
    }

    TargetResult result{};

    result.player = entity;
    result.selectedBone = selectedBone;
    result.boneWorldPos = selectedBoneWorldPos;
    result.screenPos = selectedBoneScreenPos;
    result.worldDistanceSq = worldDistanceSq;
    result.screenDistanceSq = screenDistanceSq;

    return result;
}

bool ReadOnlyAim::GetSelectedBonePosition(const PlayerCache& entity, boneListIndexes selectedBone, glm::vec3& outPosition) const
{
    const size_t boneIndex = static_cast<size_t>(selectedBone);

    if (boneIndex >= entity.bonePositions.size())
        return false;

    const glm::vec3& position = entity.bonePositions[boneIndex];

    if (!std::isfinite(position.x) ||
        !std::isfinite(position.y) ||
        !std::isfinite(position.z))
    {
        return false;
    }

    if (position == glm::vec3{})
        return false;

    outPosition = position;
    return true;
}

std::optional<TargetResult> ReadOnlyAim::FindBestTarget(const std::vector<PlayerCache>& snapshot, TargetMode mode, float maxDistance, float fovRadiusPx) const
{
    std::optional<TargetResult> bestTarget;

    for (const PlayerCache& entity : snapshot)
    {
        const auto candidate = BuildTargetResult(entity, maxDistance, fovRadiusPx);

        if (!candidate.has_value())
            continue;

        if (!bestTarget.has_value())
        {
            bestTarget = candidate;
            continue;
        }

        bool replace = false;

        switch (mode)
        {
        case TargetMode::FOV:
            replace = candidate->screenDistanceSq < bestTarget->screenDistanceSq;
            break;

        case TargetMode::CQB:
        {
            constexpr float distanceTieThresholdSq = 0.01f;

            if (candidate->worldDistanceSq < bestTarget->worldDistanceSq)
            {
                replace = true;
            }
            else if (std::fabs(candidate->worldDistanceSq - bestTarget->worldDistanceSq) < distanceTieThresholdSq)
            {
                replace = candidate->screenDistanceSq < bestTarget->screenDistanceSq;
            }

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


std::optional<TargetResult> ReadOnlyAim::RefreshTargetByInstance(const std::vector<PlayerCache>& snapshot, uint64_t instance, float maxDistance, float fovRadiusPx) const
{
    if (!instance)
        return std::nullopt;

    for (const PlayerCache& entity : snapshot)
    {
        if (entity.instance != instance)
            continue;

        return BuildTargetResult(entity, maxDistance, fovRadiusPx);
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
    // Get current aim-key state
    const bool keyIsHeld = mem.GetKeyboard()->IsKeyDown(static_cast<int>(keyGlobals::aimKey));

    if (!camera.cameraPointersReady())
    {
        ClearTargetState(keyIsHeld);
        return;
    }

    const std::vector<PlayerCache> snapshot = players.getCacheSnapshot();

    if (snapshot.empty())
    {
        ClearTargetState(keyIsHeld);
        return;
    }

    const float maxDistance = aimGlobals::aimDistance;
    const float fovRadius = aimGlobals::aimFOV;
    const bool targetLockEnabled = aimGlobals::targetLock;

    // Best current target, used for nonlock mode
    const auto liveTarget = FindBestTarget(snapshot, aimGlobals::targetMode, maxDistance, fovRadius);

    std::optional<TargetResult> previousActiveTarget;
    bool wasKeyHeld = false;

    {
        std::shared_lock lock(m_targetMutex);

        previousActiveTarget = m_activeTarget;
        wasKeyHeld = m_wasKeyHeld;
    }

    const bool keyJustPressed = keyIsHeld && !wasKeyHeld;

    std::optional<TargetResult> newActiveTarget;

    if (!keyIsHeld)
    {
        // Aim key released: remove the active target
        newActiveTarget.reset();
    }
    else if (!targetLockEnabled)
    {
        // No target lock: always follow the best current target
        newActiveTarget = liveTarget;
    }
    else
    {
        // Target lock enabled.
        if (keyJustPressed || !previousActiveTarget.has_value())
        {
            // First press: lock the current best candidate
            newActiveTarget = liveTarget;
        }
        else
        {
            // Keep the same player instance, but update bone/screen data
            newActiveTarget = RefreshTargetByInstance(snapshot, previousActiveTarget->player.instance, maxDistance, fovRadius);
        }
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
    {
        MoveToTargetBone(*targetToMove);
    }
}

bool ReadOnlyAim::MoveToTargetBone(const TargetResult& target)
{
    if (!aimGlobals::aimEnabled || !makcu.IsConnected())
        return false;

    const glm::vec2 screenCentre(espGlobals::gameRes.x * 0.5f, espGlobals::gameRes.y * 0.5f);

    const float errorX = target.screenPos.x - screenCentre.x;
    const float errorY = target.screenPos.y - screenCentre.y;

    if (!std::isfinite(errorX) || !std::isfinite(errorY))
        return false;

    constexpr float deadZonePixels = 1.0f;

    if ((errorX * errorX) + (errorY * errorY) <=
        (deadZonePixels * deadZonePixels))
    {
        return false;
    }

    const float smooth = std::max(1.0f, aimGlobals::aimSmooth);

    float moveX =
        (errorX / smooth) *
        makcu.mouseUnitsPerScreenPixelX;

    float moveY =
        (errorY / smooth) *
        makcu.mouseUnitsPerScreenPixelY;

    // Protection against stale W2S
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

