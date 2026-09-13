#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "Transform.h"

struct CameraMatrixSample
{
    glm::mat4 view{ 1.0f };
    glm::mat4 projection{ 1.0f };
    glm::mat4 viewProjection{ 1.0f };
    bool valid = false;
};

struct OpticProjectionState
{
    glm::mat4 combined{ 1.0f };
    std::array<glm::vec2, 4> imageQuad{};
    std::vector<glm::vec2> mask;

    // Live diagnostics from the same accepted paired sample. These values are
    // observational only and are not fed back into projection.
    glm::vec2 imageCenterNdc{};
    glm::vec2 lensBoundsCenterNdc{};
    glm::vec2 lensBoundsRadiiNdc{};
    glm::vec3 mainCameraRelativeLens{};
    glm::vec3 mainCameraRelativeHands{};
    std::array<float, 3> fovNodeScaleZ{};
    std::uint32_t fovNodeCount = 0;
    float intendedFovScale = 0.0f;
    float materialScale = 0.0f;
    float materialShift = 0.0f;
    float rootPredictionErrorMm = 0.0f;
    float rootCorrectionMm = 0.0f;
    bool rootTranslationCorrected = false;
    bool rootHistoryReset = false;
    bool rootCorrectionWindowExpired = false;
    std::uint64_t sampleSequence = 0;
    std::chrono::steady_clock::time_point sampledAt{};

    std::uint64_t opticSight = 0;
    std::uint64_t lensRenderer = 0;
    std::uint64_t mesh = 0;
    std::uint64_t material = 0;

    bool valid = false;
};

struct CameraScreenSegment
{
    glm::vec2 start{};
    glm::vec2 end{};
};

class OpticProjectionEngine
{
public:
    [[nodiscard]] static bool readCamera(
        std::uint64_t nativeCamera,
        CameraMatrixSample& out,
        bool* busy = nullptr);

    [[nodiscard]] bool update(
        std::uint64_t localPlayer,
        std::uint64_t opticSight,
        std::uint64_t mainCamera,
        std::uint64_t opticCamera,
        CameraMatrixSample& mainSample,
        CameraMatrixSample& opticSample,
        OpticProjectionState& out,
        bool* busy = nullptr);

    void reset();

    [[nodiscard]] const std::string& failureReason() const noexcept;

    [[nodiscard]] static bool projectPoint(
        const glm::mat4& matrix,
        const glm::vec3& world,
        glm::vec2& ndc,
        float minimumW = 0.001f);

    [[nodiscard]] static bool pointInConvexMask(
        const glm::vec2& point,
        const std::vector<glm::vec2>& mask);

    [[nodiscard]] static bool projectSegment(
        const glm::mat4& mainViewProjection,
        const glm::mat4& opticViewProjection,
        const OpticProjectionState& optic,
        const glm::vec3& worldStart,
        const glm::vec3& worldEnd,
        float viewportWidth,
        float viewportHeight,
        std::vector<CameraScreenSegment>& out,
        bool opticOnly = false);

public:
    struct MeshMap
    {
        glm::vec3 center{};
        glm::vec3 uAxis{};
        glm::vec3 vAxis{};
        bool valid = false;
    };

private:
    struct RootMotionSample
    {
        glm::vec3 cameraLocalPosition{};
        std::chrono::steady_clock::time_point sampledAt{};
    };

    struct RootMotionHistory
    {
        std::array<RootMotionSample, 2> trusted{};
        std::size_t trustedCount = 0;
        std::chrono::steady_clock::time_point correctionStartedAt{};
    };

    struct RootCorrectionDecision
    {
        glm::vec3 measuredCameraLocal{};
        glm::vec3 correctionCameraLocal{};
        std::chrono::steady_clock::time_point sampledAt{};
        float predictionErrorMm = 0.0f;
        bool corrected = false;
        bool resetHistory = false;
        bool correctionWindowExpired = false;
    };

    struct Route
    {
        std::uint64_t localPlayer = 0;
        std::uint64_t opticSight = 0;
        std::uint64_t mainCamera = 0;
        std::uint64_t opticCamera = 0;
        std::uint64_t lensManaged = 0;
        std::uint64_t lens = 0;
        std::uint64_t lensTransform = 0;
        std::uint64_t meshFilter = 0;
        std::uint64_t materialIds = 0;
        std::uint64_t mesh = 0;
        std::uint64_t material = 0;
        std::uint64_t handsNode = 0;
        std::size_t handsNodeIndex = 0;
        std::uint32_t meshId = 0;
        std::uint32_t materialId = 0;

        std::uint64_t scalesAddress = 0;
        std::uint64_t shiftsAddress = 0;
        std::uint64_t shiftDirectionAddress = 0;

        std::vector<std::uint64_t> lensChain;
        std::vector<std::uint64_t> mainCameraChain;
        std::vector<std::uint64_t> opticCameraChain;
        std::unordered_set<std::uint64_t> fovNodes;
        MeshMap meshMap{};
        bool valid = false;
    };

    [[nodiscard]] bool discover(
        std::uint64_t localPlayer,
        std::uint64_t opticSight,
        std::uint64_t mainCamera,
        std::uint64_t opticCamera);

    [[nodiscard]] RootCorrectionDecision evaluateRootCorrection(
        const glm::vec3& measuredCameraLocal,
        std::chrono::steady_clock::time_point sampledAt) const;

    void commitRootCorrection(const RootCorrectionDecision& decision);

    Route m_route{};
    std::string m_failureReason;
    std::uint64_t m_sampleSequence = 0;
    OpticProjectionState m_adjacentCandidate{};
    RootMotionHistory m_rootHistory{};
};
