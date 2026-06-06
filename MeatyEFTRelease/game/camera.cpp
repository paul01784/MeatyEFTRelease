#include "headers/camera.h"
#include "../memory/Memory.h"
#include "headers/unitysdk.h"
#include "../app/includes.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"

#include <algorithm>
#include <cmath>
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

    initedCamera = false;

    resetOpticActivity();
}

// Camera pointers

void Camera::getCameraPtrs()
{
    fpsCamera = 0;
    opticCamera = 0;

    constexpr bool debug = false;

    const uint64_t cameraArrayPtr =
        mem.Read<uint64_t>(mem.base + UnityOffsets::AllCamera);

    if (!Utils::valid_pointer(cameraArrayPtr))
    {
        if (debug)
            std::cout << "[Camera] Invalid cameraArrayPtr" << std::endl;

        return;
    }

    const CameraArray array = mem.Read<CameraArray>(cameraArrayPtr);

    if (!Utils::valid_pointer(array.cameras))
    {
        if (debug)
            std::cout << "[Camera] Invalid array.cameras" << std::endl;

        return;
    }

    if (array.curCount <= 0 || array.curCount > kMaxCameraCount)
    {
        if (debug)
            std::cout << "[Camera] Invalid curCount: " << array.curCount << std::endl;

        return;
    }

    if (debug)
    {
        std::cout << "\n=== CameraArray Info ===" << std::endl;
        std::cout << "BasePtr:     0x" << std::hex << cameraArrayPtr << std::dec << std::endl;
        std::cout << "Cameras ptr: 0x" << std::hex << array.cameras << std::dec << std::endl;
        std::cout << "minCount:    " << array.minCount << std::endl;
        std::cout << "curCount:    " << array.curCount << std::endl;
        std::cout << "maxCount:    " << array.maxCount << std::endl;
        std::cout << "========================" << std::endl;
    }

    for (uint64_t i = static_cast<uint64_t>(array.curCount); i-- > 0;)
    {
        const uint64_t cam =
            mem.Read<uint64_t>(array.cameras + (i * sizeof(uint64_t)));

        if (!Utils::valid_pointer(cam))
            continue;

        const uint64_t go =
            mem.Read<uint64_t>(cam + UnityOffsets::GameObject_ComponentsOffset);

        if (!Utils::valid_pointer(go))
            continue;

        const uint64_t namePtr =
            mem.Read<uint64_t>(go + UnityOffsets::GameObject_NameOffset);

        if (!Utils::valid_pointer(namePtr))
            continue;

        const std::string name = mem.readString(namePtr, 64);

        if (name.empty())
            continue;

        if (debug)
        {
            std::cout << "[" << i << "] "
                << "cameraEntity: 0x" << std::hex << cam << std::dec
                << "  Name: \"" << name << "\""
                << std::endl;
        }

        if (!fpsCamera && name == "FPS Camera")
        {
            fpsCamera = cam;
            cameraEntity = cam;

            if (debug)
                std::cout << " -> FPS Camera FOUND!" << std::endl;
        }

        if (!opticCamera && name == "BaseOpticCamera(Clone)")
        {
            opticCamera = cam;

            if (debug)
                std::cout << " -> Optic Camera FOUND!" << std::endl;
        }

        if (fpsCamera && opticCamera)
            break;
    }

    if (debug)
    {
        std::cout << "\n=== Camera Scan Results ===" << std::endl;
        std::cout << "FPS Camera   : 0x" << std::hex << fpsCamera << std::dec << std::endl;
        std::cout << "Optic Camera : 0x" << std::hex << opticCamera << std::dec << std::endl;
        std::cout << "==========================\n" << std::endl;
    }
}

void Camera::getMatrixPtrs()
{
    fpsMatrixAddr = resolveMatrixAddress(fpsCamera);
    opticMatrixAddr = resolveMatrixAddress(opticCamera);
}

