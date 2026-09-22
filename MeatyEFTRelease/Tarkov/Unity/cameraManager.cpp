#include "cameraManager.h"

#include "UnityOffsets.h"
#include "../SDK/EftOffsets.h"
#include "../../Core/Utilities.h"
#include "../../UI/debug.h"
#include "../../memory/Memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

namespace
{
    // Bound compatibility list walks and throttle them independently of frame reads.
    constexpr int kMaxCameraCount = 1024;
    constexpr int kMaxOpticCount = 16;
    constexpr int kMaxScopeCount = 16;
    constexpr int kMaxModeCount = 32;
    constexpr auto kUpdateInterval = std::chrono::milliseconds(4);
    constexpr auto kSightRefreshInterval = std::chrono::milliseconds(50);
    constexpr auto kInitializeRetryInterval = std::chrono::milliseconds(500);
    constexpr auto kViewMatrixRetryInterval = std::chrono::milliseconds(250);
    constexpr auto kManagedCameraRetryInterval = std::chrono::seconds(3);
    constexpr auto kCameraRefreshInterval = std::chrono::seconds(3);
    constexpr std::uint8_t kOpticMatrixFailureLimit = 3;
    constexpr auto kCameraReadFailureGrace = std::chrono::milliseconds(750);
    constexpr auto kAllCamerasRetryInterval = std::chrono::seconds(3);
    
    constexpr auto kSnapshotMaxAge = std::chrono::seconds(2);

    constexpr std::uint64_t kManagedListItems = 0x10;
    constexpr std::uint64_t kManagedListCount = 0x18;
    constexpr std::uint64_t kManagedArrayCount = 0x18;
    constexpr std::uint64_t kManagedArrayData = 0x20;

    constexpr std::uint64_t kComponentObjectClass = 0x20;
    constexpr std::uint64_t kObjectClassMonoBehaviour = 0x10;
    constexpr std::uint64_t kUnityObjectCachedPointer = 0x10;
    constexpr std::uint64_t kComponentArraySize = 0x10;
    constexpr std::uint64_t kComponentArrayEntryComponent = 0x8;
    constexpr std::uint64_t kComponentArrayEntryStride = 0x10;

    constexpr std::uint64_t kEftCameraManagerOpticManager = 0x10;
    constexpr std::uint64_t kOpticCameraManagerCurrentSight = 0x78;
    constexpr std::uint64_t kOpticSightScopeTransform = 0x40;
    constexpr std::uint64_t kSightBoneTransform = 0x18;
    constexpr int kMaxGameObjectComponents = 128;

    [[nodiscard]] bool validPointer(std::uint64_t value)
    {
        return Utils::valid_pointer(value);
    }

    template <typename T>
    [[nodiscard]] bool readUncached(std::uint64_t address, T& value)
    {
        return mem.TryRead(address, value, DmaCacheMode::Uncached);
    }

    [[nodiscard]] bool readPointer(std::uint64_t address, std::uint64_t& value)
    {
        return readUncached(address, value) && validPointer(value);
    }

    [[nodiscard]] bool containsInsensitive(std::string_view value, std::string_view needle)
    {
        if (needle.empty())
            return true;

        return std::search(
            value.begin(),
            value.end(),
            needle.begin(),
            needle.end(),
            [](char left, char right)
            {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }) != value.end();
    }

    [[nodiscard]] bool classNameMatches(std::string_view value, std::string_view expected)
    {
        if (value.size() < expected.size())
            return false;

        const std::string_view suffix = value.substr(value.size() - expected.size());

        if (suffix.size() != expected.size() || !containsInsensitive(suffix, expected))
        {
            return false;
        }

        return value.size() == expected.size() || value[value.size() - expected.size() - 1] == '.';
    }

    [[nodiscard]] bool sameUnityObject(std::uint64_t left, std::uint64_t right)
    {
        if (!validPointer(left) || !validPointer(right))
            return false;

        if (left == right)
            return true;

        std::uint64_t leftNative = 0;
        std::uint64_t rightNative = 0;

        return readPointer(left + kUnityObjectCachedPointer, leftNative) && readPointer(right + kUnityObjectCachedPointer, rightNative) && leftNative == rightNative;
    }

    [[nodiscard]] float matrixDifference(const glm::mat4& left, const glm::mat4& right)
    {
        float difference = 0.0f;
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
                difference = (std::max)(difference, std::fabs(left[column][row] - right[column][row]));
        }
        return difference;
    }

}

CameraManager cameraManagerTest;

CameraManager::CameraManager()
    : m_viewMatrixOffset(static_cast<std::uint32_t>(UnityOffsets::Camera_ViewMatrixOffset)),
      m_snapshot(std::make_shared<const CameraManagerState>())
{
}

CameraManagerSnapshot CameraManager::snapshot() const noexcept
{
    CameraManagerSnapshot value = m_snapshot.load(std::memory_order_acquire);
    const auto now = std::chrono::steady_clock::now();
    if (value && (!value->valid ||
        (value->publishedAt != std::chrono::steady_clock::time_point{} &&
            now >= value->publishedAt && (now - value->publishedAt) <= kSnapshotMaxAge)))
        return value;

    static const CameraManagerSnapshot empty = std::make_shared<const CameraManagerState>();
    return empty;
}

void CameraManager::reset()
{
    m_allCamerasGlobal = 0;
    m_allCamerasFpsCamera = 0;
    m_allCamerasOpticCamera = 0;
    m_allCamerasFpsViewMatrixAddress = 0;
    m_allCamerasOpticViewMatrixAddress = 0;
    m_unityPlayerBase = 0;
    m_fpsCamera = 0;
    m_opticCamera = 0;
    m_fpsViewMatrixAddress = 0;
    m_opticViewMatrixAddress = 0;
    m_eftCameraManager = 0;
    m_opticCameraManager = 0;

    m_viewMatrixOffset = static_cast<std::uint32_t>(UnityOffsets::Camera_ViewMatrixOffset);
    m_cachedSights.clear();
    m_cachedCurrentOpticSight = 0;
    m_cachedCurrentScopeTransform = 0;
    m_cachedActiveSightIndex = -1;
    m_lastUpdate = {};
    m_lastInitializeAttempt = {};
    m_lastManagedCameraResolve = {};
    m_lastCameraRefresh = {};
    m_lastSightRefresh = {};
    m_lastViewMatrixResolve = {};
    m_lastAllCamerasResolve = {};
    m_usedAllCamerasOffset = false;
    m_fpsFromAllCameras = false;
    m_opticFromAllCameras = false;
    m_lastAds = false;
    m_opticMatrixReadFailures = 0;
    m_cameraReadFailureSince = {};
    m_busyReadSkips = 0;
    if (m_cameraHealthFailureActive)
        mem.ReportDmaHealthSuccess(DmaHealthSource::CameraChain);
    m_cameraHealthFailureActive = false;

    CameraManagerState empty{};
    publish(std::move(empty));
}

