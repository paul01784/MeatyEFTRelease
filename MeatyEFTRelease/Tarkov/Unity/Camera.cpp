#include "Camera.h"
#include "../../memory/Memory.h"
#include "UnityOffsets.h"
#include "../../UI/includes.h"
#include "../../Core/Utilities.h"
#include "UnityContainers.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

Camera::Camera()
    : m_publishedProjection(
        std::make_shared<const CameraProjectionState>())
{
}

Camera camera;

// members

bool Camera::initedCamera = false;

uint64_t Camera::fpsCamera = 0;
uint64_t Camera::fpsMatrixAddr = 0;

uint64_t Camera::opticCamera = 0;
uint64_t Camera::opticMatrixAddr = 0;

float Camera::gameFOV = 0.0f;
float Camera::gameAspect = 0.0f;

bool Camera::localmpCamera = false;

uint64_t Camera::cameraEntity = 0;
uint64_t Camera::opticCameraMatrix = 0;

glm::highp_mat4 Camera::g_viewMatrix{};
glm::highp_mat4 Camera::g_viewMatrixOptic{};
glm::highp_mat4 Camera::g_viewMatrixRAW{};
glm::highp_mat4 Camera::g_viewMatrixOpticRAW{};

uint64_t Camera::closestPlayer = 0;
float Camera::closestPlayerDist = 0.0f;

CameraProjectionSnapshot Camera::getProjectionSnapshot() const noexcept
{
    CameraProjectionSnapshot snapshot = m_publishedProjection.load(std::memory_order_acquire);

    if (snapshot)
        return snapshot;

    static const CameraProjectionSnapshot emptySnapshot = std::make_shared<const CameraProjectionState>();

    return emptySnapshot;
}

std::uint64_t Camera::getBusyReadSkips() const noexcept
{
    return m_busyReadSkips.load(std::memory_order_relaxed);
}

void Camera::publishProjectionSnapshot(bool valid, bool scoped)
{
    const auto now = std::chrono::steady_clock::now();

    if (m_lastProjectionPublish != std::chrono::steady_clock::time_point{})
    {
        const double intervalMs = std::chrono::duration<double, std::milli>(now - m_lastProjectionPublish).count();

        if (m_averageProjectionIntervalMs <= 0.0)
            m_averageProjectionIntervalMs = intervalMs;
        else
            m_averageProjectionIntervalMs =
                (m_averageProjectionIntervalMs * 0.88) +
                (intervalMs * 0.12);
    }

    m_lastProjectionPublish = now;

    CameraProjectionState state{};
    state.valid = valid;
    state.scoped = scoped;
    state.usingOptic = scoped && localmpCamera;
    state.viewMatrix = state.usingOptic
        ? g_viewMatrixOptic
        : g_viewMatrix;
    state.fpsViewMatrix = g_viewMatrix;
    state.opticViewMatrix = g_viewMatrixOptic;
    state.fpsRawMatrix = g_viewMatrixRAW;
    state.opticRawMatrix = g_viewMatrixOpticRAW;
    state.gameFOV = gameFOV;
    state.gameAspect = gameAspect;
    state.fpsCamera = fpsCamera;
    state.fpsMatrixAddress = fpsMatrixAddr;
    state.opticCamera = opticCamera;
    state.opticMatrixAddress = opticMatrixAddr;
    state.cameraEntity = cameraEntity;
    state.opticCameraMatrix = opticCameraMatrix;
    state.fpsPointersReady = cameraPointersReady();
    state.opticPointersReady = opticPointersReady();
    state.matrixDebug = m_matrixDebug;
    state.version =
        m_projectionVersion.fetch_add(1, std::memory_order_relaxed) + 1;
    state.publishedAt = now;
    state.averageIntervalMs = m_averageProjectionIntervalMs;

    m_publishedProjection.store(
        std::make_shared<const CameraProjectionState>(std::move(state)),
        std::memory_order_release);
}

// Debug

