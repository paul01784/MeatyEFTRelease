#pragma once

#include <glm/glm.hpp>
#include <atomic>
#include <chrono>
#include <memory>

#include "../../Unity/Transform.h"

inline constexpr float kFireportProjectionDistanceM = 100.0f;

struct FireportPose
{
    bool valid = false;
    bool screenStartOk = false;
    bool screenEndOk = false;
    bool aimRefOk = false;
    glm::vec3 worldOrigin{};
    glm::vec3 worldForward{};
    glm::vec2 screenStart{};
    glm::vec2 screenEnd{};
    const char* pathUsed = nullptr;
};

using FireportPoseSnapshot = std::shared_ptr<const FireportPose>;

class FireportTracker
{
public:
    FireportTracker();

    void clear() noexcept;
    void update(uint64_t localPlayer);
    [[nodiscard]] FireportPoseSnapshot
        getSnapshot() const noexcept;
    [[nodiscard]] FireportPose snapshot() const noexcept;

private:
    void publish(FireportPose pose);
    bool refreshMuzzleTransform(
        uint64_t localPlayer,
        uint64_t handsController,
        std::chrono::steady_clock::time_point now);
    void clearCachedMuzzle() noexcept;

    std::atomic<FireportPoseSnapshot> publishedPose_;
    std::unique_ptr<UnityTransform> muzzleTransform_;
    uint64_t cachedLocalPlayer_ = 0;
    uint64_t cachedHandsController_ = 0;
    const char* cachedPath_ = nullptr;
    bool cachedPathIsFallback_ = false;
    std::chrono::steady_clock::time_point nextPathRefresh_{};
};

extern FireportTracker g_fireport;