bool CameraManager::readCameraList(std::uint64_t globalAddress, CameraListView& list) const
{
    list = {};

    std::uint64_t listObject = 0;
    if (!readPointer(globalAddress, listObject))
        return false;

    if (!readPointer(listObject, list.items))
        return false;

    int dmaRadarCount = 0;
    std::uint64_t meatyCount = 0;

    const bool readDmaCount = readUncached(listObject + 0x8, dmaRadarCount);
    const bool readMeatyCount = readUncached(listObject + 0x10, meatyCount);

    const bool dmaCountValid = readDmaCount && dmaRadarCount > 0 && dmaRadarCount <= kMaxCameraCount;
    const bool meatyCountValid = readMeatyCount && meatyCount > 0 && meatyCount <= kMaxCameraCount;

    if (!dmaCountValid && !meatyCountValid)
        return false;

    list.count = dmaCountValid ? dmaRadarCount : 0;
    if (meatyCountValid)
        list.count = (std::max)(list.count, static_cast<int>(meatyCount));

    return list.count > 0;
}

std::uint64_t CameraManager::resolveAllCamerasGlobal()
{
    if (!validPointer(m_unityPlayerBase))
        m_unityPlayerBase = mem.GetTarkovPointerSnapshot().unityPlayerBase;

    if (!validPointer(m_unityPlayerBase))
        return 0;

    const std::uint64_t hardcoded = m_unityPlayerBase + UnityOffsets::AllCamera;
    CameraListView list{};

    if (validPointer(hardcoded) && readCameraList(hardcoded, list))
    {
        m_usedAllCamerasOffset = true;
        return hardcoded;
    }

    return 0;
}

std::string CameraManager::readCameraName(std::uint64_t camera) const
{
    if (!validPointer(camera))
        return {};

    constexpr std::array<std::uint64_t, 2> gameObjectOffsets = {
        UnityOffsets::GameObject_ObjectClassOffset,
        UnityOffsets::GameObject_ComponentsOffset
    };

    for (const std::uint64_t offset : gameObjectOffsets)
    {
        std::uint64_t gameObject = 0;
        if (!readPointer(camera + offset, gameObject))
            continue;

        std::uint64_t namePointer = 0;
        if (!readPointer(gameObject + UnityOffsets::GameObject_NameOffset, namePointer))
        {
            continue;
        }

        std::string name = mem.readString(namePointer, 64, DmaCacheMode::Uncached);

        if (!name.empty())
            return name;
    }

    return {};
}

std::string CameraManager::readManagedComponentName(std::uint64_t objectClass) const
{
    if (!validPointer(objectClass))
        return {};

    std::uint64_t nativeClass = 0;
    std::uint64_t namePointer = 0;

    if (!readPointer(objectClass, nativeClass) || !readPointer(nativeClass + 0x10, namePointer))
    {
        return {};
    }

    return mem.readString(namePointer, 96, DmaCacheMode::Uncached);
}

std::uint64_t CameraManager::resolveManagedComponent(std::uint64_t camera, std::string_view className) const
{
    if (!validPointer(camera) || className.empty())
        return 0;

    constexpr std::array<std::uint64_t, 2> gameObjectOffsets = {
        UnityOffsets::GameObject_ObjectClassOffset,
        UnityOffsets::GameObject_ComponentsOffset
    };

    for (const std::uint64_t gameObjectOffset : gameObjectOffsets)
    {
        std::uint64_t gameObject = 0;
        if (!readPointer(camera + gameObjectOffset, gameObject))
            continue;

        std::uint64_t components = 0;
        std::uint64_t componentCount = 0;

        if (!readPointer(gameObject + UnityOffsets::GameObject_ComponentsOffset, components) ||
            !readUncached(gameObject + UnityOffsets::GameObject_ComponentsOffset + kComponentArraySize, componentCount) ||
            componentCount == 0 || componentCount > kMaxGameObjectComponents)
        {
            continue;
        }

        for (std::uint64_t index = 0; index < componentCount; ++index)
        {
            std::uint64_t component = 0;
            std::uint64_t objectClass = 0;

            if (!readPointer(components + index * kComponentArrayEntryStride + kComponentArrayEntryComponent, component) ||
                !readPointer(component + kComponentObjectClass, objectClass))
            {
                continue;
            }

            const std::string candidate = readManagedComponentName(objectClass);

            if (!classNameMatches(candidate, className))
                continue;

            std::uint64_t managedComponent = 0;
            if (readPointer(objectClass + kObjectClassMonoBehaviour, managedComponent))
            {
                return managedComponent;
            }
        }
    }

    return 0;
}

bool CameraManager::resolveOpticCameraManager()
{
    std::uint64_t manager = m_opticCameraManager;
    if (!validPointer(manager))
        manager = resolveManagedComponent(m_opticCamera, "OpticCameraManager");
    if (!validPointer(manager))
    {
        if (!validPointer(m_eftCameraManager))
            m_eftCameraManager = resolveManagedComponent(m_fpsCamera, "CameraManager");
        if (validPointer(m_eftCameraManager))
            (void)readPointer(m_eftCameraManager + kEftCameraManagerOpticManager, manager);
    }
    if (!validPointer(manager))
        return false;

    m_opticCameraManager = manager;

    std::uint64_t cameraReference = 0;
    std::uint64_t nativeCamera = 0;
    if (readPointer(manager + sdk::OpticCameraManager::Camera, cameraReference) &&
        classNameMatches(readManagedComponentName(cameraReference), "Camera") &&
        readPointer(cameraReference + kUnityObjectCachedPointer, nativeCamera) && nativeCamera != m_fpsCamera)
    {
        const std::uint64_t matrixAddress = resolveViewMatrixAddress(nativeCamera, true);
        if (validPointer(matrixAddress))
        {
            m_opticCamera = nativeCamera;
            m_opticViewMatrixAddress = matrixAddress;
            m_opticFromAllCameras = false;
        }
    }
    return true;
}

bool CameraManager::readCurrentOpticSight(std::uint64_t& currentOpticSight, std::uint64_t& currentScopeTransform) const
{
    currentOpticSight = 0;
    currentScopeTransform = 0;

    if (!validPointer(m_opticCameraManager))
        return false;

    if (!readUncached(m_opticCameraManager + kOpticCameraManagerCurrentSight, currentOpticSight))
    {
        return false;
    }

    if (!validPointer(currentOpticSight))
    {
        currentOpticSight = 0;
        return true;
    }

    readPointer(currentOpticSight + kOpticSightScopeTransform, currentScopeTransform);

    return true;
}

