#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

struct CameraArray
{
    uint64_t cameras;
    uint64_t minCount;
    uint64_t curCount;
    uint64_t maxCount;
};

struct CameraMatrixActivityDebugState
{
    bool localScoped = false;

    bool fpsMatrixValid = false;
    bool opticMatrixValid = false;

    bool opticMatrixChanged = false;
    bool opticMatrixActive = false;
    bool usingOpticMatrix = false;

    int activityTick = 0;
    int noChangeSamples = 0;

    float opticMatrixDiff = 0.0f;
};

struct CameraProjectionState
{
    glm::highp_mat4 viewMatrix{};
    glm::highp_mat4 fpsViewMatrix{};
    glm::highp_mat4 opticViewMatrix{};
    glm::highp_mat4 fpsRawMatrix{};
    glm::highp_mat4 opticRawMatrix{};

    float gameFOV = 0.0f;
    float gameAspect = 0.0f;

    std::uint64_t fpsCamera = 0;
    std::uint64_t fpsMatrixAddress = 0;
    std::uint64_t opticCamera = 0;
    std::uint64_t opticMatrixAddress = 0;
    std::uint64_t cameraEntity = 0;
    std::uint64_t opticCameraMatrix = 0;

    bool valid = false;
    bool scoped = false;
    bool usingOptic = false;
    bool fpsPointersReady = false;
    bool opticPointersReady = false;

    CameraMatrixActivityDebugState matrixDebug{};

    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point publishedAt{};
    double averageIntervalMs = 0.0;
};

using CameraProjectionSnapshot =
    std::shared_ptr<const CameraProjectionState>;

class Camera
{
public:
	Camera();

    using MatrixActivityDebugState = CameraMatrixActivityDebugState;

    [[nodiscard]] MatrixActivityDebugState
        getMatrixActivityDebug() const noexcept;

    void getCameraPtrs();
    void getMatrixPtrs();
    void cameraTask();
    void clearCache();

    [[nodiscard]] CameraProjectionSnapshot
        getProjectionSnapshot() const noexcept;
    [[nodiscard]] std::uint64_t getBusyReadSkips() const noexcept;

    bool checkIfOpticMatrix();


    bool cameraPointersReady() const;
    bool opticPointersReady() const;

    static bool initedCamera;

    static uint64_t fpsCamera;
    static uint64_t fpsMatrixAddr;

    static uint64_t opticCamera;
    static uint64_t opticMatrixAddr;

    static float gameFOV;
    static float gameAspect;

    // TRUE = use optic matrix.
    // FALSE = use FPS matrix.
    static bool localmpCamera;

    static uint64_t cameraEntity;
    static uint64_t opticCameraMatrix;

    static glm::highp_mat4 g_viewMatrix;
    static glm::highp_mat4 g_viewMatrixOptic;
    static glm::highp_mat4 g_viewMatrixRAW;
    static glm::highp_mat4 g_viewMatrixOpticRAW;

    static uint64_t closestPlayer;
    static float closestPlayerDist;

private:

    enum class FrameReadStatus : std::uint8_t
    {
        Success,
        Busy,
        Failed
    };

    struct FrameData
    {
        glm::highp_mat4 fpsRaw{};
        glm::highp_mat4 opticRaw{};

        float fov = 0.0f;
        float aspect = 0.0f;

        bool lensRead = false;
        bool queuedOptic = false;

        bool fpsMatrixValid = false;
        bool opticMatrixValid = false;
    };

private:

    static constexpr int kMaxCameraCount = 512;

    // Check optic activity every # cameraTask calls.
    static constexpr int kOpticActivityCheckInterval = 3;

    // Number of failed #-tick samples before optic is treated inactive.
    static constexpr int kOpticInactiveSampleLimit = 10;

    static constexpr float kOpticMatrixChangeThreshold = 0.0005f;

private:

    MatrixActivityDebugState m_matrixDebug{};

    int m_opticActivityTick = 0;
    int m_opticNoChangeSamples = 0;

    bool m_hasLastOpticMatrix = false;
    bool m_opticMatrixActive = false;

    glm::highp_mat4 m_lastOpticRaw{};

private:

    void clearCameraPointerCacheOnly();
    bool refreshCameraPointersStrict();

    uint64_t resolveMatrixAddress(uint64_t cameraPtr) const;

    FrameReadStatus readFrameData(FrameData& out, bool readLens);
    void applyFpsFrame(const FrameData& frame);
    bool applyOpticFrame(const FrameData& frame);

    bool updateOpticMatrixActivity(const FrameData& frame);
    void resetOpticActivity();

    static bool matrixLooksValid(const glm::highp_mat4& m);
    static float matrixDiff(const glm::highp_mat4& a, const glm::highp_mat4& b);

    static bool validFov(float value);
    static bool validAspect(float value);

    void publishProjectionSnapshot(bool valid, bool scoped);

    std::atomic<CameraProjectionSnapshot> m_publishedProjection;
    std::atomic<std::uint64_t> m_projectionVersion{ 0 };
    std::atomic<std::uint64_t> m_busyReadSkips{ 0 };
    std::chrono::steady_clock::time_point m_lastProjectionPublish{};
    double m_averageProjectionIntervalMs = 0.0;
};

extern Camera camera;