uint64_t Camera::resolveMatrixAddress(uint64_t cameraPtr) const
{
    if (!Utils::valid_pointer(cameraPtr))
        return 0;

    const uint64_t gameObject = mem.Read<uint64_t>(cameraPtr + 0x58);

    if (!Utils::valid_pointer(gameObject))
        return 0;

    const uint64_t ptr1 = mem.Read<uint64_t>(gameObject + 0x58);

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
        Utils::valid_pointer(opticCamera) &&
        Utils::valid_pointer(fpsMatrixAddr) &&
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
    out = {};

    if (!cameraPointersReady())
        return false;

    out.fov = gameFOV;
    out.aspect = gameAspect;

    out.queuedFps = true;
    out.queuedOptic = true;

    auto handle = mem.CreateScatterHandle();

    if (!handle)
        return false;

    mem.AddScatterReadRequest(
        handle,
        fpsCamera + UnityOffsets::Camera_FOVOffset,
        &out.fov,
        sizeof(float)
    );

    mem.AddScatterReadRequest(
        handle,
        fpsCamera + UnityOffsets::Camera_AspectRatioOffset,
        &out.aspect,
        sizeof(float)
    );

    mem.AddScatterReadRequest(
        handle,
        fpsMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
        &out.fpsRaw,
        sizeof(glm::highp_mat4)
    );

    mem.AddScatterReadRequest(
        handle,
        opticMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
        &out.opticRaw,
        sizeof(glm::highp_mat4)
    );

    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    out.fpsMatrixValid = matrixLooksValid(out.fpsRaw);
    out.opticMatrixValid = matrixLooksValid(out.opticRaw);

    return out.fpsMatrixValid && out.opticMatrixValid;
}

void Camera::applyFpsFrame(const FrameData& frame)
{
    if (validFov(frame.fov))
        gameFOV = frame.fov;

    if (validAspect(frame.aspect))
        gameAspect = frame.aspect;

    g_viewMatrixRAW = frame.fpsRaw;
    g_viewMatrix = glm::transpose(frame.fpsRaw);
}

bool Camera::applyOpticFrame(const FrameData& frame)
{
    if (!frame.opticMatrixValid)
        return false;

    g_viewMatrixOpticRAW = frame.opticRaw;
    g_viewMatrixOptic = glm::transpose(frame.opticRaw);

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
    m_matrixDebug.localScoped = mainGame.localIsScoped;
    m_matrixDebug.fpsMatrixValid = frame.fpsMatrixValid;
    m_matrixDebug.opticMatrixValid = frame.opticMatrixValid;

    if (!frame.opticMatrixValid)
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

        if (m_opticNoChangeSamples >= kOpticInactiveSampleLimit)
            m_opticMatrixActive = false;
    }

    m_matrixDebug.noChangeSamples = m_opticNoChangeSamples;
    m_matrixDebug.opticMatrixActive = m_opticMatrixActive;

    return m_opticMatrixActive;
}

// camera task

void Camera::cameraTask()
{
    try
    {
        if (!mainGame.checkIfRaidStarted())
        {
            if (initedCamera || fpsCamera || opticCamera || fpsMatrixAddr || opticMatrixAddr)
                clearCache();

            localmpCamera = false;
            return;
        }

        if (!cameraPointersReady())
        {
            if (!refreshCameraPointersStrict())
            {
                localmpCamera = false;
                m_matrixDebug.usingOpticMatrix = false;
                return;
            }
        }

        FrameData frame{};

        if (!readFrameData(frame))
        {
            localmpCamera = false;
            m_matrixDebug.usingOpticMatrix = false;

            clearCameraPointerCacheOnly();

            LOGS.logWarn("[CAMERA] Matrix read invalid. Cleared camera pointer cache for retry.");
            return;
        }

        // Always update both matrix caches
        applyFpsFrame(frame);
        applyOpticFrame(frame);

        const bool opticActive = updateOpticMatrixActivity(frame);

        // Final active camera choice.
        localmpCamera = mainGame.localIsScoped && opticActive;

        m_matrixDebug.localScoped = mainGame.localIsScoped;
        m_matrixDebug.usingOpticMatrix = localmpCamera;
    }
    catch (const std::exception& e)
    {
        localmpCamera = false;
        m_matrixDebug.usingOpticMatrix = false;

        clearCameraPointerCacheOnly();

        LOGS.logError("Exception caught in cameraTask: " + std::string(e.what()));
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