std::uint64_t CameraManager::resolveViewMatrixAddress(std::uint64_t camera, bool preferDirect) const
{
    if (!validPointer(camera))
        return 0;

    glm::highp_mat4 view{};
    const bool viewValid = readUncached(camera + UnityOffsets::Camera_WorldToCameraMatrixOffset, view) && matrixLooksValid(view);
    const auto candidateLooksValid = [&](const glm::highp_mat4& candidate)
    {
        return viewProjectionLooksValid(candidate) && (!viewValid || viewProjectionMatchesView(candidate, view));
    };

    glm::highp_mat4 matrix{};
    const std::uint64_t directAddress = camera + m_viewMatrixOffset;
    if (preferDirect && readUncached(directAddress, matrix) && candidateLooksValid(matrix))
        return directAddress;

    std::uint64_t gameObject = 0;
    std::uint64_t componentData = 0;
    std::uint64_t matrixBase = 0;
    if (!readPointer(camera + UnityOffsets::GameObject_ComponentsOffset, gameObject) ||
        !readPointer(gameObject + UnityOffsets::GameObject_ComponentsOffset, componentData) ||
        !readPointer(componentData + 0x18, matrixBase))
        return 0;

    const std::uint64_t matrixAddress = matrixBase + m_viewMatrixOffset;
    if (readUncached(matrixAddress, matrix) && candidateLooksValid(matrix))
        return matrixAddress;

    return 0;
}

bool CameraManager::resolveCamerasFromAllCameras(std::uint64_t& fps, std::uint64_t& optic)
{
    fps = m_allCamerasFpsCamera;
    optic = m_allCamerasOpticCamera;

    const auto now = std::chrono::steady_clock::now();
    if (m_lastAllCamerasResolve != std::chrono::steady_clock::time_point{} &&
        (now - m_lastAllCamerasResolve) < kAllCamerasRetryInterval)
        return validPointer(fps) || validPointer(optic);
    m_lastAllCamerasResolve = now;

    if (!validPointer(m_allCamerasGlobal))
    {
        m_usedAllCamerasOffset = false;
        m_allCamerasGlobal = resolveAllCamerasGlobal();
    }

    if (!validPointer(m_allCamerasGlobal))
        return validPointer(fps) || validPointer(optic);

    CameraListView list{};
    if (!readCameraList(m_allCamerasGlobal, list))
    {
        m_allCamerasGlobal = 0;
        return validPointer(fps) || validPointer(optic);
    }

    const int count = list.count;
    std::uint64_t foundFps = 0;
    std::uint64_t foundOptic = 0;
    std::uint64_t firstOptic = 0;

    for (int index = 0; index < count; ++index)
    {
        std::uint64_t camera = 0;
        if (!readPointer(list.items + static_cast<std::uint64_t>(index) * sizeof(std::uint64_t), camera))
        {
            continue;
        }

        const std::string name = readCameraName(camera);
        if (name.empty())
            continue;

        const bool isFps = containsInsensitive(name, "FPS") && containsInsensitive(name, "Camera");
        const bool isOptic = (containsInsensitive(name, "Optic") || containsInsensitive(name, "BaseOptic")) && containsInsensitive(name, "Camera");

        if (!foundFps && isFps && validPointer(resolveViewMatrixAddress(camera)))
            foundFps = camera;
        if (isOptic)
        {
            if (!firstOptic)
                firstOptic = camera;
            if (!foundOptic && validPointer(resolveViewMatrixAddress(camera, true)))
                foundOptic = camera;
        }

        if (foundFps && foundOptic)
            break;
    }

    if (validPointer(foundFps))
    {
        m_allCamerasFpsCamera = foundFps;
        m_allCamerasFpsViewMatrixAddress = resolveViewMatrixAddress(foundFps);
    }
    if (!foundOptic)
        foundOptic = firstOptic;
    if (validPointer(foundOptic))
    {
        m_allCamerasOpticCamera = foundOptic;
        m_allCamerasOpticViewMatrixAddress = resolveViewMatrixAddress(foundOptic, true);
    }

    fps = m_allCamerasFpsCamera;
    optic = m_allCamerasOpticCamera;
    return validPointer(fps) || validPointer(optic);
}

bool CameraManager::resolveCameras()
{
    std::uint64_t fps = 0;
    std::uint64_t optic = 0;
    if (!resolveCamerasFromAllCameras(fps, optic))
        return false;

    const std::uint64_t fpsMatrix = resolveViewMatrixAddress(fps);
    if (!validPointer(fps) || !validPointer(fpsMatrix))
        return false;

    if (fps != m_fpsCamera || optic != m_opticCamera)
    {
        m_eftCameraManager = 0;
        m_opticCameraManager = 0;
        m_lastManagedCameraResolve = {};
    }

    m_fpsCamera = fps;
    m_opticCamera = optic;
    m_fpsViewMatrixAddress = fpsMatrix;
    m_opticViewMatrixAddress = resolveViewMatrixAddress(optic, true);
    m_allCamerasFpsViewMatrixAddress = m_fpsViewMatrixAddress;
    m_allCamerasOpticViewMatrixAddress = m_opticViewMatrixAddress;
    m_fpsFromAllCameras = true;
    m_opticFromAllCameras = validPointer(optic);

    return true;
}

bool CameraManager::initialize()
{
    if (!mem.IsDmaOperational())
        return false;

    const bool resolved = resolveCameras();

    if (resolved)
    {
        // Resolve the sight component without changing the AllCameras pointers.
        m_lastManagedCameraResolve = std::chrono::steady_clock::now();
        m_lastCameraRefresh = m_lastManagedCameraResolve;
        (void)resolveOpticCameraManager();

        static auto lastBootstrapLog =
            std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (lastBootstrapLog == std::chrono::steady_clock::time_point{} ||
            (now - lastBootstrapLog) >= std::chrono::seconds(5))
        {
            lastBootstrapLog = now;
            LOGS.logInfo(
                "[CAMERA MANAGER] Resolved camera bootstrap.");
        }
    }

    return resolved;
}