Camera::MatrixActivityDebugState
Camera::getMatrixActivityDebug() const noexcept
{
    const CameraProjectionSnapshot snapshot = getProjectionSnapshot();
    return snapshot ? snapshot->matrixDebug : MatrixActivityDebugState{};
}

// Cache clear

void Camera::clearCache()
{
    fpsCamera = 0;
    opticCamera = 0;

    fpsMatrixAddr = 0;
    opticMatrixAddr = 0;

    gameFOV = 0.0f;
    gameAspect = 0.0f;

    localmpCamera = false;

    cameraEntity = 0;
    opticCameraMatrix = 0;

    g_viewMatrix = {};
    g_viewMatrixOptic = {};
    g_viewMatrixRAW = {};
    g_viewMatrixOpticRAW = {};

    closestPlayer = 0;
    closestPlayerDist = 0.0f;

    initedCamera = false;

    resetOpticActivity();

    m_matrixDebug = {};

    publishProjectionSnapshot(false, false);

    LOGS.logInfo("[CAMERA][CACHE] Data cleared");
}

void Camera::clearCameraPointerCacheOnly()
{
    fpsCamera = 0;
    opticCamera = 0;

    fpsMatrixAddr = 0;
    opticMatrixAddr = 0;

    cameraEntity = 0;
    opticCameraMatrix = 0;

    localmpCamera = false;
    initedCamera = false;

    resetOpticActivity();

    publishProjectionSnapshot(false, false);
}

// Camera pointers

