#include "headers/camera.h"
#include "../memory/Memory.h"
#include "headers/unitysdk.h"
#include "../app/includes.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>

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

// Debug

const Camera::MatrixActivityDebugState& Camera::getMatrixActivityDebug() const
{
    return m_matrixDebug;
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
}

// Camera pointers

void Camera::getCameraPtrs()
{
    fpsCamera = 0;
    opticCamera = 0;

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

    for (uint64_t i = static_cast<uint64_t>(array.curCount); i-- > 0;)
    {
        const uint64_t cam = mem.Read<uint64_t>(array.cameras + (i * sizeof(uint64_t)));

        if (!Utils::valid_pointer(cam))
        {
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

        const uint64_t go = mem.Read<uint64_t>(cam + UnityOffsets::GameObject_ObjectClassOffset);

        if (!Utils::valid_pointer(go))
        {
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

        if (!fpsCamera && name == "FPS Camera")
        {
            fpsCamera = cam;
            cameraEntity = cam;

            logInfo(
                "[Camera] FPS Camera found: 0x",
                std::hex,
                fpsCamera,
                std::dec
            );
        }

        if (!opticCamera && name == "BaseOpticCamera(Clone)")
        {
            opticCamera = cam;

            logInfo(
                "[Camera] Optic Camera found: 0x",
                std::hex,
                opticCamera,
                std::dec
            );
        }

        if (fpsCamera && opticCamera)
            break;
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
    fpsMatrixAddr =
        Utils::valid_pointer(fpsCamera)
        ? resolveMatrixAddress(fpsCamera)
        : 0;

    opticMatrixAddr =
        Utils::valid_pointer(opticCamera)
        ? resolveMatrixAddress(opticCamera)
        : 0;
}

uint64_t Camera::resolveMatrixAddress(uint64_t cameraPtr) const
{
    if (!Utils::valid_pointer(cameraPtr))
        return 0;

    const uint64_t gameObject = mem.Read<uint64_t>(cameraPtr + UnityOffsets::MonoBehaviour_GameObjectOffset);

    if (!Utils::valid_pointer(gameObject))
        return 0;
    
    const uint64_t ptr1 = mem.Read<uint64_t>(gameObject + 0x48);

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
    clearCameraPointerCacheOnly();

    getCameraPtrs();
    getMatrixPtrs();

    if (!cameraPointersReady())
    {
        clearCameraPointerCacheOnly();
        return false;
    }

    initedCamera = true;
    return true;
}

// Frame read matrix application

bool Camera::readFrameData(FrameData& out)
{
    FrameData next{};

    if (!cameraPointersReady())
        return false;

    next.fov = gameFOV;
    next.aspect = gameAspect;

    next.queuedFps = true;
    next.queuedOptic =
        mainGame.localIsScoped &&
        opticPointersReady();

    auto handle = mem.CreateScatterHandle();

    if (!handle)
        return false;

    if (!mem.AddScatterReadRequest(
        handle,
        fpsCamera + UnityOffsets::Camera_FOVOffset,
        &next.fov,
        sizeof(float)
    ))
    {
        mem.CloseScatterHandle(handle);
        return false;
    }

    if (!mem.AddScatterReadRequest(
        handle,
        fpsCamera + UnityOffsets::Camera_AspectRatioOffset,
        &next.aspect,
        sizeof(float)
    ))
    {
        mem.CloseScatterHandle(handle);
        return false;
    }

    if (!mem.AddScatterReadRequest(
        handle,
        fpsMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
        &next.fpsRaw,
        sizeof(glm::highp_mat4)
    ))
    {
        mem.CloseScatterHandle(handle);
        return false;
    }

    if (next.queuedOptic)
    {
        if (!mem.AddScatterReadRequest(
            handle,
            opticMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
            &next.opticRaw,
            sizeof(glm::highp_mat4)
        ))
        {
            mem.CloseScatterHandle(handle);
            return false;
        }
    }

    const bool executed =
        mem.ExecuteReadScatter(handle, false, "Camera Update");

    mem.CloseScatterHandle(handle);

    if (!executed)
        return false;

    next.fpsMatrixValid = matrixLooksValid(next.fpsRaw);
    next.opticMatrixValid =
        next.queuedOptic &&
        matrixLooksValid(next.opticRaw);

    if (!next.fpsMatrixValid)
        return false;

    out = next;
    return true;
}

void Camera::applyFpsFrame(const FrameData& frame)
{
    const glm::highp_mat4 transposed = glm::transpose(frame.fpsRaw);

    if (validFov(frame.fov))
        gameFOV = frame.fov;

    if (validAspect(frame.aspect))
        gameAspect = frame.aspect;

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
    static auto lastOpticProbe = std::chrono::steady_clock::time_point{};
    static auto matrixInvalidSince = std::chrono::steady_clock::time_point{};
    static auto opticInvalidSince = std::chrono::steady_clock::time_point{};
    static auto lastMatrixFailureLog = std::chrono::steady_clock::time_point{};
    static bool wasScoped = false;

    constexpr auto kCameraReadInterval = std::chrono::milliseconds(4);
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

        lastCameraRead = now;

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
        }

        if (scopeJustEnded)
        {
            opticInvalidSince = std::chrono::steady_clock::time_point{};
            lastOpticProbe = std::chrono::steady_clock::time_point{};

            resetOpticActivity();

            localmpCamera = false;
            m_matrixDebug.usingOpticMatrix = false;
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

                if (!resolvedOptic)
                {
                    if (scopeJustStarted)
                    {
                        opticCamera = 0;
                        opticMatrixAddr = 0;
                        opticCameraMatrix = 0;
                        resetOpticActivity();
                    }
                    else
                    {
                        opticCamera = prevOpticCam;
                        opticMatrixAddr = prevOpticMatrix;
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

        // This remains your normal fast path and still runs every camera task.
        if (!readFrameData(frame))
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