float CameraManager::readSelectedZoom(std::uint64_t sightTemplate, std::uint64_t selectedModes, int selectedScope, int& selectedMode, float& minimumZoom, float& maximumZoom) const
{
    selectedMode = 0;
    minimumZoom = 100.0f;
    maximumZoom = 0.0f;

    if (!validPointer(sightTemplate) ||
        selectedScope < 0 || selectedScope >= kMaxScopeCount)
    {
        return -1.0f;
    }

    std::uint64_t zoomArrays = 0;
    if (!readPointer(sightTemplate + sdk::SightInterface::Zooms, zoomArrays))
    {
        return -1.0f;
    }

    int scopeCount = 0;
    if (!readUncached(zoomArrays + kManagedArrayCount, scopeCount) ||
        scopeCount <= 0 || scopeCount > kMaxScopeCount ||
        selectedScope >= scopeCount)
    {
        return -1.0f;
    }

    if (validPointer(selectedModes))
    {
        int modeEntryCount = 0;
        if (readUncached(selectedModes + kManagedArrayCount, modeEntryCount) && modeEntryCount > 0 && modeEntryCount <= kMaxScopeCount && selectedScope < modeEntryCount)
        {
            readUncached(selectedModes + kManagedArrayData + static_cast<std::uint64_t>(selectedScope) * sizeof(int), selectedMode);
        }
    }

    std::uint64_t zoomArray = 0;
    if (!readPointer(zoomArrays + kManagedArrayData + static_cast<std::uint64_t>(selectedScope) * sizeof(std::uint64_t), zoomArray))
    {
        return -1.0f;
    }

    int zoomCount = 0;
    if (!readUncached(zoomArray + kManagedArrayCount, zoomCount) || zoomCount <= 0 || zoomCount > kMaxModeCount)
    {
        return -1.0f;
    }

    if (selectedMode < 0 || selectedMode >= zoomCount)
        selectedMode = 0;

    // The template bounds let the live scope FOV be converted without treating
    // unrelated sights' raw values as magnification.
    for (int scope = 0; scope < scopeCount; ++scope)
    {
        std::uint64_t modes = 0;
        int modeCount = 0;
        if (!readPointer(zoomArrays + kManagedArrayData + static_cast<std::uint64_t>(scope) * sizeof(std::uint64_t), modes) ||
            !readUncached(modes + kManagedArrayCount, modeCount) || modeCount <= 0 || modeCount > kMaxModeCount)
            continue;

        for (int mode = 0; mode < modeCount; ++mode)
        {
            float modeZoom = 0.0f;
            if (readUncached(modes + kManagedArrayData + static_cast<std::uint64_t>(mode) * sizeof(float), modeZoom) &&
                std::isfinite(modeZoom) && modeZoom > 0.0f && modeZoom < 100.0f)
            {
                minimumZoom = (std::min)(minimumZoom, modeZoom);
                maximumZoom = (std::max)(maximumZoom, modeZoom);
            }
        }
    }

    float zoom = -1.0f;
    if (!readUncached(zoomArray + kManagedArrayData + static_cast<std::uint64_t>(selectedMode) * sizeof(float), zoom))
    {
        return -1.0f;
    }

    return std::isfinite(zoom) && zoom >= 0.0f && zoom < 100.0f
        ? zoom
        : -1.0f;
}

bool CameraManager::readSight(std::uint64_t sightBone, int listIndex, std::uint64_t currentOpticSight, std::uint64_t currentScopeTransform, CameraSightState& result) const
{
    result = {};
    result.sightBone = sightBone;
    result.opticsListIndex = listIndex;

    if (!validPointer(sightBone) ||
        !readPointer(sightBone + sdk::SightNBone::Mod, result.sightComponent))
    {
        return false;
    }

    readPointer(sightBone + kSightBoneTransform, result.sightBoneTransform);

    readUncached(result.sightComponent + sdk::SightComponent::SelectedScope, result.selectedScope);
    readUncached(result.sightComponent + sdk::SightComponent::ScopeZoomValue, result.scopeZoomValue);

    std::uint64_t selectedModes = 0;
    float minimumZoom = 100.0f;
    float maximumZoom = 0.0f;
    readPointer(result.sightComponent + sdk::SightComponent::_template, result.sightTemplate);
    readPointer(result.sightComponent + sdk::SightComponent::ScopeSelectedModes, selectedModes);
    result.resolvedZoom = readSelectedZoom(result.sightTemplate, selectedModes, result.selectedScope, result.selectedMode, minimumZoom, maximumZoom);

    // ScopeZoomValue is the live optic FOV for variable scopes, not the
    // magnification. The observed 1x/8x positions are 30/3.75 degrees.
    // Keep the template value for fixed scopes or an implausible live value.
    if (minimumZoom > 0.0f && maximumZoom > minimumZoom + 0.01f &&
        std::isfinite(result.scopeZoomValue) && result.scopeZoomValue > 0.0f)
    {
        constexpr float kScopeBaseFov = 30.0f;
        const float liveZoom = kScopeBaseFov / result.scopeZoomValue;
        if (std::isfinite(liveZoom) && liveZoom >= minimumZoom - 0.05f && liveZoom <= maximumZoom + 0.05f)
            result.resolvedZoom = (std::clamp)(liveZoom, minimumZoom, maximumZoom);
    }

    result.valid =
        std::isfinite(result.resolvedZoom) &&
        result.resolvedZoom >= 0.0f &&
        result.resolvedZoom < 100.0f;

    result.magnified = result.valid && result.resolvedZoom > 1.0f;

    result.selectedByCurrentOptic =
        sameUnityObject(
            currentScopeTransform,
            result.sightBoneTransform) ||
        (currentOpticSight != 0 &&
            (currentOpticSight == result.sightBone ||
                currentOpticSight == result.sightComponent));

    return true;
}

std::vector<CameraSightState> CameraManager::readSights(std::uint64_t localPwa, std::uint64_t currentOpticSight, std::uint64_t currentScopeTransform) const
{
    std::vector<CameraSightState> result;

    if (!validPointer(localPwa))
        return result;

    std::uint64_t opticsList = 0;
    if (!readPointer(localPwa + sdk::ProceduralWeaponAnimation::_optics, opticsList))
    {
        return result;
    }

    int count = 0;
    std::uint64_t items = 0;

    if (!readUncached(opticsList + kManagedListCount, count) || count <= 0 || count > kMaxOpticCount ||
        !readPointer(opticsList + kManagedListItems, items))
    {
        return result;
    }

    const std::vector<std::uint64_t> sightBones = mem.ReadVector<std::uint64_t>(items + kManagedArrayData, static_cast<std::size_t>(count), DmaCacheMode::Uncached);

    if (sightBones.size() != static_cast<std::size_t>(count))
        return result;

    result.reserve(sightBones.size());

    for (int index = 0; index < count; ++index)
    {
        CameraSightState sight{};
        if (readSight(sightBones[static_cast<std::size_t>(index)], index, currentOpticSight, currentScopeTransform, sight))
        {
            result.push_back(sight);
        }
    }

    return result;
}

int CameraManager::selectActiveSight(const std::vector<CameraSightState>& sights)
{
    for (std::size_t index = 0; index < sights.size(); ++index)
    {
        if (sights[index].valid && sights[index].selectedByCurrentOptic)
            return static_cast<int>(index);
    }

    for (std::size_t index = 0; index < sights.size(); ++index)
    {
        if (sights[index].valid)
            return static_cast<int>(index);
    }

    return -1;
}

bool CameraManager::update(std::uint64_t localPwa, std::uint64_t currentOpticSight, std::uint64_t localPlayer)
{
    if (!mem.IsDmaOperational())
        return false;

    const auto now = std::chrono::steady_clock::now();

    if (m_lastUpdate != std::chrono::steady_clock::time_point{} &&
        (now - m_lastUpdate) < kUpdateInterval)
    {
        const CameraManagerSnapshot current = snapshot();
        return current && current->valid;
    }

    bool isAds = false;

    if (validPointer(localPwa))
    {
        readUncached(localPwa + sdk::ProceduralWeaponAnimation::_isAiming, isAds);
    }

    return updateFrame(localPwa, isAds, currentOpticSight, localPlayer, now);
}

