#pragma once

#include <glm/glm.hpp>
#include <shared_mutex>

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

class FireportTracker
{
public:
    void update(uint64_t localPlayer);
    FireportPose snapshot() const;

private:
    mutable std::shared_mutex mutex_;
    FireportPose pose_{};
};

extern FireportTracker g_fireport;