void Camera::getCameraPtrs()
{
    uint64_t foundFpsCamera = 0;
    uint64_t foundOpticCamera = 0;

    constexpr bool debug = false;

    const auto logInfo = [&](auto&&... values)
        {
            if (!debug)
                return;

            std::ostringstream stream;
            (stream << ... << values);
            LOGS.logInfo(stream.str());
        };

    const auto logWarn = [&](auto&&... values)
        {
            if (!debug)
                return;

            std::ostringstream stream;
            (stream << ... << values);
            LOGS.logWarn(stream.str());
        };

    const uint64_t cameraArrayPtr = mem.Read<uint64_t>(mem.base + UnityOffsets::AllCamera);

    if (!Utils::valid_pointer(cameraArrayPtr))
    {
        logWarn("[Camera] Invalid cameraArrayPtr: 0x",
            std::hex, cameraArrayPtr, std::dec);
        return;
    }

    const CameraArray array = mem.Read<CameraArray>(cameraArrayPtr);

    if (!Utils::valid_pointer(array.cameras))
    {
        logWarn("[Camera] Invalid array.cameras: 0x",
            std::hex, array.cameras, std::dec);
        return;
    }

    if (array.curCount <= 0 || array.curCount > kMaxCameraCount)
    {
        logWarn(
            "[Camera] Invalid curCount: ",
            array.curCount,
            " | Maximum allowed: ",
            kMaxCameraCount
        );
        return;
    }

    logInfo(
        "[Camera] CameraArray Info | BasePtr: 0x",
        std::hex,
        cameraArrayPtr,
        " | CamerasPtr: 0x",
        array.cameras,
        std::dec,
        " | minCount: ",
        array.minCount,
        " | curCount: ",
        array.curCount,
        " | maxCount: ",
        array.maxCount
    );

    std::ostringstream cameraDump;
    cameraDump
        << "Camera object scan\n"
        << "Scan order: highest index to lowest index\n"
        << "Camera array pointer: 0x" << std::hex << cameraArrayPtr << '\n'
        << "Camera entries pointer: 0x" << array.cameras << std::dec << '\n'
        << "Minimum count: " << array.minCount << '\n'
        << "Current count: " << array.curCount << '\n'
        << "Maximum count: " << array.maxCount << "\n\n"
        << "Entries:\n";

    for (uint64_t i = static_cast<uint64_t>(array.curCount); i-- > 0;)
    {
        const uint64_t cam = mem.Read<uint64_t>(array.cameras + (i * sizeof(uint64_t)));

        if (!Utils::valid_pointer(cam))
        {
            cameraDump
                << '[' << std::dec << i << "]"
                << " | Camera: 0x" << std::hex << cam << std::dec
                << " | Status: invalid camera pointer\n";

            logWarn(
                "[Camera] Invalid camera pointer at index ",
                i,
                ": 0x",
                std::hex,
                cam,
                std::dec
            );
            continue;
        }

        const uint64_t go = mem.Read<uint64_t>(cam + UnityOffsets::GameObject_ComponentsOffset);

        if (!Utils::valid_pointer(go))
        {
            cameraDump
                << '[' << std::dec << i << "]"
                << " | Camera: 0x" << std::hex << cam
                << " | GameObject: 0x" << go << std::dec
                << " | Status: invalid GameObject pointer\n";

            logWarn(
                "[Camera] Invalid GameObject at index ",
                i,
                " | Camera: 0x",
                std::hex,
                cam,
                " | GameObject: 0x",
                go,
                std::dec
            );
            continue;
        }

        const uint64_t namePtr = mem.Read<uint64_t>(go + UnityOffsets::GameObject_NameOffset);

        if (!Utils::valid_pointer(namePtr))
        {
            cameraDump
                << '[' << std::dec << i << "]"
                << " | Camera: 0x" << std::hex << cam
                << " | GameObject: 0x" << go
                << " | NamePtr: 0x" << namePtr << std::dec
                << " | Status: invalid name pointer\n";

            logWarn(
                "[Camera] Invalid name pointer at index ",
                i,
                " | Camera: 0x",
                std::hex,
                cam,
                " | GameObject: 0x",
                go,
                " | NamePtr: 0x",
                namePtr,
                std::dec
            );
            continue;
        }

        const std::string name = mem.readString(namePtr, 64);

        if (name.empty())
        {
            cameraDump
                << '[' << std::dec << i << "]"
                << " | Camera: 0x" << std::hex << cam
                << " | GameObject: 0x" << go
                << " | NamePtr: 0x" << namePtr << std::dec
                << " | Name: <empty>"
                << " | Status: empty camera name\n";

            logWarn(
                "[Camera] Empty camera name at index ",
                i,
                " | Camera: 0x",
                std::hex,
                cam,
                std::dec
            );
            continue;
        }

        const bool isFpsCamera = name == "FPS Camera";
        const bool isOpticCamera = name == "BaseOpticCamera(Clone)";

        cameraDump
            << '[' << std::dec << i << "]"
            << " | Camera: 0x" << std::hex << cam
            << " | GameObject: 0x" << go
            << " | NamePtr: 0x" << namePtr << std::dec
            << " | Name: \"" << name << '"'
            << " | FPS match: " << (isFpsCamera ? "yes" : "no")
            << " | Optic match: " << (isOpticCamera ? "yes" : "no")
            << " | Status: ok\n";

        logInfo(
            "[Camera] Index: ",
            i,
            " | CameraEntity: 0x",
            std::hex,
            cam,
            std::dec,
            " | Name: \"",
            name,
            "\""
        );

        if (!foundFpsCamera && isFpsCamera)
        {
            foundFpsCamera = cam;

            logInfo(
                "[Camera] FPS Camera found: 0x",
                std::hex,
                foundFpsCamera,
                std::dec
            );
        }

        if (!foundOpticCamera && isOpticCamera)
        {
            foundOpticCamera = cam;

            logInfo(
                "[Camera] Optic Camera found: 0x",
                std::hex,
                foundOpticCamera,
                std::dec
            );
        }
    }

    if (Utils::valid_pointer(foundFpsCamera))
    {
        if (foundFpsCamera != fpsCamera)
        {
            fpsMatrixAddr = 0;
            initedCamera = false;
        }

        fpsCamera = foundFpsCamera;
        cameraEntity = foundFpsCamera;
    }

    if (Utils::valid_pointer(foundOpticCamera))
    {
        if (foundOpticCamera != opticCamera)
        {
            opticMatrixAddr = 0;
            opticCameraMatrix = 0;
            resetOpticActivity();
        }

        opticCamera = foundOpticCamera;
    }

    cameraDump
        << "\nMatches from this scan:\n"
        << "FPS Camera: 0x" << std::hex << foundFpsCamera << '\n'
        << "Optic Camera: 0x" << foundOpticCamera << '\n'
        << "\nCached cameras after scan:\n"
        << "FPS Camera: 0x" << std::hex << fpsCamera << '\n'
        << "Optic Camera: 0x" << opticCamera << std::dec << '\n';

    std::error_code fileError;
    const std::filesystem::path logsDirectory = "logs";
    std::filesystem::create_directories(logsDirectory, fileError);

    if (fileError)
    {
        LOGS.logWarn(
            "[Camera] Failed to create camera dump directory: " +
            fileError.message()
        );
    }
    else
    {
        const std::filesystem::path dumpPath =
            logsDirectory / "camera_objects.txt";

        std::ofstream output(dumpPath, std::ios::out | std::ios::trunc);

        if (!output.is_open())
        {
            LOGS.logWarn(
                "[Camera] Failed to open camera object dump: " +
                dumpPath.string()
            );
        }
        else
        {
            output << cameraDump.str();
        }
    }

    logInfo(
        "[Camera] Scan complete | FPS Camera: 0x",
        std::hex,
        fpsCamera,
        " | Optic Camera: 0x",
        opticCamera,
        std::dec
    );
}

