#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "CameraTypes.h"

enum class ManagedCameraKind : std::uint8_t
{
    Fps,
    Optic
};

struct CameraSightState
{
    std::uint64_t sightBone = 0;
    std::uint64_t sightBoneTransform = 0;
    std::uint64_t sightComponent = 0;
    std::uint64_t sightTemplate = 0;

    int opticsListIndex = -1;
    int selectedScope = -1;
    int selectedMode = -1;

    float scopeZoomValue = 0.0f;
    float resolvedZoom = 1.0f;

    bool valid = false;
    bool magnified = false;
    bool selectedByCurrentOptic = false;
};

struct CameraManagerState
{
    glm::highp_mat4 rawViewMatrix{};
    glm::highp_mat4 viewMatrix{};
    glm::highp_mat4 mainViewProjection{};
    glm::highp_mat4 opticViewProjection{};
    glm::vec3 fpsCameraWorldPosition{};
    std::string cameraSampleFailure;
    std::string opticCameraFailure;

    std::vector<CameraSightState> sights;

    std::uint64_t fpsCamera = 0;
    std::uint64_t opticCamera = 0;
    std::uint64_t activeCamera = 0;
    std::uint64_t activeViewMatrixAddress = 0;
    std::uint64_t allCamerasGlobal = 0;
    std::uint64_t allCamerasFpsCamera = 0;
    std::uint64_t allCamerasOpticCamera = 0;
    std::uint64_t eftCameraManager = 0;
    std::uint64_t opticCameraManager = 0;
    std::uint64_t currentOpticSight = 0;
    std::uint64_t currentOpticScopeTransform = 0;

    std::uint32_t viewMatrixOffset = 0;

    int activeSightVectorIndex = -1;

    float magnification = 1.0f;
    float fpsFov = 0.0f;
    float fpsAspect = 0.0f;
    float opticScaleX = 1.0f;
    float opticScaleY = 1.0f;
    float fpsMatrixDelta = -1.0f;
    float opticMatrixDelta = -1.0f;

    bool valid = false;
    bool fpsMatrixValid = false;
    bool opticMatrixValid = false;
    bool fpsCameraWorldPositionValid = false;
    bool ads = false;
    bool scoped = false;
    bool opticRequested = false;
    bool usingOptic = false;
    bool stackedSightResolved = false;
    bool usedAllCamerasOffset = false;
    bool fpsFromAllCameras = false;
    bool opticFromAllCameras = false;

    std::uint64_t busyReadSkips = 0;

    ManagedCameraKind activeKind = ManagedCameraKind::Fps;

    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point publishedAt{};
};

using CameraManagerSnapshot = std::shared_ptr<const CameraManagerState>;

class CameraManager
{
public:
    static constexpr float kScreenEdgeBufferPixels = 8.0f;

    CameraManager();

    [[nodiscard]] bool initialize();

    [[nodiscard]] bool update(std::uint64_t localPwa, std::uint64_t currentOpticSight = 0, std::uint64_t localPlayer = 0);

    [[nodiscard]] bool updateWithAds(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight = 0, std::uint64_t localPlayer = 0);

    void reset();

    [[nodiscard]] CameraManagerSnapshot snapshot() const noexcept;

    [[nodiscard]] static bool worldToScreen(const CameraManagerState& state, const glm::vec3& world, glm::vec2& screen, float viewportWidth, float viewportHeight, float edgeBufferPixels = kScreenEdgeBufferPixels);

    [[nodiscard]] static bool worldSegmentToScreen(const CameraManagerState& state, const glm::vec3& worldStart, const glm::vec3& worldEnd, float viewportWidth, float viewportHeight, std::vector<CameraScreenSegment>& segments);

private:
    struct CameraListView
    {
        std::uint64_t items = 0;
        int count = 0;
    };

    [[nodiscard]] std::uint64_t resolveAllCamerasGlobal();
    [[nodiscard]] bool resolveCameras();
    [[nodiscard]] bool resolveCamerasFromAllCameras(std::uint64_t& fps, std::uint64_t& optic);

    [[nodiscard]] bool updateFrame(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight, std::uint64_t localPlayer, std::chrono::steady_clock::time_point now);