bool CameraManager::updateWithAds(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight, std::uint64_t localPlayer)
{
    if (!mem.IsDmaOperational())
        return false;

    const std::uint64_t recoveryEpoch = mem.GetDmaRecoveryEpoch();
    if (recoveryEpoch != m_observedDmaRecoveryEpoch)
    {
        m_observedDmaRecoveryEpoch = recoveryEpoch;
        reset();
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_lastUpdate != std::chrono::steady_clock::time_point{} &&
        (now - m_lastUpdate) < kUpdateInterval)
    {
        const CameraManagerSnapshot current = snapshot();
        return current && current->valid;
    }

    return updateFrame(localPwa, isAds, currentOpticSight, localPlayer, now);
}

bool CameraManager::updateFrame(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight, std::uint64_t localPlayer, std::chrono::steady_clock::time_point now)
{
    (void)localPlayer;
    m_lastUpdate = now;

    if (isAds && !m_lastAds && !validPointer(m_allCamerasOpticCamera))
        m_lastAllCamerasResolve = {};

    if (!validPointer(m_allCamerasFpsCamera) || !validPointer(m_allCamerasOpticCamera))
    {
        std::uint64_t cachedFps = 0;
        std::uint64_t cachedOptic = 0;
        (void)resolveCamerasFromAllCameras(cachedFps, cachedOptic);
    }

    if (validPointer(m_fpsCamera) && (m_lastCameraRefresh == std::chrono::steady_clock::time_point{} || (now - m_lastCameraRefresh) >= kCameraRefreshInterval))
    {
        m_lastCameraRefresh = now;
        (void)resolveCameras();
    }

    if (!validPointer(m_fpsCamera) || !validPointer(m_fpsViewMatrixAddress))
    {
        if (m_lastInitializeAttempt != std::chrono::steady_clock::time_point{} &&
            (now - m_lastInitializeAttempt) < kInitializeRetryInterval)
        {
            const CameraManagerSnapshot current = snapshot();
            return current && current->valid;
        }

        m_lastInitializeAttempt = now;

        if (!initialize())
        {
            m_cameraHealthFailureActive = true;
            mem.ReportDmaHealthFailure(
                DmaHealthSource::CameraChain,
                m_allCamerasGlobal ^ (m_unityPlayerBase << 1));
            return false;
        }
    }

    // EFT's managed camera objects may become available after the FPS camera.
    if (isAds &&
        (!validPointer(m_opticCameraManager) ||
            !validPointer(m_opticCamera) ||
            !validPointer(m_opticViewMatrixAddress)) &&
        (m_lastManagedCameraResolve ==
                std::chrono::steady_clock::time_point{} ||
            (now - m_lastManagedCameraResolve) >=
                kManagedCameraRetryInterval))
    {
        m_lastManagedCameraResolve = now;

        const bool hadOpticPath = validPointer(m_opticCameraManager) && validPointer(m_opticCamera);

        if (resolveOpticCameraManager() && !hadOpticPath)
        {
            LOGS.logInfo(
                "[CAMERA MANAGER] Resolved optic sight manager through Unity components.");
        }
    }

    if (isAds && !validPointer(m_opticCamera) && validPointer(m_allCamerasOpticCamera))
    {
        m_opticCamera = m_allCamerasOpticCamera;
        m_opticViewMatrixAddress = m_allCamerasOpticViewMatrixAddress;
        m_opticFromAllCameras = true;
    }

    const bool suppliedSightChanged = validPointer(currentOpticSight) && currentOpticSight != m_cachedCurrentOpticSight;
    const bool refreshSights = isAds && (!m_lastAds || suppliedSightChanged ||
            m_lastSightRefresh == std::chrono::steady_clock::time_point{} ||
            (now - m_lastSightRefresh) >= kSightRefreshInterval);

    if (!isAds)
    {
        if (m_lastAds || !m_cachedSights.empty())
        {
            m_cachedSights.clear();
            m_cachedCurrentOpticSight = 0;
            m_cachedCurrentScopeTransform = 0;
            m_cachedActiveSightIndex = -1;
            m_lastSightRefresh = {};
        }
    }
    else if (refreshSights)
    {
        std::uint64_t resolvedCurrentOpticSight = currentOpticSight;
        std::uint64_t currentScopeTransform = 0;

        if (validPointer(m_opticCameraManager))
            (void)resolveOpticCameraManager();

        if (validPointer(resolvedCurrentOpticSight))
        {
            readPointer(resolvedCurrentOpticSight + kOpticSightScopeTransform, currentScopeTransform);
        }
        else
        {
            if (!validPointer(m_opticCameraManager) && (m_lastManagedCameraResolve ==
                        std::chrono::steady_clock::time_point{} ||
                    (now - m_lastManagedCameraResolve) >=
                        kManagedCameraRetryInterval))
            {
                m_lastManagedCameraResolve = now;

                if (resolveOpticCameraManager())
                {
                    LOGS.logInfo(
                        "[CAMERA MANAGER] Resolved OpticCameraManager through "
                        "Unity components; stacked-sight matching enabled.");
                }
            }

            (void)readCurrentOpticSight(resolvedCurrentOpticSight, currentScopeTransform);
        }

        m_cachedSights = readSights(localPwa, resolvedCurrentOpticSight, currentScopeTransform);
        m_cachedCurrentOpticSight = resolvedCurrentOpticSight;
        m_cachedCurrentScopeTransform = currentScopeTransform;
        m_cachedActiveSightIndex = selectActiveSight(m_cachedSights);
        m_lastSightRefresh = now;
    }

    m_lastAds = isAds;

    CameraManagerState state{};
    state.ads = isAds;
    state.eftCameraManager = m_eftCameraManager;
    state.opticCameraManager = m_opticCameraManager;
    state.currentOpticSight = m_cachedCurrentOpticSight;
    state.currentOpticScopeTransform = m_cachedCurrentScopeTransform;
    state.fpsCamera = m_fpsCamera;
    state.opticCamera = m_opticCamera;
    state.allCamerasGlobal = m_allCamerasGlobal;
    state.allCamerasFpsCamera = m_allCamerasFpsCamera;
    state.allCamerasOpticCamera = m_allCamerasOpticCamera;
    state.viewMatrixOffset = m_viewMatrixOffset;
    state.usedAllCamerasOffset = m_usedAllCamerasOffset;
    state.fpsFromAllCameras = m_fpsFromAllCameras;
    state.opticFromAllCameras = m_opticFromAllCameras;
    state.busyReadSkips = m_busyReadSkips;
    state.sights = m_cachedSights;
    state.activeSightVectorIndex = m_cachedActiveSightIndex;

    if (state.activeSightVectorIndex >= 0)
    {
        const CameraSightState& sight = state.sights[static_cast<std::size_t>(state.activeSightVectorIndex)];
        state.magnification = sight.resolvedZoom;
        state.stackedSightResolved = state.sights.size() > 1 && sight.selectedByCurrentOptic;
    }

    state.scoped = state.ads && state.activeSightVectorIndex >= 0 && state.magnification > 1.0f;
    // An active ADS sight uses the optic camera from 1x upward.
    const bool wantOptic = state.ads && state.activeSightVectorIndex >= 0 && state.magnification >= 1.0f;
    state.opticRequested = wantOptic;
    if (!wantOptic)
        m_opticMatrixReadFailures = 0;

    if (wantOptic && !validPointer(m_opticViewMatrixAddress) &&
        (m_lastViewMatrixResolve == std::chrono::steady_clock::time_point{} ||
            (now - m_lastViewMatrixResolve) >= kViewMatrixRetryInterval))
    {
        m_lastViewMatrixResolve = now;
        m_opticViewMatrixAddress = resolveViewMatrixAddress(m_opticCamera, true);
        if (m_opticFromAllCameras)
            m_allCamerasOpticViewMatrixAddress = m_opticViewMatrixAddress;
    }


    CameraMatrixSample fpsSample{};
    CameraMatrixSample opticSample{};
    const CameraManagerSnapshot previous = snapshot();

    bool fpsSampleBusy = false;
    bool opticSampleBusy = false;
    std::uint64_t sampledFpsCamera = m_fpsCamera;
    std::uint64_t sampledOpticCamera = m_opticCamera;
    if (!fpsSample.valid)
        (void)readCameraSample(
            m_fpsCamera, m_fpsViewMatrixAddress, fpsSample, &fpsSampleBusy);
    if (wantOptic && !opticSample.valid)
        (void)readCameraSample(
            m_opticCamera, m_opticViewMatrixAddress, opticSample, &opticSampleBusy);

    if (!fpsSample.valid && !fpsSampleBusy && m_cameraReadFailureSince == std::chrono::steady_clock::time_point{})
        m_cameraReadFailureSince = now;

    const bool fpsFallbackDue = !fpsSample.valid &&
        ((!previous || !previous->valid) ||
            (!fpsSampleBusy && m_cameraReadFailureSince != std::chrono::steady_clock::time_point{} &&
                (now - m_cameraReadFailureSince) >= kCameraReadFailureGrace));

    // A brief failed read retains the last good camera matrix.
    if (fpsFallbackDue && validPointer(m_allCamerasFpsCamera) && m_allCamerasFpsCamera != m_fpsCamera)
    {
        CameraMatrixSample fallbackSample{};
        bool fallbackBusy = false;
        if (readCameraSample(m_allCamerasFpsCamera, m_allCamerasFpsViewMatrixAddress, fallbackSample, &fallbackBusy))
        {
            fpsSample = fallbackSample;
            sampledFpsCamera = m_allCamerasFpsCamera;
            state.fpsFromAllCameras = true;
        }
        else if (!fallbackBusy)
        {
            m_allCamerasFpsCamera = 0;
            m_allCamerasFpsViewMatrixAddress = 0;
            m_lastAllCamerasResolve = {};
        }
        fpsSampleBusy = fpsSampleBusy || fallbackBusy;
    }
    if (wantOptic && !opticSample.valid && validPointer(m_allCamerasOpticCamera) && m_allCamerasOpticCamera != m_opticCamera)
    {
        CameraMatrixSample fallbackSample{};
        bool fallbackBusy = false;
        if (readCameraSample(m_allCamerasOpticCamera, m_allCamerasOpticViewMatrixAddress, fallbackSample, &fallbackBusy))
        {
            opticSample = fallbackSample;
            sampledOpticCamera = m_allCamerasOpticCamera;
            state.opticFromAllCameras = true;
        }
        else if (!fallbackBusy)
        {
            m_allCamerasOpticCamera = 0;
            m_allCamerasOpticViewMatrixAddress = 0;
            m_lastAllCamerasResolve = {};
        }
        opticSampleBusy = opticSampleBusy || fallbackBusy;
    }

    state.fpsCamera = sampledFpsCamera;
    state.opticCamera = sampledOpticCamera;

    const bool fpsRead = fpsSample.valid;
    const bool opticRead = wantOptic && opticSample.valid;
    state.fpsMatrixValid = fpsRead;
    state.opticMatrixValid = opticRead;
    if (fpsRead && previous && previous->valid && previous->fpsCamera == sampledFpsCamera)
        state.fpsMatrixDelta = matrixDifference(fpsSample.viewProjection, previous->mainViewProjection);
    if (opticRead && previous && previous->opticRequested && previous->opticCamera == sampledOpticCamera)
        state.opticMatrixDelta = matrixDifference(opticSample.viewProjection, previous->opticViewProjection);
    if (!fpsRead)
    {
        state.cameraSampleFailure = fpsSampleBusy
            ? "DMA busy during camera sample"
            : "main camera matrix read or validation";
    }
    if (wantOptic && !opticRead)
        state.opticCameraFailure = !validPointer(m_opticCamera) ? "optic camera pointer unavailable" :
            (opticSampleBusy ? "DMA busy during optic camera sample" : "optic camera matrix read or validation");

    if (!fpsRead)
    {
        const auto current = snapshot();
        if (fpsSampleBusy)
        {
            ++m_busyReadSkips;
            if (current->valid)
                return true;
            publish(std::move(state));
            return false;
        }

        if (m_cameraReadFailureSince == std::chrono::steady_clock::time_point{})
            m_cameraReadFailureSince = now;

        if ((now - m_cameraReadFailureSince) < kCameraReadFailureGrace)
        {
            if (current->valid)
                return true;
            publish(std::move(state));
            return false;
        }

        m_cameraHealthFailureActive = true;
        mem.ReportDmaHealthFailure(
            DmaHealthSource::CameraChain,
            m_fpsCamera ^ (m_fpsViewMatrixAddress << 1));

        if (m_fpsFromAllCameras)
        {
            m_allCamerasFpsCamera = 0;
            m_allCamerasFpsViewMatrixAddress = 0;
            m_lastAllCamerasResolve = {};
        }

        m_fpsCamera = 0;
        m_fpsViewMatrixAddress = 0;
        m_eftCameraManager = 0;
        m_opticCameraManager = 0;
        m_opticCamera = 0;
        m_opticViewMatrixAddress = 0;
    }
    else
    {
        if (m_cameraHealthFailureActive)
        {
            mem.ReportDmaHealthSuccess(DmaHealthSource::CameraChain);
            m_cameraHealthFailureActive = false;
        }
        m_cameraReadFailureSince = {};
    }
    if (fpsRead && wantOptic && !opticRead && !opticSampleBusy && validPointer(m_opticViewMatrixAddress))
    {
        
        if (m_opticMatrixReadFailures < kOpticMatrixFailureLimit)
            ++m_opticMatrixReadFailures;

        if (m_opticMatrixReadFailures == kOpticMatrixFailureLimit)
        {
            const std::uint64_t refreshedMatrix = resolveViewMatrixAddress(m_opticCamera, true);

            if (validPointer(refreshedMatrix))
            {
                m_opticViewMatrixAddress = refreshedMatrix;
                if (m_opticFromAllCameras)
                    m_allCamerasOpticViewMatrixAddress = refreshedMatrix;
                m_opticMatrixReadFailures = 0;
            }
            else
            {
                m_opticViewMatrixAddress = 0;
                if (m_opticFromAllCameras)
                {
                    m_allCamerasOpticCamera = 0;
                    m_allCamerasOpticViewMatrixAddress = 0;
                    m_lastAllCamerasResolve = {};
                    m_opticCamera = 0;
                    m_opticFromAllCameras = false;
                }
                m_lastManagedCameraResolve = now - kManagedCameraRetryInterval + kViewMatrixRetryInterval;
                m_lastViewMatrixResolve = {};
            }
        }
    }
    else if (fpsRead && opticRead)
    {
        m_opticMatrixReadFailures = 0;
    }

    
    if (fpsRead && opticRead)
    {
        float fov = 0.0f;
        float aspect = 0.0f;
        if (!readUncached(sampledFpsCamera + UnityOffsets::Camera_FOVOffset, fov) || !std::isfinite(fov) || fov <= 1.0f || fov >= 180.0f)
            fov = previous && previous->valid && previous->fpsCamera == sampledFpsCamera ? previous->fpsFov : 0.0f;
        if (!readUncached(sampledFpsCamera + UnityOffsets::Camera_AspectRatioOffset, aspect) || !std::isfinite(aspect) || aspect <= 0.1f || aspect >= 5.0f)
            aspect = previous && previous->valid && previous->fpsCamera == sampledFpsCamera ? previous->fpsAspect : 0.0f;

        state.fpsFov = fov;
        state.fpsAspect = aspect;
        if (fov > 1.0f && fov < 180.0f && aspect > 0.1f && aspect < 5.0f)
        {
            const float halfFovRadians = fov * (3.14159265358979323846f / 360.0f);
            const float scaleY = 2.0f * std::tan(halfFovRadians);
            const float scaleX = scaleY / aspect;
            if (std::isfinite(scaleX) && std::isfinite(scaleY) && scaleX > 0.0f && scaleY > 0.0f)
            {
                state.opticScaleX = scaleX;
                state.opticScaleY = scaleY;
                state.usingOptic = true;
            }
        }
    }

    state.activeKind = state.usingOptic ? ManagedCameraKind::Optic : ManagedCameraKind::Fps;
    state.activeCamera = state.usingOptic ? sampledOpticCamera : sampledFpsCamera;
    state.activeViewMatrixAddress = state.usingOptic
        ? (state.opticFromAllCameras ? m_allCamerasOpticViewMatrixAddress : m_opticViewMatrixAddress)
        : (state.fpsFromAllCameras ? m_allCamerasFpsViewMatrixAddress : m_fpsViewMatrixAddress);
    state.valid = fpsRead;
    if (fpsRead)
    {
        if (fpsSample.viewValid)
        {
            const glm::mat4 cameraToWorld = glm::inverse(fpsSample.view);
            const glm::vec3 cameraPosition(cameraToWorld[3]);
            constexpr float kMaximumCameraCoordinate = 1000000.0f;

            state.fpsCameraWorldPositionValid =
                std::isfinite(cameraPosition.x) &&
                std::isfinite(cameraPosition.y) &&
                std::isfinite(cameraPosition.z) &&
                std::fabs(cameraPosition.x) <= kMaximumCameraCoordinate &&
                std::fabs(cameraPosition.y) <= kMaximumCameraCoordinate &&
                std::fabs(cameraPosition.z) <= kMaximumCameraCoordinate;

            if (state.fpsCameraWorldPositionValid)
                state.fpsCameraWorldPosition = cameraPosition;
        }

        state.mainViewProjection = fpsSample.viewProjection;
        state.opticViewProjection = opticSample.viewProjection;
        state.rawViewMatrix = state.usingOptic ? opticSample.viewProjection : fpsSample.viewProjection;
        state.viewMatrix = state.rawViewMatrix;
        if (state.usingOptic)
        {
            for (int column = 0; column < 4; ++column)
            {
                state.viewMatrix[column][0] *= state.opticScaleX;
                state.viewMatrix[column][1] *= state.opticScaleY;
            }
        }
    }
    else
    {
        const auto current = snapshot();
        if (current->valid && current->ads == isAds)
            return true;
    }

    state.fpsCamera = sampledFpsCamera;
    state.opticCamera = sampledOpticCamera;
    state.allCamerasGlobal = m_allCamerasGlobal;
    state.allCamerasFpsCamera = m_allCamerasFpsCamera;
    state.allCamerasOpticCamera = m_allCamerasOpticCamera;
    state.viewMatrixOffset = m_viewMatrixOffset;
    state.usedAllCamerasOffset = m_usedAllCamerasOffset;
    state.busyReadSkips = m_busyReadSkips;

    publish(std::move(state));
    return snapshot()->valid;
}

