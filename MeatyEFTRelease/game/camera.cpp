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

// ------------------------------------------------------------
// SightComponentView
// ------------------------------------------------------------

Camera::SightComponentView::SightComponentView(uint64_t base)
    : m_base(base)
{
}

bool Camera::SightComponentView::valid() const
{
    return Utils::valid_pointer(m_base);
}

uint64_t Camera::SightComponentView::base() const
{
    return m_base;
}

uint64_t Camera::SightComponentView::sightInterfacePtr() const
{
    if (!valid())
        return 0;

    return mem.Read<uint64_t>(m_base + sdk::SightComponent::_template);
}

uint64_t Camera::SightComponentView::selectedModesPtr() const
{
    if (!valid())
        return 0;

    return mem.Read<uint64_t>(m_base + sdk::SightComponent::ScopeSelectedModes);
}

int Camera::SightComponentView::selectedScope() const
{
    if (!valid())
        return -1;

    return mem.Read<int>(m_base + sdk::SightComponent::SelectedScope);
}

int Camera::SightComponentView::selectedScopeMode(int selectedScope) const
{
    if (selectedScope < 0 || selectedScope >= kMaxScopeCount)
        return 0;

    const uint64_t modesPtr = selectedModesPtr();

    if (!Utils::valid_pointer(modesPtr))
        return 0;

    UnityArray<int> selectedModes(modesPtr);

    if (selectedModes.count <= 0 || selectedModes.count > kMaxScopeCount)
        return 0;

    if (selectedScope >= selectedModes.count)
        return 0;

    const int selectedMode = selectedModes[selectedScope];

    if (selectedMode < 0 || selectedMode >= kMaxScopeCount)
        return 0;

    return selectedMode;
}

float Camera::SightComponentView::scopeZoomValue() const
{
    if (!valid())
        return -1.0f;

    return mem.Read<float>(m_base + sdk::SightComponent::ScopeZoomValue);
}

float Camera::SightComponentView::getZoomLevel() const
{
    try
    {
        const int scope = selectedScope();

        if (scope < 0 || scope >= kMaxScopeCount)
            return -1.0f;

        const uint64_t sightInterface = sightInterfacePtr();

        if (!Utils::valid_pointer(sightInterface))
            return -1.0f;

        const uint64_t zoomsPtr =
            mem.Read<uint64_t>(sightInterface + sdk::SightInterface::Zooms);

        if (!Utils::valid_pointer(zoomsPtr))
            return -1.0f;

        UnityArray<uint64_t> zooms(zoomsPtr);

        if (zooms.count <= 0 || zooms.count > kMaxScopeCount)
            return -1.0f;

        if (scope >= zooms.count)
            return -1.0f;

        const int mode = selectedScopeMode(scope);

        const uint64_t zoomsForScope = zooms[scope];

        if (!Utils::valid_pointer(zoomsForScope))
            return -1.0f;

        const uint64_t zoomAddr =
            zoomsForScope +
            UnityArray<float>::ArrBaseOffset +
            (static_cast<uint64_t>(mode) * sizeof(float));

        const float zoom = mem.Read<float>(zoomAddr, false);

        if (Camera::validZoom(zoom))
            return zoom;

        return -1.0f;
    }
    catch (...)
    {
        return -1.0f;
    }
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

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

    m_opticBadFrames = 0;
    m_lastSightComponent = 0;

    LOGS.logInfo("[CAMERA][CACHE] Data cleared");
}

