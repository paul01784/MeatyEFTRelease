#include "FireportTracker.h"

#include "../../Unity/cameraManager.h"
#include "../../SDK/EftOffsets.h"
#include "../../Unity/Transform.h"
#include "../../../Core/Utilities.h"
#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

extern Memory mem;

FireportTracker::FireportTracker()
    : publishedPose_(std::make_shared<const FireportPose>())
{
}

FireportTracker g_fireport;

namespace {

constexpr auto kFallbackPathRefreshInterval = std::chrono::seconds(3);
constexpr auto kFailedPathRefreshInterval = std::chrono::milliseconds(50);
constexpr std::uint32_t kPoseFailuresBeforeRebuild = 12;
constexpr auto kOutlierWindow = std::chrono::milliseconds(75);
constexpr float kMaximumOriginJumpM = 1.5f;
constexpr float kMaximumDirectionJumpRadians = 0.78539816339f;
constexpr float kOriginFilterTimeConstantSeconds = 0.012f;
constexpr float kDirectionFilterTimeConstantSeconds = 0.010f;

bool isGoodVec3(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::unique_ptr<UnityTransform> makeMuzzleTransform(uint64_t transformInternal)
{
    auto transform = std::make_unique<UnityTransform>(
        transformInternal,
        false);

    return transform->IsValid() ? std::move(transform) : nullptr;
}

std::unique_ptr<UnityTransform> makeMuzzleTransformFromBifacial(uint64_t bifacial)
{
    if (!Utils::valid_pointer(bifacial))
        return nullptr;

    const uint64_t original = mem.Read<uint64_t>(bifacial + sdk::BifacialTransform::Original);
    if (!Utils::valid_pointer(original))
        return nullptr;

    uint64_t native = 0;
    if (!UnityTransform::TryResolveNative(original, native, true))
        return nullptr;

    return makeMuzzleTransform(native);
}

std::unique_ptr<UnityTransform> makeMuzzleTransformFromManaged(uint64_t managed)
{
    uint64_t native = 0;
    if (!UnityTransform::TryResolveNative(managed, native, true))
        return nullptr;

    return makeMuzzleTransform(native);
}

bool onScreen(const glm::vec2& p, float w, float h)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0.f && p.x <= w && p.y >= 0.f && p.y <= h;
}

float smoothingAlpha(float deltaSeconds, float timeConstantSeconds)
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
        return 1.0f;

    return std::clamp(
        1.0f - std::exp(-deltaSeconds / timeConstantSeconds),
        0.0f,
        1.0f);
}

} // namespace

bool IsFireportPoseFresh(
    const FireportPose& pose,
    std::chrono::milliseconds maximumAge,
    std::chrono::steady_clock::time_point now) noexcept
{
    return pose.valid &&
        pose.publishedAt != std::chrono::steady_clock::time_point{} &&
        now >= pose.publishedAt &&
        (now - pose.publishedAt) <= maximumAge;
}

void FireportTracker::clear() noexcept
{
    consecutivePoseFailures_ = 0;
    const FireportPoseSnapshot current = publishedPose_.load(std::memory_order_acquire);

    if (current && !current->valid)
        return;

    publish(FireportPose{});
}

void FireportTracker::publish(FireportPose pose)
{
    pose.version = publishedVersion_.fetch_add(1, std::memory_order_relaxed) + 1;
    pose.publishedAt = std::chrono::steady_clock::now();
    publishedPose_.store(std::make_shared<const FireportPose>(std::move(pose)), std::memory_order_release);
}

FireportPoseSnapshot FireportTracker::getSnapshot() const noexcept
{
    FireportPoseSnapshot pose = publishedPose_.load(std::memory_order_acquire);

    if (pose)
        return pose;

    static const FireportPoseSnapshot emptyPose = std::make_shared<const FireportPose>();
    return emptyPose;
}

void FireportTracker::clearCachedMuzzle() noexcept
{
    muzzleTransform_.reset();
    cachedPath_ = nullptr;
    cachedPathIsFallback_ = false;
}

void FireportTracker::holdLastValidOrInvalidate(
    std::chrono::steady_clock::time_point now,
    bool forceInvalid)
{
    const FireportPoseSnapshot current = getSnapshot();
    if (!forceInvalid &&
        current &&
        IsFireportPoseFresh(*current, kFireportVisualMaxAge, now))
    {
        return;
    }

    if (current && current->valid)
        publish(FireportPose{});
}