void CameraManager::publish(CameraManagerState&& state)
{
    state.version = m_version.fetch_add(1, std::memory_order_relaxed) + 1;
    state.publishedAt = std::chrono::steady_clock::now();

    m_snapshot.store(std::make_shared<const CameraManagerState>(std::move(state)), std::memory_order_release);
}

bool CameraManager::matrixLooksValid(const glm::highp_mat4& matrix)
{
    constexpr float kMaxElementValue = 100000.0f;
    constexpr float kNonZeroEpsilon = 0.00001f;
    std::array<bool, 4> populatedColumns{};
    std::array<bool, 4> populatedRows{};

    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            const float value = matrix[column][row];

            if (!std::isfinite(value) || std::fabs(value) > kMaxElementValue)
                return false;

            if (std::fabs(value) > kNonZeroEpsilon)
            {
                populatedColumns[static_cast<std::size_t>(column)] = true;
                populatedRows[static_cast<std::size_t>(row)] = true;
            }
        }
    }

    return std::all_of(populatedColumns.begin(), populatedColumns.end(), [](bool populated) { return populated; }) &&
        std::all_of(populatedRows.begin(), populatedRows.end(), [](bool populated) { return populated; });
}

bool CameraManager::viewProjectionLooksValid(const glm::highp_mat4& matrix)
{
    if (!matrixLooksValid(matrix))
        return false;

    // A perspective view-projection matrix derives clip X, clip Y and clip W
    // from three independent world-space directions. This rejects identity,
    // dense singular data and affine transforms while allowing off-centre or
    // oblique projections.
    const glm::vec3 clipX{matrix[0][0], matrix[1][0], matrix[2][0]};
    const glm::vec3 clipY{matrix[0][1], matrix[1][1], matrix[2][1]};
    const glm::vec3 clipW{matrix[0][3], matrix[1][3], matrix[2][3]};
    const float clipXLength = glm::length(clipX);
    const float clipYLength = glm::length(clipY);
    const float clipWLength = glm::length(clipW);
    constexpr float kMinimumBasisLength = 0.00001f;
    if (!std::isfinite(clipXLength) || !std::isfinite(clipYLength) || !std::isfinite(clipWLength) ||
        clipXLength <= kMinimumBasisLength || clipYLength <= kMinimumBasisLength || clipWLength <= kMinimumBasisLength)
        return false;

    const float normalizedVolume = std::fabs(glm::dot(glm::cross(clipX, clipY), clipW)) / (clipXLength * clipYLength * clipWLength);
    constexpr float kMinimumNormalizedVolume = 0.0001f;
    return std::isfinite(normalizedVolume) && normalizedVolume > kMinimumNormalizedVolume;
}