void Camera::getMatrixPtrs()
{
    if (Utils::valid_pointer(fpsCamera))
    {
        const uint64_t resolvedFpsMatrix = resolveMatrixAddress(fpsCamera);

        if (Utils::valid_pointer(resolvedFpsMatrix))
            fpsMatrixAddr = resolvedFpsMatrix;
    }
    else
    {
        fpsMatrixAddr = 0;
    }

    if (Utils::valid_pointer(opticCamera))
    {
        const uint64_t resolvedOpticMatrix = resolveMatrixAddress(opticCamera);

        if (Utils::valid_pointer(resolvedOpticMatrix))
            opticMatrixAddr = resolvedOpticMatrix;
    }
    else
    {
        opticMatrixAddr = 0;
    }
}

uint64_t Camera::resolveMatrixAddress(uint64_t cameraPtr) const
{
    if (!Utils::valid_pointer(cameraPtr))
        return 0;

    const uint64_t gameObject = mem.Read<uint64_t>(cameraPtr + UnityOffsets::GameObject_ComponentsOffset);

    if (!Utils::valid_pointer(gameObject))
        return 0;
    
    const uint64_t ptr1 = mem.Read<uint64_t>(gameObject + UnityOffsets::GameObject_ComponentsOffset);

    if (!Utils::valid_pointer(ptr1))
        return 0;

    const uint64_t matrixAddr = mem.Read<uint64_t>(ptr1 + 0x18);

    if (!Utils::valid_pointer(matrixAddr))
        return 0;

    return matrixAddr;
}

// Camera pointer readiness retry

bool Camera::cameraPointersReady() const
{
    return
        Utils::valid_pointer(fpsCamera) &&
        Utils::valid_pointer(fpsMatrixAddr);
}

bool Camera::opticPointersReady() const
{
    return
        Utils::valid_pointer(opticCamera) &&
        Utils::valid_pointer(opticMatrixAddr);
}

bool Camera::refreshCameraPointersStrict()
{
    if (!Utils::valid_pointer(fpsCamera))
        getCameraPtrs();

    getMatrixPtrs();

    if (!cameraPointersReady())
    {
        initedCamera = false;
        return false;
    }

    initedCamera = true;
    return true;
}

// Frame read matrix application

