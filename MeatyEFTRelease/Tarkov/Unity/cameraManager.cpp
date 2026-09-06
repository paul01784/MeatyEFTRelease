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
    constexpr auto kLensRefreshInterval = std::chrono::milliseconds(50);
    constexpr auto kInitializeRetryInterval = std::chrono::milliseconds(500);
    constexpr auto kViewMatrixRetryInterval = std::chrono::milliseconds(250);
    constexpr auto kManagedCameraRetryInterval = std::chrono::seconds(3);
    constexpr std::uint8_t kOpticMatrixFailureLimit = 3;
    constexpr auto kAllCamerasRetryInterval = std::chrono::seconds(3);
    constexpr auto kSnapshotMaxAge = std::chrono::milliseconds(250);

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

    constexpr std::uint64_t kCameraManagerBss = 0x5B76058;
    constexpr std::uint64_t kIl2CppClassStaticFields = 0xB8;

    constexpr std::uint64_t kCameraManagerInstance = 0x0;
    constexpr std::uint64_t kEftCameraManagerCamera = 0x70;

    constexpr std::uint64_t kLegacyEftCameraManagerCamera = 0x60;
    constexpr std::uint64_t kEftCameraManagerOpticManager = 0x10;
    constexpr std::uint64_t kOpticCameraManagerCamera = 0x70;
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

    [[nodiscard]] bool isUnityCameraReference(std::uint64_t cameraReference)
    {
        std::uint64_t cameraClass = 0;
        std::uint64_t cameraClassName = 0;
        std::uint64_t nativeCamera = 0;

        if (!readPointer(cameraReference, cameraClass) ||
            !readPointer(cameraClass + 0x10, cameraClassName) ||
            mem.readString(cameraClassName, 32, DmaCacheMode::Uncached) != "Camera" ||
            !readPointer(cameraReference + kUnityObjectCachedPointer, nativeCamera))
        {
            return false;
        }

        return true;
    }

    [[nodiscard]] bool readCameraReferenceFromManager(std::uint64_t manager, std::uint64_t& cameraReference)
    {
        cameraReference = 0;

        for (const std::uint64_t cameraOffset : {
                 kEftCameraManagerCamera,
                 kLegacyEftCameraManagerCamera })
        {
            std::uint64_t candidate = 0;

            if (readPointer(manager + cameraOffset, candidate) &&
                isUnityCameraReference(candidate))
            {
                cameraReference = candidate;
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool isCameraManagerInstance(std::uint64_t instance)
    {
        std::uint64_t cameraReference = 0;
        return readCameraReferenceFromManager(instance, cameraReference);
    }

    [[nodiscard]] bool resolveEftCameraManagerFromBss(std::uint64_t& manager, std::uint64_t& gameAssembly)
    {
        manager = 0;

        if (!validPointer(gameAssembly))
            gameAssembly = mem.GetTarkovPointerSnapshot().gameAssemblyBase;

        std::uint64_t bssValue = 0;

        if (!validPointer(gameAssembly) || !readPointer(gameAssembly + kCameraManagerBss, bssValue))
        {
            return false;
        }

        std::uint64_t staticFields = 0;
        std::uint64_t candidate = 0;

        // BSS -> Il2CppClass -> static_fields -> Instance.  
        if (readPointer(bssValue + kIl2CppClassStaticFields, staticFields))
        {
            if (readPointer(staticFields + kCameraManagerInstance, candidate) && isCameraManagerInstance(candidate))
            {
                manager = candidate;
                return true;
            }
        }

        // BSS -> static_fields -> Instance.  
        if (readPointer(bssValue + kCameraManagerInstance, candidate) && isCameraManagerInstance(candidate))
        {
            manager = candidate;
            return true;
        }

        // BSS -> Instance, for a direct singleton global
        if (isCameraManagerInstance(bssValue))
        {
            manager = bssValue;
            return true;
        }

        return false;
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

}

CameraManager cameraManagerTest;

CameraManager::CameraManager()
    : m_viewMatrixOffset(static_cast<std::uint32_t>(UnityOffsets::Camera_ViewMatrixOffset)),
      m_fovOffset(static_cast<std::uint32_t>(UnityOffsets::Camera_FOVOffset)),
      m_aspectOffset(static_cast<std::uint32_t>(UnityOffsets::Camera_AspectRatioOffset)),
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
    m_fpsCamera = 0;
    m_opticCamera = 0;
    m_fpsViewMatrixAddress = 0;
    m_opticViewMatrixAddress = 0;
    m_eftCameraManager = 0;
    m_opticCameraManager = 0;
    m_gameAssemblyBase = 0;

    m_viewMatrixOffset = static_cast<std::uint32_t>(UnityOffsets::Camera_ViewMatrixOffset);
    m_fovOffset = static_cast<std::uint32_t>(UnityOffsets::Camera_FOVOffset);
    m_aspectOffset = static_cast<std::uint32_t>(UnityOffsets::Camera_AspectRatioOffset);

    m_lastFov = 0.0f;
    m_lastAspect = 0.0f;
    m_cachedSights.clear();
    m_cachedCurrentOpticSight = 0;
    m_cachedCurrentScopeTransform = 0;
    m_cachedActiveSightIndex = -1;
    m_lastUpdate = {};
    m_lastInitializeAttempt = {};
    m_lastManagedCameraResolve = {};
    m_lastSightRefresh = {};
    m_lastViewMatrixResolve = {};
    m_lastLensRefresh = {};
    m_lastAllCamerasResolve = {};
    m_usedAllCamerasOffset = false;
    m_lastAds = false;
    m_lastUsingOptic = false;
    m_opticMatrixReadFailures = 0;
    m_busyReadSkips = 0;

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
    if (!validPointer(m_gameAssemblyBase))
        m_gameAssemblyBase = mem.GetTarkovPointerSnapshot().gameAssemblyBase;

    if (!validPointer(m_gameAssemblyBase))
        return 0;

    const std::uint64_t hardcoded = m_gameAssemblyBase + UnityOffsets::AllCamera;
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
    const bool hadCachedOpticPath = validPointer(m_opticCameraManager) && validPointer(m_opticCamera);
    std::uint64_t manager = m_opticCameraManager;

    if (!validPointer(m_eftCameraManager))
        (void)resolveEftCameraManagerFromBss(m_eftCameraManager, m_gameAssemblyBase);

    if (!validPointer(manager) && validPointer(m_eftCameraManager))
    {
        readPointer(m_eftCameraManager + kEftCameraManagerOpticManager, manager);
    }

    if (!validPointer(manager))
    {
        manager = resolveManagedComponent(m_opticCamera, "OpticCameraManager");
    }

    if (!validPointer(manager))
    {
        const std::uint64_t resolvedManager = resolveManagedComponent(m_fpsCamera, "CameraManager");

        if (!validPointer(resolvedManager) || !readPointer(resolvedManager + kEftCameraManagerOpticManager, manager))
        {
            // Camera transitions can briefly detach this component
            if (!hadCachedOpticPath)
                m_opticCameraManager = 0;
            return false;
        }

        m_eftCameraManager = resolvedManager;
    }

    std::uint64_t cameraReference = 0;
    std::uint64_t opticCamera = 0;

    if (!readPointer(manager + kOpticCameraManagerCamera, cameraReference) ||
        readManagedComponentName(cameraReference) != "Camera" ||
        !readPointer(cameraReference + kUnityObjectCachedPointer, opticCamera))
    {

        if (!hadCachedOpticPath)
        {
            m_opticCamera = 0;
            m_opticViewMatrixAddress = 0;
            m_opticCameraManager = 0;
        }
        return false;

    }

    const std::uint64_t opticMatrix = resolveViewMatrixAddress(opticCamera);

    const bool opticChanged = opticCamera != m_opticCamera || (validPointer(opticMatrix) && opticMatrix != m_opticViewMatrixAddress);

    m_opticCameraManager = manager;

    if (opticCamera != m_opticCamera)
        m_opticViewMatrixAddress = opticMatrix;
    else if (validPointer(opticMatrix))
        m_opticViewMatrixAddress = opticMatrix;

    m_opticCamera = opticCamera;
    m_opticMatrixReadFailures = 0;

    if (opticChanged)
    {
        m_lastViewMatrixResolve = {};
        m_lastLensRefresh = {};
        m_lastUsingOptic = false;
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

std::uint64_t CameraManager::resolveViewMatrixAddress(std::uint64_t camera) const
{
    if (!validPointer(camera))
        return 0;

    glm::highp_mat4 matrix{};
    const std::uint64_t directAddress = camera + m_viewMatrixOffset;

    if (readUncached(directAddress, matrix) && matrixLooksValid(matrix))
        return directAddress;

    std::uint64_t gameObject = 0;
    std::uint64_t componentArray = 0;
    std::uint64_t matrixBase = 0;

    if (!readPointer(camera + UnityOffsets::GameObject_ComponentsOffset, gameObject) ||
        !readPointer(gameObject + UnityOffsets::GameObject_ComponentsOffset, componentArray) ||
        !readPointer(componentArray + 0x18, matrixBase))
    {
        return 0;
    }

    const std::uint64_t legacyAddress = matrixBase + m_viewMatrixOffset;
    matrix = {};

    return readUncached(legacyAddress, matrix) && matrixLooksValid(matrix)
        ? legacyAddress
        : 0;
}

bool CameraManager::resolveCamerasFromAllCameras(std::uint64_t& fps, std::uint64_t& optic)
{
    fps = 0;
    optic = 0;

    const auto now = std::chrono::steady_clock::now();
    if (m_lastAllCamerasResolve != std::chrono::steady_clock::time_point{} &&
        (now - m_lastAllCamerasResolve) < kAllCamerasRetryInterval)
        return false;
    m_lastAllCamerasResolve = now;

    if (!validPointer(m_allCamerasGlobal))
    {
        m_usedAllCamerasOffset = false;
        m_allCamerasGlobal = resolveAllCamerasGlobal();
    }

    if (!validPointer(m_allCamerasGlobal))
        return false;

    CameraListView list{};
    if (!readCameraList(m_allCamerasGlobal, list))
        return false;

    const int count = list.count;

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

        if (!fps && isFps && validPointer(resolveViewMatrixAddress(camera)))
            fps = camera;
        if (!optic && isOptic)
            optic = camera;

        if (fps && optic)
            break;
    }

    return validPointer(fps) || validPointer(optic);
}

bool CameraManager::resolveCameras()
{
    std::uint64_t fps = 0;
    std::uint64_t optic = 0;

    std::uint64_t fpsCameraReference = 0;
    if (resolveEftCameraManagerFromBss(m_eftCameraManager, m_gameAssemblyBase) &&
        readCameraReferenceFromManager(m_eftCameraManager, fpsCameraReference) &&
        readPointer(fpsCameraReference + kUnityObjectCachedPointer, fps))
    {
        const std::uint64_t fpsMatrix = resolveViewMatrixAddress(fps);
        if (validPointer(fpsMatrix))
        {
            const bool fpsChanged = fps != m_fpsCamera;

            m_fpsCamera = fps;
            m_fpsViewMatrixAddress = fpsMatrix;

            if (fpsChanged)
            {
                m_lastFov = 0.0f;
                m_lastAspect = 0.0f;
                m_lastLensRefresh = {};
                m_opticCamera = 0;
                m_opticViewMatrixAddress = 0;
                m_opticCameraManager = 0;
                m_lastManagedCameraResolve = {};
            }

            if (resolveOpticCameraManager() && validPointer(m_opticCamera))
                return true;

            return true;
        }
    }

    // Use the AllCameras offset path only if the direct EFT chain is unavailable
    if (!resolveCamerasFromAllCameras(fps, optic))
        return false;

    const std::uint64_t fpsMatrix = resolveViewMatrixAddress(fps);
    if (!validPointer(fps) || !validPointer(fpsMatrix))
        return false;

    if (fps != m_fpsCamera || optic != m_opticCamera)
    {
        m_lastFov = 0.0f;
        m_lastAspect = 0.0f;
        m_lastLensRefresh = {};
        m_eftCameraManager = 0;
        m_opticCameraManager = 0;
        m_lastManagedCameraResolve = {};
    }

    m_fpsCamera = fps;
    m_opticCamera = optic;
    m_fpsViewMatrixAddress = fpsMatrix;
    m_opticViewMatrixAddress = resolveViewMatrixAddress(optic);

    return true;
}

bool CameraManager::initialize()
{
    if (!mem.IsDmaOperational())
        return false;

    const bool resolved = resolveCameras();

    if (resolved)
    {
        // Prefer the direct EFT chain when the optional optic becomes ready
        m_lastManagedCameraResolve = std::chrono::steady_clock::now();
        (void)resolveOpticCameraManager();

        LOGS.logInfo(
            "[CAMERA MANAGER] Resolved camera bootstrap.");
    }

    return resolved;
}

float CameraManager::readSelectedZoom(std::uint64_t sightTemplate, std::uint64_t selectedModes, int selectedScope, int& selectedMode) const
{
    selectedMode = 0;

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

    if (std::isfinite(result.scopeZoomValue) &&
        result.scopeZoomValue > 0.0f &&
        result.scopeZoomValue < 100.0f)
    {
        result.resolvedZoom = result.scopeZoomValue;
    }
    else
    {
        std::uint64_t selectedModes = 0;

        readPointer(
            result.sightComponent + sdk::SightComponent::_template,
            result.sightTemplate);
        readPointer(
            result.sightComponent + sdk::SightComponent::ScopeSelectedModes,
            selectedModes);

        result.resolvedZoom = readSelectedZoom(
            result.sightTemplate,
            selectedModes,
            result.selectedScope,
            result.selectedMode);
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

bool CameraManager::update(std::uint64_t localPwa, std::uint64_t currentOpticSight)
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

    return updateFrame(localPwa, isAds, currentOpticSight, now);
}

bool CameraManager::updateWithAds(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight)
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

    return updateFrame(localPwa, isAds, currentOpticSight, now);
}

bool CameraManager::updateFrame(std::uint64_t localPwa, bool isAds, std::uint64_t currentOpticSight, std::chrono::steady_clock::time_point now)
{
    m_lastUpdate = now;

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
            return false;
    }

    // EFT's managed camera objects may become available after the FPS camera.
    if ((!validPointer(m_opticCameraManager) ||
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
                "[CAMERA MANAGER] Resolved optic camera through the "
                "direct EFT CameraManager chain.");
        }
        if (isAds && !validPointer(m_opticCamera))
        {
            std::uint64_t fallbackFps = 0;
            std::uint64_t fallbackOptic = 0;
            if (resolveCamerasFromAllCameras(fallbackFps, fallbackOptic) &&
                validPointer(fallbackOptic))
            {
                m_opticCamera = fallbackOptic;
                m_opticViewMatrixAddress = resolveViewMatrixAddress(fallbackOptic);
            }
        }
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
    state.sights = m_cachedSights;
    state.activeSightVectorIndex = m_cachedActiveSightIndex;

    if (state.activeSightVectorIndex >= 0)
    {
        const CameraSightState& sight = state.sights[static_cast<std::size_t>(state.activeSightVectorIndex)];
        state.magnification = sight.resolvedZoom;
        state.stackedSightResolved = state.sights.size() > 1 && sight.selectedByCurrentOptic;
    }

    state.scoped = state.ads && state.activeSightVectorIndex >= 0 && state.magnification > 1.0f;
    const bool wantOptic = state.scoped && validPointer(m_opticCamera);

    if (!wantOptic)
        m_opticMatrixReadFailures = 0;

    if (wantOptic && !validPointer(m_opticViewMatrixAddress) &&
        (m_lastViewMatrixResolve == std::chrono::steady_clock::time_point{} ||
            (now - m_lastViewMatrixResolve) >= kViewMatrixRetryInterval))
    {
        m_lastViewMatrixResolve = now;
        m_opticViewMatrixAddress = resolveViewMatrixAddress(m_opticCamera);
    }

    // Read FPS even while scoped, so an unavailable optic has a fresh fallback
    // NaN sentinels prevent untouched/partial destinations becoming valid data
    glm::highp_mat4 fpsRaw{};
    glm::highp_mat4 opticRaw{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            fpsRaw[column][row] = opticRaw[column][row] = std::numeric_limits<float>::quiet_NaN();

    float fov = std::numeric_limits<float>::quiet_NaN();
    float aspect = std::numeric_limits<float>::quiet_NaN();
    const bool readLens = wantOptic &&
        (m_lastLensRefresh == std::chrono::steady_clock::time_point{} ||
            (now - m_lastLensRefresh) >= kLensRefreshInterval);
    const bool readOptic = wantOptic && validPointer(m_opticViewMatrixAddress);

    Memory::ScatterReadRequest requests[4]{};
    std::size_t requestCount = 0;
    requests[requestCount++] = { m_fpsViewMatrixAddress, &fpsRaw, sizeof(fpsRaw) };
    if (readOptic)
        requests[requestCount++] = { m_opticViewMatrixAddress, &opticRaw, sizeof(opticRaw) };
    if (readLens)
    {
        requests[requestCount++] = { m_fpsCamera + m_fovOffset, &fov, sizeof(fov) };
        requests[requestCount++] = { m_fpsCamera + m_aspectOffset, &aspect, sizeof(aspect) };
    }

    DWORD bytesRead[4]{};
    const auto readResult = mem.TryReadScatter(requests, requestCount,
        DmaCacheMode::Uncached, "Camera Manager Update", bytesRead);
    if (readResult == Memory::TryScatterReadResult::Busy)
    {
        ++m_busyReadSkips;
        const auto current = snapshot();
        if (current->valid && current->ads == isAds)
            return true;

        publish(std::move(state));
        return false;
    }

    const bool batchRead = readResult == Memory::TryScatterReadResult::Success;
    const bool fpsRead = batchRead && bytesRead[0] == sizeof(fpsRaw) && matrixLooksValid(fpsRaw);
    const bool opticRead = batchRead && readOptic && bytesRead[1] == sizeof(opticRaw) && matrixLooksValid(opticRaw);
    if (readLens)
    {
        m_lastLensRefresh = now;
        const std::size_t lensIndex = readOptic ? 2 : 1;
        if (batchRead && bytesRead[lensIndex] == sizeof(fov) && validFov(fov))
            m_lastFov = fov;
        if (batchRead && bytesRead[lensIndex + 1] == sizeof(aspect) && validAspect(aspect))
            m_lastAspect = aspect;
    }

    if (!fpsRead)
    {
        m_fpsCamera = 0;
        m_fpsViewMatrixAddress = 0;
        m_eftCameraManager = 0;
        m_opticCameraManager = 0;
        m_opticCamera = 0;
        m_opticViewMatrixAddress = 0;
        m_lastFov = 0.0f;
        m_lastAspect = 0.0f;
        m_lastLensRefresh = {};
    }
    else if (wantOptic && readOptic && !opticRead)
    {
        
        if (m_opticMatrixReadFailures < kOpticMatrixFailureLimit)
            ++m_opticMatrixReadFailures;

        if (m_opticMatrixReadFailures == kOpticMatrixFailureLimit)
        {
            const std::uint64_t refreshedMatrix = resolveViewMatrixAddress(m_opticCamera);

            if (validPointer(refreshedMatrix))
            {
                m_opticViewMatrixAddress = refreshedMatrix;
                m_opticMatrixReadFailures = 0;
            }
            else
            {
                m_opticViewMatrixAddress = 0;
                m_lastManagedCameraResolve = now - kManagedCameraRetryInterval + kViewMatrixRetryInterval;
                m_lastViewMatrixResolve = {};
            }
        }
    }
    else if (opticRead)
    {
        m_opticMatrixReadFailures = 0;
    }

    state.fov = m_lastFov;
    state.aspect = m_lastAspect;
    state.usingOptic = fpsRead && opticRead && validFov(state.fov) && validAspect(state.aspect);
    state.activeKind = state.usingOptic ? ManagedCameraKind::Optic : ManagedCameraKind::Fps;
    state.activeCamera = state.usingOptic ? m_opticCamera : m_fpsCamera;
    state.activeViewMatrixAddress = state.usingOptic ? m_opticViewMatrixAddress : m_fpsViewMatrixAddress;
    m_lastUsingOptic = state.usingOptic;
    state.valid = fpsRead;
    if (fpsRead)
    {
        state.rawViewMatrix = state.usingOptic ? opticRaw : fpsRaw;
        state.viewMatrix = glm::transpose(state.rawViewMatrix);
    }
    else
    {
        const auto current = snapshot();
        if (current->valid && current->ads == isAds)
            return true;
    }

    state.fpsCamera = m_fpsCamera;
    state.opticCamera = m_opticCamera;
    state.allCamerasGlobal = m_allCamerasGlobal;
    state.viewMatrixOffset = m_viewMatrixOffset;
    state.fovOffset = m_fovOffset;
    state.aspectOffset = m_aspectOffset;
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
    
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]) || std::fabs(matrix[column][row]) > 100000.0f)
                return false;

    const float m11 = matrix[0][0];
    const float m22 = matrix[1][1];
    const float m33 = matrix[2][2];
    const float m44 = matrix[3][3];

    if (!std::isfinite(m11) || !std::isfinite(m22) ||
        !std::isfinite(m33) || !std::isfinite(m44) ||
        (m11 == 0.0f && m22 == 0.0f && m33 == 0.0f && m44 == 0.0f))
    {
        return false;
    }

    return std::fabs(matrix[3][0]) <= 5000.0f &&
        std::fabs(matrix[3][1]) <= 5000.0f &&
        std::fabs(matrix[3][2]) <= 5000.0f;
}