void Camera::getCameraPtrs()
{
    fpsCamera = 0;
    opticCamera = 0;

    constexpr bool debug = false;

    const uint64_t cameraArrayPtr = mem.Read<uint64_t>(mem.base + UnityOffsets::AllCamera);

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
        std::cout << "curCount:    " << array.curCount << std::endl;
        std::cout << "maxCount:    " << array.maxCount << std::endl;
        std::cout << "========================" << std::endl;
    }

    for (uint64_t i = static_cast<uint64_t>(array.curCount); i-- > 0;)
    {
        const uint64_t cam = mem.Read<uint64_t>(array.cameras + (i * sizeof(uint64_t)));

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

        // Do not use sizeof(std::string) here.
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

bool Camera::checkIfOpticMatrix()
{
    const ScopeState scope = readScopeState();
    return shouldUseOpticMatrix(scope);
}

const Camera::ScopeDebugState& Camera::getScopeDebug() const
{
    return m_scopeDebug;
}

void Camera::cameraTask()
{
    try
    {
        if (!mainGame.checkIfRaidStarted())
        {
            localmpCamera = false;
            return;
        }

        const bool localScoped = mainGame.localIsScoped;

        m_scopeDebug = {};
        m_scopeDebug.localScoped = localScoped;

        ScopeState scope{};

        if (localScoped)
            scope = readScopeState();

        const bool wantOptic = localScoped && shouldUseOpticMatrix(scope);

        if (!ensureCameraPointers(wantOptic))
        {
            localmpCamera = false;
            return;
        }

        FrameData frame{};

        if (!readFrameData(wantOptic, frame))
        {
            localmpCamera = false;
            return;
        }

        applyFpsFrame(frame);

        bool opticActive = false;

        if (wantOptic)
            opticActive = applyOpticFrame(frame, scope);

        localmpCamera = opticActive;
        m_scopeDebug.usingOpticMatrix = opticActive;

        if (wantOptic)
            handleOpticBadFrame(!opticActive);
        else
            resetOpticBadFrames();


    }
    catch (const std::exception& e)
    {
        localmpCamera = false;
        LOGS.logError("Exception caught in cameraTask: " + std::string(e.what()));
    }
    catch (...)
    {
        localmpCamera = false;
        LOGS.logError("Unknown exception caught in cameraTask.");
    }
}

// ------------------------------------------------------------
// Private flow helpers
// ------------------------------------------------------------

void Camera::refreshCameraPointers()
{
    getCameraPtrs();
    getMatrixPtrs();
}

bool Camera::ensureCameraPointers(bool preferOptic)
{
    if (!initedCamera)
    {
        refreshCameraPointers();
        initedCamera = true;
    }

    const auto fpsOk = []()
        {
            return Utils::valid_pointer(fpsCamera) &&
                Utils::valid_pointer(fpsMatrixAddr);
        };

    const auto opticOk = []()
        {
            return Utils::valid_pointer(opticCamera) &&
                Utils::valid_pointer(opticMatrixAddr);
        };

    if (!fpsOk() || (preferOptic && !opticOk()))
        refreshCameraPointers();

    return fpsOk();
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

bool Camera::readFrameData(bool wantOptic, FrameData& out)
{
    out = {};

    out.fov = gameFOV;
    out.aspect = gameAspect;

    out.queuedOptic =
        wantOptic &&
        Utils::valid_pointer(opticCamera) &&
        Utils::valid_pointer(opticMatrixAddr);

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

    if (out.queuedOptic)
    {
        mem.AddScatterReadRequest(
            handle,
            opticMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset,
            &out.opticRaw,
            sizeof(glm::highp_mat4)
        );
    }

    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    out.fpsMatrixValid = matrixLooksValid(out.fpsRaw);
    out.opticMatrixValid = out.queuedOptic && matrixLooksValid(out.opticRaw);

    return out.fpsMatrixValid;
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

bool Camera::applyOpticFrame(const FrameData& frame, const ScopeState& scope)
{
    if (!scope.wantsOpticMatrix)
        return false;

    if (!frame.queuedOptic)
        return false;

    if (!frame.opticMatrixValid)
        return false;

    g_viewMatrixOpticRAW = frame.opticRaw;
    g_viewMatrixOptic = glm::transpose(frame.opticRaw);

    return true;
}

Camera::ScopeState Camera::readScopeState()
{
    ScopeState state{};

    try
    {
        m_scopeDebug.localScoped = mainGame.localIsScoped;

        if (!Utils::valid_pointer(mainGame.localPlayerPWA))
            return state;

        const uint64_t opticsPtr =
            mem.Read<uint64_t>(mainGame.localPlayerPWA + sdk::ProceduralWeaponAnimation::_optics);

        m_scopeDebug.opticsPtr = opticsPtr;

        if (!Utils::valid_pointer(opticsPtr))
            return state;

        MonoList<uint64_t> optics(opticsPtr);

        m_scopeDebug.opticCount = optics.count;

        if (optics.count <= 0 || optics.count > kMaxOpticCount)
            return state;

        const int opticCount = (std::min)(optics.count, kMaxOpticCount);

        for (int i = 0; i < opticCount; ++i)
        {
            const uint64_t optic = optics[i];

            if (!Utils::valid_pointer(optic))
                continue;

            const uint64_t sightComponentPtr =
                mem.Read<uint64_t>(optic + sdk::SightNBone::Mod);

            if (!Utils::valid_pointer(sightComponentPtr))
                continue;

            SightComponentView sight(sightComponentPtr);

            if (!sight.valid())
                continue;

            state.hasSight = true;
            state.sightComponent = sightComponentPtr;

            state.selectedScope = sight.selectedScope();
            state.selectedMode = sight.selectedScopeMode(state.selectedScope);

            state.zoomLevel = sight.getZoomLevel();
            state.scopeZoomValue = sight.scopeZoomValue();

            if (validZoom(state.zoomLevel))
                state.selectedZoom = state.zoomLevel;
            else if (validZoom(state.scopeZoomValue))
                state.selectedZoom = state.scopeZoomValue;
            else
                state.selectedZoom = -1.0f;

            state.wantsOpticMatrix = zoomNeedsOptic(state.selectedZoom);

            const uint64_t sightInterfacePtr = sight.sightInterfacePtr();
            const uint64_t selectedModesPtr = sight.selectedModesPtr();

            uint64_t zoomsPtr = 0;
            int zoomArrayCount = 0;
            int selectedModesCount = 0;

            if (Utils::valid_pointer(sightInterfacePtr))
            {
                zoomsPtr = mem.Read<uint64_t>(
                    sightInterfacePtr + sdk::SightInterface::Zooms
                );

                if (Utils::valid_pointer(zoomsPtr))
                {
                    UnityArray<uint64_t> zooms(zoomsPtr);
                    zoomArrayCount = zooms.count;
                }
            }

            if (Utils::valid_pointer(selectedModesPtr))
            {
                UnityArray<int> selectedModes(selectedModesPtr);
                selectedModesCount = selectedModes.count;
            }

            m_scopeDebug.hasSight = true;
            m_scopeDebug.wantsOpticMatrix = state.wantsOpticMatrix;

            m_scopeDebug.opticEntry = optic;
            m_scopeDebug.selectedOpticIndex = i;

            m_scopeDebug.sightComponent = sightComponentPtr;
            m_scopeDebug.sightInterface = sightInterfacePtr;
            m_scopeDebug.zoomsPtr = zoomsPtr;
            m_scopeDebug.selectedModesPtr = selectedModesPtr;

            m_scopeDebug.selectedScope = state.selectedScope;
            m_scopeDebug.selectedMode = state.selectedMode;

            m_scopeDebug.zoomArrayCount = zoomArrayCount;
            m_scopeDebug.selectedModesCount = selectedModesCount;

            m_scopeDebug.scopeZoomValue = state.scopeZoomValue;
            m_scopeDebug.zoomLevel = state.zoomLevel;
            m_scopeDebug.selectedZoom = state.selectedZoom;

            if (state.sightComponent != 0 && state.sightComponent != m_lastSightComponent)
            {
                m_lastSightComponent = state.sightComponent;
                resetOpticBadFrames();
            }

            return state;
        }

        return state;
    }
    catch (...)
    {
        return state;
    }
}

bool Camera::shouldUseOpticMatrix(const ScopeState& scope) const
{
    if (!scope.hasSight)
        return false;

    return scope.wantsOpticMatrix;
}

void Camera::handleOpticBadFrame(bool badFrame)
{
    if (!badFrame)
    {
        resetOpticBadFrames();
        return;
    }

    ++m_opticBadFrames;

    if (m_opticBadFrames < kOpticBadRefreshFrames)
        return;

    refreshCameraPointers();
    m_opticBadFrames = 0;

    LOGS.logWarn(
        "Optic camera matrix invalid while zoomed. Refreshed camera pointers. "
        "opticCamera=" + std::to_string(opticCamera) +
        " opticMatrixAddr=" + std::to_string(opticMatrixAddr)
    );
}

void Camera::resetOpticBadFrames()
{
    m_opticBadFrames = 0;
}

// ------------------------------------------------------------
// Validation helpers
// ------------------------------------------------------------

bool Camera::matrixLooksValid(const glm::highp_mat4& m)
{
    int nonZeroCount = 0;
    float maxAbs = 0.0f;

    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            const float v = m[c][r];

            if (!std::isfinite(v))
                return false;

            const float av = std::fabs(v);

            if (av > 100000.0f)
                return false;

            if (av > 0.00001f)
            {
                ++nonZeroCount;
                maxAbs = (std::max)(maxAbs, av);
            }
        }
    }

    if (nonZeroCount < 6)
        return false;

    if (maxAbs < 0.0001f)
        return false;

    return true;
}

bool Camera::validFov(float value)
{
    return std::isfinite(value) && value > 1.0f && value < 180.0f;
}

bool Camera::validAspect(float value)
{
    return std::isfinite(value) && value > 0.1f && value < 10.0f;
}

bool Camera::validZoom(float value)
{
    return std::isfinite(value) && value >= 0.0f && value < 100.0f;
}

bool Camera::zoomNeedsOptic(float value)
{
    return validZoom(value) && value > 1.01f;
}