Camera::FrameReadStatus Camera::readFrameData(FrameData& out, bool readLens)
{
    FrameData next{};

    if (!cameraPointersReady())
        return FrameReadStatus::Failed;

    next.fov = gameFOV;
    next.aspect = gameAspect;

    next.lensRead = readLens;
    next.queuedOptic =
        mainGame.localIsScoped &&
        opticPointersReady();

    Memory::ScatterReadRequest requests[4]{};
    std::size_t requestCount = 0;

    requests[requestCount++] = {
        fpsMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
        &next.fpsRaw,
        sizeof(glm::highp_mat4)
    };

    if (next.queuedOptic)
    {
        requests[requestCount++] = {
            opticMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
            &next.opticRaw,
            sizeof(glm::highp_mat4)
        };
    }

    if (readLens)
    {
        requests[requestCount++] = {
            fpsCamera + UnityOffsets::Camera_FOVOffset,
            &next.fov,
            sizeof(float)
        };

        requests[requestCount++] = {
            fpsCamera + UnityOffsets::Camera_AspectRatioOffset,
            &next.aspect,
            sizeof(float)
        };
    }

    const Memory::TryScatterReadResult readResult = mem.TryReadScatter(
        requests,
        requestCount,
        DmaCacheMode::Uncached,
        "Camera Update"
    );

    if (readResult == Memory::TryScatterReadResult::Busy)
        return FrameReadStatus::Busy;

    if (readResult != Memory::TryScatterReadResult::Success)
        return FrameReadStatus::Failed;

    next.fpsMatrixValid = matrixLooksValid(next.fpsRaw);
    next.opticMatrixValid =
        next.queuedOptic &&
        matrixLooksValid(next.opticRaw);

    if (!next.fpsMatrixValid)
        return FrameReadStatus::Failed;

    out = next;
    return FrameReadStatus::Success;
}

void Camera::applyFpsFrame(const FrameData& frame)
{
    const glm::highp_mat4 transposed = glm::transpose(frame.fpsRaw);

    if (frame.lensRead)
    {
        if (validFov(frame.fov))
            gameFOV = frame.fov;

        if (validAspect(frame.aspect))
            gameAspect = frame.aspect;
    }

    g_viewMatrixRAW = frame.fpsRaw;
    g_viewMatrix = transposed;
}

bool Camera::applyOpticFrame(const FrameData& frame)
{
    if (!frame.opticMatrixValid)
        return false;

    const glm::highp_mat4 transposed = glm::transpose(frame.opticRaw);

    g_viewMatrixOpticRAW = frame.opticRaw;
    g_viewMatrixOptic = transposed;

    return true;
}

// Optic matrix actvity detectio

void Camera::resetOpticActivity()
{
    m_opticActivityTick = 0;
    m_opticNoChangeSamples = 0;

    m_hasLastOpticMatrix = false;
    m_opticMatrixActive = false;

    m_lastOpticRaw = {};

    m_matrixDebug.localScoped = false;
    m_matrixDebug.fpsMatrixValid = false;
    m_matrixDebug.opticMatrixValid = false;

    m_matrixDebug.opticMatrixChanged = false;
    m_matrixDebug.opticMatrixActive = false;
    m_matrixDebug.usingOpticMatrix = false;

    m_matrixDebug.activityTick = 0;
    m_matrixDebug.noChangeSamples = 0;
    m_matrixDebug.opticMatrixDiff = 0.0f;
}

