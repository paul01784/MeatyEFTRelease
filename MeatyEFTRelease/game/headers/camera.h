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
    void getCameraPtrs();
    void getMatrixPtrs();
    void cameraTask();
    void clearCache();

    bool checkIfOpticMatrix();

    static bool initedCamera;

    static uint64_t fpsCamera;
    static uint64_t fpsMatrixAddr;

    static uint64_t opticCamera;
    static uint64_t opticMatrixAddr;

    static float gameFOV;
    static float gameAspect;

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

        bool queuedOptic = false;
        bool fpsMatrixValid = false;
        bool opticMatrixValid = false;
    };

    struct ScopeState
    {
        bool hasSight = false;
        bool wantsOpticMatrix = false;

        uint64_t sightComponent = 0;

        int selectedScope = -1;
        int selectedMode = 0;

        float scopeZoomValue = -1.0f;
        float zoomLevel = -1.0f;
        float selectedZoom = -1.0f;
    };

    class SightComponentView
    {
    public:
        explicit SightComponentView(uint64_t base = 0);

        bool valid() const;
        uint64_t base() const;

        uint64_t sightInterfacePtr() const;
        uint64_t selectedModesPtr() const;

        int selectedScope() const;
        int selectedScopeMode(int selectedScope) const;

        float scopeZoomValue() const;
        float getZoomLevel() const;

    private:
        uint64_t m_base = 0;
    };

private:
    static constexpr int kMaxCameraCount = 512;
    static constexpr int kMaxOpticCount = 16;
    static constexpr int kMaxScopeCount = 16;
    static constexpr int kOpticBadRefreshFrames = 120;

    int m_opticBadFrames = 0;
    uint64_t m_lastSightComponent = 0;

private:
    void refreshCameraPointers();
    bool ensureCameraPointers(bool preferOptic);

    uint64_t resolveMatrixAddress(uint64_t cameraPtr) const;

    bool readFrameData(bool wantOptic, FrameData& out);
    void applyFpsFrame(const FrameData& frame);
    bool applyOpticFrame(const FrameData& frame, const ScopeState& scope);

    ScopeState readScopeState();
    bool shouldUseOpticMatrix(const ScopeState& scope) const;

    void handleOpticBadFrame(bool badFrame);
    void resetOpticBadFrames();

    static bool matrixLooksValid(const glm::highp_mat4& m);
    static bool validFov(float value);
    static bool validAspect(float value);
    static bool validZoom(float value);
    static bool zoomNeedsOptic(float value);
};

extern Camera camera;