bool CameraManager::viewProjectionMatchesView(const glm::highp_mat4& viewProjection, const glm::highp_mat4& view)
{
    const glm::vec3 clipW{viewProjection[0][3], viewProjection[1][3], viewProjection[2][3]};
    const glm::vec3 viewZ{view[0][2], view[1][2], view[2][2]};
    const float clipWLength = glm::length(clipW);
    const float viewZLength = glm::length(viewZ);
    if (!std::isfinite(clipWLength) || !std::isfinite(viewZLength) || clipWLength <= 0.00001f || viewZLength <= 0.00001f)
        return false;

    // Perspective projection maps camera-space Z into clip W. The sign varies
    // with graphics convention, so compare the rows without assuming one.
    const float alignment = std::fabs(glm::dot(clipW, viewZ) / (clipWLength * viewZLength));
    constexpr float kMinimumForwardAlignment = 0.995f;
    return std::isfinite(alignment) && alignment >= kMinimumForwardAlignment;
}

bool CameraManager::readCameraSample(std::uint64_t nativeCamera, std::uint64_t matrixAddress, CameraMatrixSample& sample, bool* busy)
{
    sample = {};
    if (busy)
        *busy = false;
    if (!validPointer(nativeCamera) || !validPointer(matrixAddress))
        return false;

    glm::mat4 matrix{};
    Memory::ScatterReadRequest request{
        matrixAddress, &matrix, sizeof(matrix)
    };
    DWORD bytesRead[1]{};
    const auto result = mem.TryReadScatter(&request, 1, DmaCacheMode::Uncached, "Camera matrix", bytesRead);
    if (result == Memory::TryScatterReadResult::Busy)
    {
        if (busy)
            *busy = true;
        return false;
    }
    if (result != Memory::TryScatterReadResult::Success || bytesRead[0] != sizeof(matrix) || !viewProjectionLooksValid(matrix))
        return false;

    glm::mat4 view{};
    if (readUncached(nativeCamera + UnityOffsets::Camera_WorldToCameraMatrixOffset, view) && matrixLooksValid(view))
    {
        if (!viewProjectionMatchesView(matrix, view))
            return false;
        sample.view = view;
        sample.viewValid = true;
    }
    // This matrix is already the camera's combined transform in the established path.
    sample.viewProjection = matrix;
    sample.valid = true;
    return true;
}