bool FireportTracker::filterPose(
    FireportPose& pose,
    std::chrono::steady_clock::time_point now,
    bool resetFilter) const
{
    const FireportPoseSnapshot previous = getSnapshot();
    if (resetFilter ||
        !previous ||
        !IsFireportPoseFresh(*previous, kFireportVisualMaxAge, now) ||
        previous->pathUsed != pose.pathUsed)
    {
        return true;
    }

    const float deltaSeconds = std::chrono::duration<float>(
        now - previous->publishedAt).count();
    const glm::vec3 originDelta = pose.worldOrigin - previous->worldOrigin;
    const float originJump = glm::length(originDelta);
    const float directionDot = std::clamp(
        glm::dot(previous->worldForward, pose.worldForward),
        -1.0f,
        1.0f);
    const float directionJump = std::acos(directionDot);

    if ((now - previous->publishedAt) <= kOutlierWindow &&
        (originJump > kMaximumOriginJumpM ||
            directionJump > kMaximumDirectionJumpRadians))
    {
        return false;
    }

    const float originAlpha = smoothingAlpha(
        deltaSeconds,
        kOriginFilterTimeConstantSeconds);
    float directionAlpha = smoothingAlpha(
        deltaSeconds,
        kDirectionFilterTimeConstantSeconds);

    // Filter small sample noise while remaining responsive to deliberate
    // weapon and camera movement.
    const float directionResponse = std::clamp(
        directionJump / 0.10471975512f,
        0.0f,
        1.0f);
    directionAlpha = std::clamp(
        directionAlpha + directionResponse * 0.35f,
        0.0f,
        1.0f);

    pose.worldOrigin = previous->worldOrigin + originDelta * originAlpha;
    const glm::vec3 filteredDirection =
        previous->worldForward * (1.0f - directionAlpha) +
        pose.worldForward * directionAlpha;
    const float filteredLength = glm::length(filteredDirection);
    if (!std::isfinite(filteredLength) || filteredLength < 1e-4f)
        return false;

    pose.worldForward = filteredDirection / filteredLength;
    return true;
}

bool FireportTracker::refreshMuzzleTransform(uint64_t localPlayer, uint64_t handsController, std::chrono::steady_clock::time_point now)
{
    
    nextPathRefresh_ = now + kFailedPathRefreshInterval;

    auto chooseBifacial = [&](uint64_t bifacial, const char* path) -> std::unique_ptr<UnityTransform>
    {
        std::unique_ptr<UnityTransform> transform = makeMuzzleTransformFromBifacial(bifacial);

        if (transform)
        {
            cachedPath_ = path;
            cachedPathIsFallback_ = false;
        }

        return transform;
    };

    auto chooseManagedTransform = [&](uint64_t transformObject, const char* path) -> std::unique_ptr<UnityTransform>
    {
        std::unique_ptr<UnityTransform> transform = makeMuzzleTransformFromManaged(transformObject);

        if (transform)
        {
            cachedPath_ = path;
            cachedPathIsFallback_ = std::strcmp(path, "firearm.gun_base_transform") == 0;
        }

        return transform;
    };

    std::unique_ptr<UnityTransform> replacement = chooseBifacial(mem.Read<uint64_t>(handsController + sdk::FirearmController::Fireport), "firearm.fireport");

    if (!replacement)
    {
        const uint64_t playerBones = mem.Read<uint64_t>(localPlayer + sdk::Player::PlayerBones);

        if (Utils::valid_pointer(playerBones))
        {
            replacement = chooseBifacial(mem.Read<uint64_t>(playerBones + sdk::PlayerBones::Fireport), "player_bones.fireport");
        }
    }

    if (!replacement)
    {
        const uint64_t firearms = mem.Read<uint64_t>(handsController + sdk::FirearmController::Firearms);

        if (Utils::valid_pointer(firearms))
        {
            replacement = chooseBifacial(mem.Read<uint64_t>(firearms + sdk::Firearms::Fireport), "firearms._fireport");
        }
    }

    if (!replacement)
    {
        replacement = chooseManagedTransform(mem.Read<uint64_t>(handsController + sdk::FirearmController::GunBaseTransform), "firearm.gun_base_transform");
    }

    if (!replacement)
    {
        const uint64_t pwa = mem.Read<uint64_t>(localPlayer + sdk::Player::ProceduralWeaponAnimation);

        const uint64_t cachedPwa = mainGame.localPlayerPWA;
        const uint64_t pwaToUse = Utils::valid_pointer(pwa)
            ? pwa
            : cachedPwa;

        if (Utils::valid_pointer(pwaToUse))
        {
            const uint64_t handsSpring = mem.Read<uint64_t>(pwaToUse + sdk::ProceduralWeaponAnimation::HandsContainer);

            if (Utils::valid_pointer(handsSpring))
            {
                replacement = makeMuzzleTransformFromManaged(mem.Read<uint64_t>(handsSpring + sdk::PlayerSpring::Fireport));

                if (replacement)
                {
                    cachedPath_ = "pwa.spring.fireport";
                    cachedPathIsFallback_ = false;
                }
            }
        }
    }

    if (replacement)
    {
        muzzleTransform_ = std::move(replacement);
        cachedLocalPlayer_ = localPlayer;
        cachedHandsController_ = handsController;
        nextPathRefresh_ = cachedPathIsFallback_
            ? now + kFallbackPathRefreshInterval
            : (std::chrono::steady_clock::time_point::max)();
        return true;
    }

    if (cachedLocalPlayer_ != localPlayer ||
        cachedHandsController_ != handsController)
    {
        clearCachedMuzzle();
    }

    cachedLocalPlayer_ = localPlayer;
    cachedHandsController_ = handsController;

    return muzzleTransform_ && muzzleTransform_->IsValid();
}

