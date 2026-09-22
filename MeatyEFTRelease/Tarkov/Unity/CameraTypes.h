#pragma once

#include <glm/glm.hpp>

struct CameraMatrixSample
{
    glm::mat4 view{ 0.0f };
    glm::mat4 projection{ 0.0f };
    glm::mat4 viewProjection{ 0.0f };
    bool viewValid = false;
    bool valid = false;
};

struct CameraScreenSegment
{
    glm::vec2 start{};
    glm::vec2 end{};
};