bool Camera::updateOpticMatrixActivity(const FrameData& frame)
{
    const bool scoped = mainGame.localIsScoped;

    m_matrixDebug.localScoped = scoped;
    m_matrixDebug.fpsMatrixValid = frame.fpsMatrixValid;
    m_matrixDebug.opticMatrixValid = frame.opticMatrixValid;

    if (!scoped || !frame.opticMatrixValid)
    {
        resetOpticActivity();
        return false;
    }

    if (!m_hasLastOpticMatrix)
    {
        m_lastOpticRaw = frame.opticRaw;
        m_hasLastOpticMatrix = true;

        m_opticMatrixActive = false;
        m_opticNoChangeSamples = 0;
        m_opticActivityTick = 0;

        m_matrixDebug.activityTick = m_opticActivityTick;
        m_matrixDebug.noChangeSamples = m_opticNoChangeSamples;
        m_matrixDebug.opticMatrixDiff = 0.0f;
        m_matrixDebug.opticMatrixChanged = false;
        m_matrixDebug.opticMatrixActive = false;

        return false;
    }

    ++m_opticActivityTick;

    if (m_opticActivityTick < kOpticActivityCheckInterval)
    {
        m_matrixDebug.activityTick = m_opticActivityTick;
        m_matrixDebug.noChangeSamples = m_opticNoChangeSamples;
        m_matrixDebug.opticMatrixChanged = false;
        m_matrixDebug.opticMatrixActive = m_opticMatrixActive;

        return m_opticMatrixActive;
    }

    m_opticActivityTick = 0;

    const float diff = matrixDiff(frame.opticRaw, m_lastOpticRaw);
    const bool changed = diff > kOpticMatrixChangeThreshold;

    m_lastOpticRaw = frame.opticRaw;

    m_matrixDebug.activityTick = m_opticActivityTick;
    m_matrixDebug.opticMatrixDiff = diff;
    m_matrixDebug.opticMatrixChanged = changed;

    if (changed)
    {
        m_opticMatrixActive = true;
        m_opticNoChangeSamples = 0;
    }
    else
    {
        ++m_opticNoChangeSamples;
    }

    m_matrixDebug.noChangeSamples = m_opticNoChangeSamples;
    m_matrixDebug.opticMatrixActive = m_opticMatrixActive;

    return m_opticMatrixActive;
}

// camera task