void FireportTracker::update(uint64_t localPlayer, uint64_t handsController)
{
    FireportPose pose{};

    if (!Utils::valid_pointer(localPlayer)) {
        clearCachedMuzzle();
        cachedLocalPlayer_ = 0;
        cachedHandsController_ = 0;
        nextPathRefresh_ = {};
        consecutivePoseFailures_ = 0;
        publish(std::move(pose));
        return;
    }

    const uint64_t handsCtrl = handsController;
    if (!Utils::valid_pointer(handsCtrl)) {
        clearCachedMuzzle();
        cachedHandsController_ = 0;
        nextPathRefresh_ = {};
        consecutivePoseFailures_ = 0;
        publish(std::move(pose));
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool identityChanged =
        (cachedLocalPlayer_ != 0 && cachedLocalPlayer_ != localPlayer) ||
        (cachedHandsController_ != 0 && cachedHandsController_ != handsCtrl);
    const char* previousPath = cachedPath_;
    const bool pathRefreshDue = cachedLocalPlayer_ != localPlayer || cachedHandsController_ != handsCtrl || ((!muzzleTransform_ || cachedPathIsFallback_) && now >= nextPathRefresh_);

    if (pathRefreshDue)
    {
        refreshMuzzleTransform(localPlayer, handsCtrl, now);
    }

    if (!muzzleTransform_)
    {
        ++consecutivePoseFailures_;
        holdLastValidOrInvalidate(now, identityChanged);
        return;
    }

    glm::quat rotation{};
    const TransformPoseReadResult poseRead =
        muzzleTransform_->TryUpdateWorldPose(pose.worldOrigin, rotation);
    if (poseRead != TransformPoseReadResult::Success)
    {
        if (poseRead == TransformPoseReadResult::Failed)
        {
            ++consecutivePoseFailures_;
            if (!muzzleTransform_->IsValid() ||
                consecutivePoseFailures_ >= kPoseFailuresBeforeRebuild)
            {
                clearCachedMuzzle();
                nextPathRefresh_ = now + kFailedPathRefreshInterval;
            }
        }

        holdLastValidOrInvalidate(now, identityChanged);
        return;
    }

    pose.worldForward = UnityTransformExtensions::Down(rotation);
    const float directionLength = glm::length(pose.worldForward);

    if (directionLength < 1e-4f || !isGoodVec3(pose.worldOrigin) || !isGoodVec3(pose.worldForward))
    {
        ++consecutivePoseFailures_;
        if (consecutivePoseFailures_ >= kPoseFailuresBeforeRebuild)
        {
            clearCachedMuzzle();
            nextPathRefresh_ = now + kFailedPathRefreshInterval;
        }
        holdLastValidOrInvalidate(now, identityChanged);
        return;
    }

    pose.worldForward /= directionLength;
    pose.pathUsed = cachedPath_;
    pose.valid = true;

    const bool pathChanged = previousPath != cachedPath_;
    if (!filterPose(pose, now, identityChanged || pathChanged))
    {
        ++consecutivePoseFailures_;
        holdLastValidOrInvalidate(now, identityChanged);
        return;
    }

    consecutivePoseFailures_ = 0;

    if (pose.valid) {
        const CameraManagerSnapshot cameraSnapshot = cameraManagerTest.snapshot();
        if (!cameraSnapshot || !cameraSnapshot->valid)
        {
            publish(std::move(pose));
            return;
        }

        pose.cameraVersion = cameraSnapshot->version;
        const glm::vec3 endWorld = pose.worldOrigin + pose.worldForward * kFireportProjectionDistanceM;
        pose.screenStartOk = CameraManager::worldToScreen(*cameraSnapshot, pose.worldOrigin, pose.screenStart, espGlobals::gameRes.x, espGlobals::gameRes.y);
        pose.screenEndOk = CameraManager::worldToScreen(*cameraSnapshot, endWorld, pose.screenEnd, espGlobals::gameRes.x, espGlobals::gameRes.y);

        const float sw = espGlobals::gameRes.x;
        const float sh = espGlobals::gameRes.y;
        pose.aimRefOk = pose.screenEndOk && onScreen(pose.screenEnd, sw, sh);
    }

    publish(std::move(pose));
}

FireportPose FireportTracker::snapshot() const noexcept
{
    return *getSnapshot();
}
