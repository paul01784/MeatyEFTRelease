#pragma once

#include <glm/glm.hpp>
#include <atomic>
#include <chrono>
#include <memory>

#include "../../Unity/Transform.h"

inline constexpr float kFireportProjectionDistanceM = 100.0f;
inline constexpr std::chrono::milliseconds kFireportAimMaxAge{ 750 };
inline constexpr std::chrono::milliseconds kFireportVisualMaxAge{ 1500 };

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
    std::uint64_t cameraVersion = 0;
    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point publishedAt{};
};

using FireportPoseSnapshot = std::shared_ptr<const FireportPose>;

[[nodiscard]] bool IsFireportPoseFresh(
    const FireportPose& pose,
    std::chrono::milliseconds maximumAge,
    std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now()) noexcept;

class FireportTracker
{
public:
    FireportTracker();

    void clear() noexcept;
    void update(uint64_t localPlayer, uint64_t handsController);
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
    void holdLastValidOrInvalidate(
        std::chrono::steady_clock::time_point now,
        bool forceInvalid = false);
    bool filterPose(
        FireportPose& pose,
        std::chrono::steady_clock::time_point now,
        bool resetFilter) const;

    std::atomic<FireportPoseSnapshot> publishedPose_;
    std::atomic<std::uint64_t> publishedVersion_{ 0 };
    std::unique_ptr<UnityTransform> muzzleTransform_;
    uint64_t cachedLocalPlayer_ = 0;
    uint64_t cachedHandsController_ = 0;
    const char* cachedPath_ = nullptr;
    bool cachedPathIsFallback_ = false;
    std::chrono::steady_clock::time_point nextPathRefresh_{};
    std::uint32_t consecutivePoseFailures_ = 0;
};

extern FireportTracker g_fireport;
