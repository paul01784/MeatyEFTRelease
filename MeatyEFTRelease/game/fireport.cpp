#include "headers/fireport.h"

#include "headers/camera.h"
#include "headers/sdk.h"
#include "headers/transform.h"
#include "headers/utils.h"
#include "../app/globals.h"
#include "../memory/Memory.h"

#include <cmath>

extern Memory mem;

FireportTracker g_fireport;

namespace {

bool isGoodVec3(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool resolveUnityTransformNative(uint64_t transformObj, uint64_t& outNative)
{
    outNative = 0;
    if (!Utils::valid_pointer(transformObj))
        return false;

    const uint64_t native = mem.Read<uint64_t>(transformObj + 0x10);
    if (Utils::valid_pointer(native)) {
        outNative = native;
        return true;
    }

    const uint64_t hierarchy = mem.Read<uint64_t>(transformObj + 0x70);
    if (Utils::valid_pointer(hierarchy)) {
        outNative = transformObj;
        return true;
    }
    return false;
}

bool readMuzzleFromNative(uint64_t transformInternal, glm::vec3& outPos, glm::vec3& outDir)
{
    UnityTransform transform(transformInternal, false);
    if (!transform.IsValid())
        return false;

    transform.UpdatePosition();
    outPos = transform.Position();

    const glm::quat rot = transform.GetRotation();
    outDir = UnityTransformExtensions::Down(rot);
    const float len = glm::length(outDir);
    if (len < 1e-4f)
        return false;
    outDir /= len;
    return isGoodVec3(outPos) && isGoodVec3(outDir);
}

bool readMuzzleFromBifacial(uint64_t bifacial, glm::vec3& outPos, glm::vec3& outDir)
{
    if (!Utils::valid_pointer(bifacial))
        return false;

    uint64_t internal = 0;
    if (mem.ReadChain(bifacial, {sdk::BifacialTransform::Original, 0x10}, internal, false) &&
        Utils::valid_pointer(internal)) {
        if (readMuzzleFromNative(internal, outPos, outDir))
            return true;
    }

    const uint64_t original = mem.Read<uint64_t>(bifacial + sdk::BifacialTransform::Original);
    if (!Utils::valid_pointer(original))
        return false;

    uint64_t native = 0;
    if (!resolveUnityTransformNative(original, native))
        return false;
    return readMuzzleFromNative(native, outPos, outDir);
}

bool readMuzzleFromManaged(uint64_t managed, glm::vec3& outPos, glm::vec3& outDir)
{
    uint64_t native = 0;
    if (!resolveUnityTransformNative(managed, native))
        return false;
    return readMuzzleFromNative(native, outPos, outDir);
}

bool onScreen(const glm::vec2& p, float w, float h)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0.f && p.x <= w && p.y >= 0.f && p.y <= h;
}

} // namespace

void FireportTracker::update(uint64_t localPlayer)
{
    FireportPose pose{};

    if (!Utils::valid_pointer(localPlayer) || !camera.cameraPointersReady()) {
        std::unique_lock lock(mutex_);
        pose_ = pose;
        return;
    }

    const uint64_t handsCtrl = mem.Read<uint64_t>(localPlayer + sdk::Player::_handsController);
    if (!Utils::valid_pointer(handsCtrl)) {
        std::unique_lock lock(mutex_);
        pose_ = pose;
        return;
    }

    auto tryBifacial = [&](uint64_t bifacial, const char* path) -> bool {
        glm::vec3 pos{};
        glm::vec3 dir{};
        if (!readMuzzleFromBifacial(bifacial, pos, dir))
            return false;
        pose.worldOrigin = pos;
        pose.worldForward = dir;
        pose.pathUsed = path;
        pose.valid = true;
        return true;
    };

    if (!tryBifacial(mem.Read<uint64_t>(handsCtrl + sdk::FirearmController::Fireport), "firearm.fireport")) {
        const uint64_t playerBones = mem.Read<uint64_t>(localPlayer + sdk::Player::PlayerBones);
        if (!Utils::valid_pointer(playerBones) ||
            !tryBifacial(mem.Read<uint64_t>(playerBones + sdk::PlayerBones::Fireport), "player_bones.fireport")) {
            const uint64_t firearms = mem.Read<uint64_t>(handsCtrl + sdk::FirearmController::Firearms);
            if (!Utils::valid_pointer(firearms) ||
                !tryBifacial(mem.Read<uint64_t>(firearms + sdk::Firearms::Fireport), "firearms._fireport")) {
                const uint64_t pwa = mem.Read<uint64_t>(localPlayer + sdk::Player::ProceduralWeaponAnimation);
                if (Utils::valid_pointer(pwa)) {
                    const uint64_t handsSpring =
                        mem.Read<uint64_t>(pwa + sdk::ProceduralWeaponAnimation::HandsContainer);
                    if (Utils::valid_pointer(handsSpring)) {
                        const uint64_t fireportManaged =
                            mem.Read<uint64_t>(handsSpring + sdk::PlayerSpring::Fireport);
                        glm::vec3 pos{};
                        glm::vec3 dir{};
                        if (Utils::valid_pointer(fireportManaged) && readMuzzleFromManaged(fireportManaged, pos, dir)) {
                            pose.worldOrigin = pos;
                            pose.worldForward = dir;
                            pose.pathUsed = "pwa.spring.fireport";
                            pose.valid = true;
                        }
                    }
                }
            }
        }
    }

    if (pose.valid) {
        const float lenM = (std::max)(10.f, aimGlobals::fireportLineLengthM);
        const glm::vec3 endWorld = pose.worldOrigin + pose.worldForward * lenM;
        pose.screenStartOk = Utils::Camera::world_to_screen(pose.worldOrigin, &pose.screenStart);
        pose.screenEndOk = Utils::Camera::world_to_screen(endWorld, &pose.screenEnd);

        const float sw = espGlobals::gameRes.x;
        const float sh = espGlobals::gameRes.y;
        pose.aimRefOk = pose.screenEndOk && onScreen(pose.screenEnd, sw, sh);
    }

    std::unique_lock lock(mutex_);
    pose_ = pose;
}

FireportPose FireportTracker::snapshot() const
{
    std::shared_lock lock(mutex_);
    return pose_;
}