bool CameraManager::worldToScreen(const CameraManagerState& state, const glm::vec3& world, glm::vec2& screen, float viewportWidth, float viewportHeight, float edgeBufferPixels)
{
    if (!state.valid ||
        !std::isfinite(viewportWidth) ||
        !std::isfinite(viewportHeight) ||
        viewportWidth <= 0.0f || viewportHeight <= 0.0f ||
        !std::isfinite(edgeBufferPixels) || edgeBufferPixels < 0.0f ||
        !std::isfinite(world.x) || !std::isfinite(world.y) || !std::isfinite(world.z))
    {
        return false;
    }

    const auto project = [&](const glm::mat4& matrix)
    {
        const glm::vec4 clip = matrix * glm::vec4(world, 1.0f);
        if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w) || clip.w <= 0.098f)
            return false;

        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        screen = {viewportWidth * 0.5f * (1.0f + ndc.x), viewportHeight * 0.5f * (1.0f - ndc.y)};
        return std::isfinite(screen.x) && std::isfinite(screen.y) &&
            screen.x >= -edgeBufferPixels && screen.x <= viewportWidth + edgeBufferPixels &&
            screen.y >= -edgeBufferPixels && screen.y <= viewportHeight + edgeBufferPixels;
    };

    return project(state.viewMatrix);
}

bool CameraManager::worldSegmentToScreen(const CameraManagerState& state, const glm::vec3& worldStart, const glm::vec3& worldEnd, float viewportWidth, float viewportHeight, std::vector<CameraScreenSegment>& segments)
{
    segments.clear();
    if (!state.valid || !std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
        return false;

    const auto project = [&](const glm::mat4& matrix)
    {
        const glm::vec4 first = matrix * glm::vec4(worldStart, 1.0f);
        const glm::vec4 last = matrix * glm::vec4(worldEnd, 1.0f);
        for (const glm::vec4& point : {first, last})
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) || !std::isfinite(point.w))
                return false;
        }

        float low = 0.0f;
        float high = 1.0f;
        const auto clipPlane = [&](float from, float to)
        {
            if (from < 0.0f && to < 0.0f)
                return false;
            if (from < 0.0f)
                low = (std::max)(low, from / (from - to));
            else if (to < 0.0f)
                high = (std::min)(high, from / (from - to));
            return low <= high;
        };

        const float xTolerance = 2.0f * kScreenEdgeBufferPixels / viewportWidth;
        const float yTolerance = 2.0f * kScreenEdgeBufferPixels / viewportHeight;
        if (!clipPlane(first.w - 0.098f, last.w - 0.098f) ||
            !clipPlane((1.0f + xTolerance) * first.w + first.x, (1.0f + xTolerance) * last.w + last.x) ||
            !clipPlane((1.0f + xTolerance) * first.w - first.x, (1.0f + xTolerance) * last.w - last.x) ||
            !clipPlane((1.0f + yTolerance) * first.w + first.y, (1.0f + yTolerance) * last.w + last.y) ||
            !clipPlane((1.0f + yTolerance) * first.w - first.y, (1.0f + yTolerance) * last.w - last.y))
            return false;

        const glm::vec4 clippedFirst = glm::mix(first, last, low);
        const glm::vec4 clippedLast = glm::mix(first, last, high);
        if (clippedFirst.w <= 0.0f || clippedLast.w <= 0.0f)
            return false;

        const glm::vec2 firstNdc = glm::vec2(clippedFirst) / clippedFirst.w;
        const glm::vec2 lastNdc = glm::vec2(clippedLast) / clippedLast.w;
        CameraScreenSegment segment{};
        segment.start = {viewportWidth * 0.5f * (1.0f + firstNdc.x), viewportHeight * 0.5f * (1.0f - firstNdc.y)};
        segment.end = {viewportWidth * 0.5f * (1.0f + lastNdc.x), viewportHeight * 0.5f * (1.0f - lastNdc.y)};
        if (!std::isfinite(segment.start.x) || !std::isfinite(segment.start.y) || !std::isfinite(segment.end.x) || !std::isfinite(segment.end.y))
            return false;
        segments.push_back(segment);
        return true;
    };

    return project(state.viewMatrix);
}
