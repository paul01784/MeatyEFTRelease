#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

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

    std::vector<CameraSightState> sights;

    std::uint64_t fpsCamera = 0;
    std::uint64_t opticCamera = 0;
    std::uint64_t activeCamera = 0;
    std::uint64_t activeViewMatrixAddress = 0;
    std::uint64_t allCamerasGlobal = 0;
    std::uint64_t eftCameraManager = 0;
    std::uint64_t opticCameraManager = 0;
    std::uint64_t currentOpticSight = 0;
    std::uint64_t currentOpticScopeTransform = 0;

    std::uint32_t viewMatrixOffset = 0;
    std::uint32_t fovOffset = 0;
    std::uint32_t aspectOffset = 0;

    int activeSightVectorIndex = -1;

    float fov = 0.0f;
    float aspect = 0.0f;
    float magnification = 1.0f;

    bool valid = false;
    bool ads = false;
    bool scoped = false;
    bool usingOptic = false;
    bool stackedSightResolved = false;
    bool usedAllCamerasOffset = false;

    std::uint64_t busyReadSkips = 0;

    ManagedCameraKind activeKind = ManagedCameraKind::Fps;

    std::uint64_t version = 0;
    std::chrono::steady_clock::time_point publishedAt{};
};

using CameraManagerSnapshot = std::shared_ptr<const CameraManagerState>;

class CameraManager
{
public:
    CameraManager();

    [[nodiscard]] bool initialize();

    [[nodiscard]] bool update(std::uint64_t localPwa, std::uint64_t currentOpticSight = 0);

    [[nodiscard]] bool updateWithAds(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight = 0);

    void reset();

    [[nodiscard]] CameraManagerSnapshot snapshot() const noexcept;

    [[nodiscard]] static bool worldToScreen(const CameraManagerState& state, const glm::vec3& world, glm::vec2& screen, float viewportWidth, float viewportHeight);

private:
    struct CameraListView
    {
        std::uint64_t items = 0;
        int count = 0;
    };

    [[nodiscard]] std::uint64_t resolveAllCamerasGlobal();
    [[nodiscard]] bool resolveCameras();
    [[nodiscard]] bool resolveCamerasFromAllCameras(std::uint64_t& fps, std::uint64_t& optic);

    [[nodiscard]] bool updateFrame(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight, std::chrono::steady_clock::time_point now);

    [[nodiscard]] bool readCameraList(std::uint64_t globalAddress, CameraListView& list) const;
    [[nodiscard]] std::string readCameraName(std::uint64_t camera) const;
    [[nodiscard]] std::uint64_t resolveViewMatrixAddress(std::uint64_t camera) const;
    [[nodiscard]] std::string readManagedComponentName(std::uint64_t objectClass) const;
    [[nodiscard]] std::uint64_t resolveManagedComponent(std::uint64_t camera, std::string_view className) const;
    [[nodiscard]] bool resolveOpticCameraManager();
    [[nodiscard]] bool readCurrentOpticSight(std::uint64_t& currentOpticSight, std::uint64_t& currentScopeTransform) const;

    [[nodiscard]] std::vector<CameraSightState> readSights(std::uint64_t localPwa, std::uint64_t currentOpticSight, std::uint64_t currentScopeTransform) const;
    [[nodiscard]] bool readSight(std::uint64_t sightBone, int listIndex, std::uint64_t currentOpticSight, std::uint64_t currentScopeTransform, CameraSightState& result) const;
    [[nodiscard]] float readSelectedZoom(std::uint64_t sightTemplate, std::uint64_t selectedModes, int selectedScope, int& selectedMode) const;

    [[nodiscard]] static int selectActiveSight(const std::vector<CameraSightState>& sights);
    [[nodiscard]] static bool matrixLooksValid(const glm::highp_mat4& matrix);
    [[nodiscard]] static bool validFov(float value);
    [[nodiscard]] static bool validAspect(float value);

    void publish(CameraManagerState&& state);

private:
    std::uint64_t m_allCamerasGlobal = 0;
    std::uint64_t m_fpsCamera = 0;
    std::uint64_t m_opticCamera = 0;
    std::uint64_t m_fpsViewMatrixAddress = 0;
    std::uint64_t m_opticViewMatrixAddress = 0;
    std::uint64_t m_eftCameraManager = 0;
    std::uint64_t m_opticCameraManager = 0;
    std::uint64_t m_gameAssemblyBase = 0;

    std::uint32_t m_viewMatrixOffset = 0;
    std::uint32_t m_fovOffset = 0;
    std::uint32_t m_aspectOffset = 0;

    float m_lastFov = 0.0f;
    float m_lastAspect = 0.0f;

    std::vector<CameraSightState> m_cachedSights;
    std::uint64_t m_cachedCurrentOpticSight = 0;
    std::uint64_t m_cachedCurrentScopeTransform = 0;
    int m_cachedActiveSightIndex = -1;

    std::chrono::steady_clock::time_point m_lastUpdate{};
    std::chrono::steady_clock::time_point m_lastInitializeAttempt{};
    std::chrono::steady_clock::time_point m_lastManagedCameraResolve{};
    std::chrono::steady_clock::time_point m_lastSightRefresh{};
    std::chrono::steady_clock::time_point m_lastViewMatrixResolve{};
    std::chrono::steady_clock::time_point m_lastLensRefresh{};
    std::chrono::steady_clock::time_point m_lastAllCamerasResolve{};

    bool m_usedAllCamerasOffset = false;
    bool m_lastAds = false;
    bool m_lastUsingOptic = false;

    std::uint8_t m_opticMatrixReadFailures = 0;

    std::uint64_t m_busyReadSkips = 0;

    std::atomic<CameraManagerSnapshot> m_snapshot;
    std::atomic<std::uint64_t> m_version{ 0 };
};

extern CameraManager cameraManagerTest;