bool CameraManager::validFov(float value)
{
    return std::isfinite(value) && value > 1.0f && value < 180.0f;
}

bool CameraManager::validAspect(float value)
{
    return std::isfinite(value) && value > 0.1f && value < 10.0f;
}

bool CameraManager::worldToScreen(const CameraManagerState& state, const glm::vec3& world, glm::vec2& screen, float viewportWidth, float viewportHeight)
{
    if (!state.valid ||
        !std::isfinite(viewportWidth) ||
        !std::isfinite(viewportHeight) ||
        viewportWidth <= 0.0f || viewportHeight <= 0.0f ||
        glm::dot(world, world) < 1.0f)
    {
        return false;
    }

    const glm::highp_mat4& matrix = state.viewMatrix;

    const float w = glm::dot(
        glm::vec3{ matrix[3][0], matrix[3][1], matrix[3][2] },
        world) + matrix[3][3];

    if (!std::isfinite(w) || w <= 0.010f)
        return false;

    float x = glm::dot(
        glm::vec3{ matrix[0][0], matrix[0][1], matrix[0][2] },
        world) + matrix[0][3];
    float y = glm::dot(
        glm::vec3{ matrix[1][0], matrix[1][1], matrix[1][2] },
        world) + matrix[1][3];

    if (state.usingOptic)
    {
        if (!validFov(state.fov) || !validAspect(state.aspect))
            return false;

        constexpr float kPi = 3.14159265358979323846f;
        const float halfAngle =
            (kPi / 180.0f) * state.fov * 0.5f;
        const float cotangent =
            std::cos(halfAngle) / std::sin(halfAngle);

        if (!std::isfinite(cotangent) || std::fabs(cotangent) < 0.00001f)
            return false;

        x /= cotangent * state.aspect * 0.5f;
        y /= cotangent * 0.5f;
    }

    const float ndcX = x / w;
    const float ndcY = y / w;

    if (!std::isfinite(ndcX) || !std::isfinite(ndcY))
        return false;

    constexpr float edgeBuffer = 1.5f;
    if (ndcX < -edgeBuffer || ndcX > edgeBuffer ||
        ndcY < -edgeBuffer || ndcY > edgeBuffer)
    {
        return false;
    }

    screen = {
        viewportWidth * 0.5f * (1.0f + ndcX),
        viewportHeight * 0.5f * (1.0f - ndcY)
    };

    return true;
}