    [[nodiscard]] bool readCameraList(std::uint64_t globalAddress, CameraListView& list) const;
    [[nodiscard]] std::string readCameraName(std::uint64_t camera) const;
    [[nodiscard]] std::uint64_t resolveViewMatrixAddress(std::uint64_t camera, bool preferDirect = false) const;
    [[nodiscard]] std::string readManagedComponentName(std::uint64_t objectClass) const;
    [[nodiscard]] std::uint64_t resolveManagedComponent(std::uint64_t camera, std::string_view className) const;
    [[nodiscard]] bool resolveOpticCameraManager();
    [[nodiscard]] bool readCurrentOpticSight(std::uint64_t& currentOpticSight, std::uint64_t& currentScopeTransform) const;

    [[nodiscard]] std::vector<CameraSightState> readSights(std::uint64_t localPwa, std::uint64_t currentOpticSight, std::uint64_t currentScopeTransform) const;
    [[nodiscard]] bool readSight(std::uint64_t sightBone, int listIndex, std::uint64_t currentOpticSight, std::uint64_t currentScopeTransform, CameraSightState& result) const;
    [[nodiscard]] float readSelectedZoom(std::uint64_t sightTemplate, std::uint64_t selectedModes, int selectedScope, int& selectedMode, float& minimumZoom, float& maximumZoom) const;

    [[nodiscard]] static int selectActiveSight(const std::vector<CameraSightState>& sights);
    [[nodiscard]] static bool matrixLooksValid(const glm::highp_mat4& matrix);
    [[nodiscard]] static bool viewProjectionLooksValid(const glm::highp_mat4& matrix);
    [[nodiscard]] static bool viewProjectionMatchesView(const glm::highp_mat4& viewProjection, const glm::highp_mat4& view);
    [[nodiscard]] static bool readCameraSample(std::uint64_t nativeCamera, std::uint64_t matrixAddress, CameraMatrixSample& sample, bool* busy = nullptr);

    void publish(CameraManagerState&& state);

private:
    std::uint64_t m_allCamerasGlobal = 0;
    std::uint64_t m_allCamerasFpsCamera = 0;
    std::uint64_t m_allCamerasOpticCamera = 0;
    std::uint64_t m_allCamerasFpsViewMatrixAddress = 0;
    std::uint64_t m_allCamerasOpticViewMatrixAddress = 0;
    std::uint64_t m_unityPlayerBase = 0;
    std::uint64_t m_fpsCamera = 0;
    std::uint64_t m_opticCamera = 0;
    std::uint64_t m_fpsViewMatrixAddress = 0;
    std::uint64_t m_opticViewMatrixAddress = 0;
    std::uint64_t m_eftCameraManager = 0;
    std::uint64_t m_opticCameraManager = 0;

    std::uint32_t m_viewMatrixOffset = 0;

    std::vector<CameraSightState> m_cachedSights;
    std::uint64_t m_cachedCurrentOpticSight = 0;
    std::uint64_t m_cachedCurrentScopeTransform = 0;
    int m_cachedActiveSightIndex = -1;

    std::chrono::steady_clock::time_point m_lastUpdate{};
    std::chrono::steady_clock::time_point m_lastInitializeAttempt{};
    std::chrono::steady_clock::time_point m_lastManagedCameraResolve{};
    std::chrono::steady_clock::time_point m_lastCameraRefresh{};
    std::chrono::steady_clock::time_point m_lastSightRefresh{};
    std::chrono::steady_clock::time_point m_lastViewMatrixResolve{};
    std::chrono::steady_clock::time_point m_lastAllCamerasResolve{};

    bool m_usedAllCamerasOffset = false;
    bool m_fpsFromAllCameras = false;
    bool m_opticFromAllCameras = false;
    bool m_lastAds = false;

    std::uint8_t m_opticMatrixReadFailures = 0;
    int m_opticFrozenFrames = 0;
    int m_opticLiveFrames = 0;
    bool m_opticSuppressed = false;
    std::chrono::steady_clock::time_point m_cameraReadFailureSince{};

    std::uint64_t m_busyReadSkips = 0;
    std::uint64_t m_observedDmaRecoveryEpoch = 0;
    bool m_cameraHealthFailureActive = false;

    std::atomic<CameraManagerSnapshot> m_snapshot;
    std::atomic<std::uint64_t> m_version{ 0 };
};

extern CameraManager cameraManagerTest;
