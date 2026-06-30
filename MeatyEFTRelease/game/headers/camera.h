#pragma once

#include <cstdint>
#include <glm/glm.hpp>

struct CameraArray
{
    uint64_t cameras;
    uint64_t minCount;
    uint64_t curCount;
    uint64_t maxCount;
};

class Camera
{
public:

    struct MatrixActivityDebugState
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

    const MatrixActivityDebugState& getMatrixActivityDebug() const;

    void getCameraPtrs();
    void getMatrixPtrs();
    void cameraTask();
    void clearCache();

    bool checkIfOpticMatrix();


    bool cameraPointersReady() const;

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

    struct FrameData
    {
        glm::highp_mat4 fpsRaw{};
        glm::highp_mat4 opticRaw{};

        float fov = 0.0f;
        float aspect = 0.0f;

        bool queuedFps = false;
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

    bool readFrameData(FrameData& out);
    void applyFpsFrame(const FrameData& frame);
    bool applyOpticFrame(const FrameData& frame);

    bool updateOpticMatrixActivity(const FrameData& frame);
    void resetOpticActivity();

    static bool matrixLooksValid(const glm::highp_mat4& m);
    static float matrixDiff(const glm::highp_mat4& a, const glm::highp_mat4& b);

    static bool validFov(float value);
    static bool validAspect(float value);
};

extern Camera camera;