void Camera::cameraTask()
{
    static auto lastCameraRead = std::chrono::steady_clock::time_point{};
    static auto lastCameraPointerRefresh = std::chrono::steady_clock::time_point{};
    static auto lastLensReadAttempt = std::chrono::steady_clock::time_point{};
    static auto lastOpticProbe = std::chrono::steady_clock::time_point{};
    static auto matrixInvalidSince = std::chrono::steady_clock::time_point{};
    static auto opticInvalidSince = std::chrono::steady_clock::time_point{};
    static auto lastMatrixFailureLog = std::chrono::steady_clock::time_point{};
    static bool wasScoped = false;

    constexpr auto kCameraReadInterval = std::chrono::milliseconds(3);
    constexpr auto kLensReadInterval = std::chrono::milliseconds(100);
    constexpr auto kCameraPointerRetryInterval = std::chrono::milliseconds(250);
    constexpr auto kScopedOpticProbeInterval = std::chrono::milliseconds(250);
    constexpr auto kOpticInvalidGracePeriod = std::chrono::milliseconds(250);
    constexpr auto kMatrixInvalidResetDelay = std::chrono::milliseconds(750);
    constexpr auto kMatrixFailureLogCooldown = std::chrono::seconds(5);

    try
    {
        if (!mem.IsDmaOperational())
            return;

        const auto now = std::chrono::steady_clock::now();

        if (lastCameraRead != std::chrono::steady_clock::time_point{} &&
            (now - lastCameraRead) < kCameraReadInterval)
        {
            return;
        }

        const bool scoped = mainGame.localIsScoped;
        const bool scopeJustStarted = scoped && !wasScoped;
        const bool scopeJustEnded = !scoped && wasScoped;

        wasScoped = scoped;

        m_matrixDebug.localScoped = scoped;

        if (scopeJustStarted)
        {
            resetOpticActivity();
            m_matrixDebug.localScoped = true;
            localmpCamera = false;
            publishProjectionSnapshot(matrixLooksValid(g_viewMatrix), true);
        }

        if (scopeJustEnded)
        {
            opticInvalidSince = std::chrono::steady_clock::time_point{};
            lastOpticProbe = std::chrono::steady_clock::time_point{};

            resetOpticActivity();

            localmpCamera = false;
            m_matrixDebug.usingOpticMatrix = false;
            publishProjectionSnapshot(matrixLooksValid(g_viewMatrix), false);
        }

        if (!cameraPointersReady())
        {
            const bool refreshDue =
                lastCameraPointerRefresh == std::chrono::steady_clock::time_point{} ||
                (now - lastCameraPointerRefresh) >= kCameraPointerRetryInterval;

            if (!refreshDue)
                return;

            lastCameraPointerRefresh = now;

            if (!refreshCameraPointersStrict() || !cameraPointersReady())
                return;

            lastLensReadAttempt = std::chrono::steady_clock::time_point{};
        }

        
        if (scoped && (scopeJustStarted || !opticPointersReady()))
        {
            const bool opticProbeDue =
                scopeJustStarted ||
                lastOpticProbe == std::chrono::steady_clock::time_point{} ||
                (now - lastOpticProbe) >= kScopedOpticProbeInterval;

            if (opticProbeDue)
            {
                lastOpticProbe = now;

                const uint64_t prevFpsCam = fpsCamera;
                const uint64_t prevFpsMatrix = fpsMatrixAddr;
                const uint64_t prevCameraEntity = cameraEntity;

                const uint64_t prevOpticCam = opticCamera;
                const uint64_t prevOpticMatrix = opticMatrixAddr;

                const bool cameraListScanNeeded =
                    scopeJustStarted ||
                    !Utils::valid_pointer(fpsCamera) ||
                    !Utils::valid_pointer(opticCamera);

                if (cameraListScanNeeded)
                    getCameraPtrs();

                getMatrixPtrs();

                const bool resolvedFps = cameraPointersReady();
                const bool resolvedOptic = opticPointersReady();

                if (!resolvedFps)
                {
                    fpsCamera = prevFpsCam;
                    fpsMatrixAddr = prevFpsMatrix;
                    cameraEntity = prevCameraEntity;
                }
                else if (fpsCamera != prevFpsCam)
                {
                    lastLensReadAttempt =
                        std::chrono::steady_clock::time_point{};
                }

                if (!resolvedOptic)
                {
                    if (opticCamera == prevOpticCam &&
                        Utils::valid_pointer(prevOpticMatrix))
                    {
                        opticMatrixAddr = prevOpticMatrix;
                    }
                    else
                    {
                        opticMatrixAddr = 0;
                        opticCameraMatrix = 0;

                        if (opticCamera != prevOpticCam)
                            resetOpticActivity();
                    }
                }
                else if (
                    opticCamera != prevOpticCam ||
                    opticMatrixAddr != prevOpticMatrix)
                {
                    resetOpticActivity();
                }
            }
        }

        FrameData frame{};

        const bool lensReadDue =
            lastLensReadAttempt == std::chrono::steady_clock::time_point{} ||
            (now - lastLensReadAttempt) >= kLensReadInterval;

        const FrameReadStatus frameReadStatus =
            readFrameData(frame, lensReadDue);

        if (frameReadStatus == FrameReadStatus::Busy)
        {
            m_busyReadSkips.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        lastCameraRead = now;

        if (lensReadDue)
            lastLensReadAttempt = std::chrono::steady_clock::now();

        if (frameReadStatus == FrameReadStatus::Failed)
        {
            const auto failedAt = std::chrono::steady_clock::now();

            m_matrixDebug.fpsMatrixValid = false;

            if (matrixInvalidSince == std::chrono::steady_clock::time_point{})
                matrixInvalidSince = failedAt;

            // Keep the last valid published frame through transient failures.
            if ((failedAt - matrixInvalidSince) >= kMatrixInvalidResetDelay)
            {
                clearCameraPointerCacheOnly();

                lastCameraPointerRefresh = failedAt;
                matrixInvalidSince = std::chrono::steady_clock::time_point{};

                const bool canLog =
                    lastMatrixFailureLog == std::chrono::steady_clock::time_point{} ||
                    (failedAt - lastMatrixFailureLog) >= kMatrixFailureLogCooldown;

                if (canLog)
                {
                    lastMatrixFailureLog = failedAt;

                    LOGS.logWarn(
                        "[CAMERA] Matrix reads remained invalid for 750ms. "
                        "Cleared camera pointer cache for controlled retry."
                    );
                }
            }

            return;
        }

        matrixInvalidSince = std::chrono::steady_clock::time_point{};

        // FPS matrix should always be fresh.
        applyFpsFrame(frame);

        bool opticActive = false;

        m_matrixDebug.fpsMatrixValid = frame.fpsMatrixValid;
        m_matrixDebug.opticMatrixValid = frame.opticMatrixValid;

        if (!scoped)
        {
            opticInvalidSince = std::chrono::steady_clock::time_point{};
        }
        else if (frame.opticMatrixValid)
        {
            applyOpticFrame(frame);

            opticInvalidSince = std::chrono::steady_clock::time_point{};
            opticActive = updateOpticMatrixActivity(frame);
        }
        else
        {
            const auto frameTime = std::chrono::steady_clock::now();

            if (opticInvalidSince == std::chrono::steady_clock::time_point{})
                opticInvalidSince = frameTime;

            const bool withinGracePeriod =
                (frameTime - opticInvalidSince) < kOpticInvalidGracePeriod;

            opticActive = m_opticMatrixActive && withinGracePeriod;

            if (!withinGracePeriod)
            {
                opticCamera = 0;
                opticMatrixAddr = 0;
                opticCameraMatrix = 0;

                lastOpticProbe = std::chrono::steady_clock::time_point{};
                opticInvalidSince = std::chrono::steady_clock::time_point{};

                resetOpticActivity();
                opticActive = false;
            }
        }

        // A valid optic stays selected while scoped even when the view is still
        localmpCamera = scoped && opticActive;

        m_matrixDebug.usingOpticMatrix = localmpCamera;
        publishProjectionSnapshot(true, scoped);
    }
    catch (const std::exception& e)
    {
        localmpCamera = false;
        m_matrixDebug.usingOpticMatrix = false;

        clearCameraPointerCacheOnly();

        LOGS.logError(
            "Exception caught in cameraTask: " + std::string(e.what())
        );
    }
    catch (...)
    {
        localmpCamera = false;
        m_matrixDebug.usingOpticMatrix = false;

        clearCameraPointerCacheOnly();

        LOGS.logError("Unknown exception caught in cameraTask.");
    }
}

bool Camera::checkIfOpticMatrix()
{
    return mainGame.localIsScoped && m_opticMatrixActive;
}

//helpers

bool Camera::matrixLooksValid(const glm::highp_mat4& m)
{
    constexpr float maxElementValue = 100000.0f;
    constexpr float epsilon = 0.00001f;

    int nonZeroCount = 0;

    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            const float value = m[column][row];

            if (!std::isfinite(value))
                return false;

            if (std::fabs(value) > maxElementValue)
                return false;

            if (std::fabs(value) > epsilon)
                ++nonZeroCount;
        }
    }

    if (nonZeroCount < 6)
        return false;

    const bool diagonalEmpty =
        std::fabs(m[0][0]) <= epsilon &&
        std::fabs(m[1][1]) <= epsilon &&
        std::fabs(m[2][2]) <= epsilon &&
        std::fabs(m[3][3]) <= epsilon;

    return !diagonalEmpty;
}

float Camera::matrixDiff(const glm::highp_mat4& a, const glm::highp_mat4& b)
{
    float maxDiff = 0.0f;

    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            const float diff = std::fabs(a[c][r] - b[c][r]);

            if (diff > maxDiff)
                maxDiff = diff;
        }
    }

    return maxDiff;
}

bool Camera::validFov(float value)
{
    return std::isfinite(value) && value > 1.0f && value < 180.0f;
}

bool Camera::validAspect(float value)
{
    return std::isfinite(value) && value > 0.1f && value < 10.0f;
}
