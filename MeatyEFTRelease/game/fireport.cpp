#include "headers/fireport.h"

#include "headers/camera.h"
#include "headers/sdk.h"
#include "headers/transform.h"
#include "headers/utils.h"
#include "../app/globals.h"
#include "../memory/Memory.h"

#include <cmath>
#include <utility>

extern Memory mem;

FireportTracker::FireportTracker()
    : publishedPose_(std::make_shared<const FireportPose>())
{
}

FireportTracker g_fireport;

namespace {

bool isGoodVec3(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::unique_ptr<UnityTransform> makeMuzzleTransform(
    uint64_t transformInternal)
{
    auto transform = std::make_unique<UnityTransform>(
        transformInternal,
        false);

    return transform->IsValid() ? std::move(transform) : nullptr;
}

std::unique_ptr<UnityTransform> makeMuzzleTransformFromBifacial(
    uint64_t bifacial)
{
    if (!Utils::valid_pointer(bifacial))
        return nullptr;

    const uint64_t original = mem.Read<uint64_t>(bifacial + sdk::BifacialTransform::Original);
    if (!Utils::valid_pointer(original))
        return nullptr;

    uint64_t native = 0;
    if (!UnityTransform::TryResolveNative(original, native))
        return nullptr;

    return makeMuzzleTransform(native);
}

std::unique_ptr<UnityTransform> makeMuzzleTransformFromManaged(
    uint64_t managed)
{
    uint64_t native = 0;
    if (!UnityTransform::TryResolveNative(managed, native))
        return nullptr;

    return makeMuzzleTransform(native);
}

bool onScreen(const glm::vec2& p, float w, float h)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0.f && p.x <= w && p.y >= 0.f && p.y <= h;
}

} // namespace

void FireportTracker::publish(FireportPose pose)
{
    publishedPose_.store(
        std::make_shared<const FireportPose>(std::move(pose)),
        std::memory_order_release);
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
}

bool FireportTracker::refreshMuzzleTransform(
    uint64_t localPlayer,
    uint64_t handsController,
    std::chrono::steady_clock::time_point now)
{
    // Weapon/hand pointers normally change only when the held item changes.
    // Retaining the resolved transform avoids rebuilding its parent chain on
    // every 16 ms fireport tick.
    // Only retry a failed lookup. A valid path stays valid until the player
    // pipeline reports a hands-controller change.
    nextPathRefresh_ = now + std::chrono::milliseconds(500);

    auto chooseBifacial = [&](uint64_t bifacial,
        const char* path) -> std::unique_ptr<UnityTransform>
    {
        std::unique_ptr<UnityTransform> transform =
            makeMuzzleTransformFromBifacial(bifacial);

        if (transform)
            cachedPath_ = path;

        return transform;
    };

    std::unique_ptr<UnityTransform> replacement = chooseBifacial(
        mem.Read<uint64_t>(handsController + sdk::FirearmController::Fireport),
        "firearm.fireport");

    if (!replacement)
    {
        const uint64_t playerBones = mem.Read<uint64_t>(
            localPlayer + sdk::Player::PlayerBones);

        if (Utils::valid_pointer(playerBones))
        {
            replacement = chooseBifacial(
                mem.Read<uint64_t>(
                    playerBones + sdk::PlayerBones::Fireport),
                "player_bones.fireport");
        }
    }

    if (!replacement)
    {
        const uint64_t firearms = mem.Read<uint64_t>(
            handsController + sdk::FirearmController::Firearms);

        if (Utils::valid_pointer(firearms))
        {
            replacement = chooseBifacial(
                mem.Read<uint64_t>(firearms + sdk::Firearms::Fireport),
                "firearms._fireport");
        }
    }

    if (!replacement)
    {
        const uint64_t pwa = mem.Read<uint64_t>(
            localPlayer + sdk::Player::ProceduralWeaponAnimation);

        if (Utils::valid_pointer(pwa))
        {
            const uint64_t handsSpring = mem.Read<uint64_t>(
                pwa + sdk::ProceduralWeaponAnimation::HandsContainer);

            if (Utils::valid_pointer(handsSpring))
            {
                replacement = makeMuzzleTransformFromManaged(
                    mem.Read<uint64_t>(
                        handsSpring + sdk::PlayerSpring::Fireport));

                if (replacement)
                    cachedPath_ = "pwa.spring.fireport";
            }
        }
    }

    if (replacement)
    {
        muzzleTransform_ = std::move(replacement);
        cachedLocalPlayer_ = localPlayer;
        cachedHandsController_ = handsController;
        nextPathRefresh_ =
            (std::chrono::steady_clock::time_point::max)();
        return true;
    }

    // A transient failed refresh must not discard a still-valid path. If the
    // hands controller genuinely changed, the old path is not safe to retain.
    if (cachedLocalPlayer_ != localPlayer ||
        cachedHandsController_ != handsController)
    {
        clearCachedMuzzle();
    }

    cachedLocalPlayer_ = localPlayer;
    cachedHandsController_ = handsController;

    return muzzleTransform_ && muzzleTransform_->IsValid();
}

void FireportTracker::update(uint64_t localPlayer)
{
    FireportPose pose{};

    if (!Utils::valid_pointer(localPlayer) || !camera.cameraPointersReady()) {
        clearCachedMuzzle();
        cachedLocalPlayer_ = 0;
        cachedHandsController_ = 0;
        nextPathRefresh_ = {};
        publish(std::move(pose));
        return;
    }

    // The player pipeline already maintains this pointer. Reusing it avoids a
    // separate uncached read every fireport tick and detects weapon switches.
    const uint64_t handsCtrl = mainGame.localPlayerHands;
    if (!Utils::valid_pointer(handsCtrl)) {
        clearCachedMuzzle();
        cachedHandsController_ = 0;
        nextPathRefresh_ = {};
        publish(std::move(pose));
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool pathRefreshDue =
        cachedLocalPlayer_ != localPlayer ||
        cachedHandsController_ != handsCtrl ||
        (!muzzleTransform_ && now >= nextPathRefresh_);

    if (pathRefreshDue)
    {
        refreshMuzzleTransform(localPlayer, handsCtrl, now);
    }

    if (!muzzleTransform_)
    {
        publish(std::move(pose));
        return;
    }

    glm::quat rotation{};
    if (!muzzleTransform_->UpdateWorldPose(
        pose.worldOrigin,
        rotation))
    {
        clearCachedMuzzle();
        nextPathRefresh_ = now + std::chrono::milliseconds(500);
        publish(std::move(pose));
        return;
    }

    pose.worldForward = UnityTransformExtensions::Down(rotation);
    const float directionLength = glm::length(pose.worldForward);

    if (directionLength < 1e-4f ||
        !isGoodVec3(pose.worldOrigin) ||
        !isGoodVec3(pose.worldForward))
    {
        publish(std::move(pose));
        return;
    }

    pose.worldForward /= directionLength;
    pose.pathUsed = cachedPath_;
    pose.valid = true;

    if (pose.valid) {
        const float lenM = (std::max)(10.f, aimGlobals::fireportLineLengthM);
        const glm::vec3 endWorld = pose.worldOrigin + pose.worldForward * lenM;
        const CameraProjectionSnapshot projection = camera.getProjectionSnapshot();

        if (projection)
        {
            pose.screenStartOk = Utils::Camera::world_to_screen(
                pose.worldOrigin,
                &pose.screenStart,
                *projection);
            pose.screenEndOk = Utils::Camera::world_to_screen(
                endWorld,
                &pose.screenEnd,
                *projection);
        }

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
