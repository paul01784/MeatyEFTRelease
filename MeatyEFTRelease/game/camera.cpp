#include "headers/camera.h"
#include "../memory/Memory.h"
#include "headers/unitysdk.h"
#include "../app/includes.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"

#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

Camera camera;

uint64_t Camera::fpsCamera = NULL;
uint64_t Camera::opticCamera = NULL;
uint64_t Camera::fpsMatrixAddr = NULL;
uint64_t Camera::opticMatrixAddr = NULL;
float Camera::gameFOV = 0.f;
float Camera::gameAspect = 0.f;

bool Camera::localmpCamera = false;
uint64_t Camera::cameraEntity = NULL;

uint64_t Camera::opticCameraMatrix;
glm::highp_mat4 Camera::g_viewMatrix{};
glm::highp_mat4 Camera::g_viewMatrixOptic{};
glm::highp_mat4 Camera::g_viewMatrixRAW{};
glm::highp_mat4 Camera::g_viewMatrixOpticRAW{};


void Camera::clearCache()
{
    camera.fpsCamera = NULL;
    camera.opticCamera = NULL;
    camera.fpsMatrixAddr = NULL;
    camera.opticMatrixAddr = NULL;
    camera.gameFOV = 0.f;
    camera.gameAspect = 0.f;

    camera.localmpCamera = false;
    camera.cameraEntity = NULL;

    camera.opticCameraMatrix = NULL;
    camera.g_viewMatrix = {};
    camera.g_viewMatrixOptic = {};
    camera.g_viewMatrixRAW = {};
    camera.g_viewMatrixOpticRAW = {};

    camera.initedCamera = false;

    LOGS.logInfo("[CAMERA][CACHE] Data cleared");
}

void Camera::getCameraPtrs()
{
    camera.fpsCamera = 0;
    camera.opticCamera = 0;

    bool debug = false;

    uint64_t cameraArrayPtr = mem.Read<uint64_t>(mem.base + UnityOffsets::AllCamera);
    if (!cameraArrayPtr)
    {
        if (debug) std::cout << "[Camera] Invalid cameraArrayPtr" << std::endl;
        return;
    }

    CameraArray array = mem.Read<CameraArray>(cameraArrayPtr);

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

    if (array.curCount <= 0)
    {
        if (debug) std::cout << "[Camera] Invalid curCount: " << array.curCount << std::endl;
        return;
    }

    if (debug) std::cout << "=== Camera Scan ===" << std::endl;

    for (uint64_t i = 0; i < array.curCount; i++)
    {
        uint64_t cameraEntity = mem.Read<uint64_t>(array.cameras + i * sizeof(uint64_t));
        if (!cameraEntity)
            continue;

        uint64_t go = mem.Read<uint64_t>(cameraEntity + UnityOffsets::GameObject_ComponentsOffset);
        if (!go)
            continue;

        uint64_t namePtr = mem.Read<uint64_t>(go + UnityOffsets::GameObject_NameOffset);
        if (!namePtr)
            continue;

        std::string name = mem.readString(namePtr + 0x0, sizeof(name));
        if (name.empty())
            continue;

        if (debug)
        {
            std::cout << "[" << i << "] "
                << "cameraEntity: 0x" << std::hex << cameraEntity << std::dec
                << "  Name: \"" << name << "\""
                << std::endl;
        }

        if (!camera.fpsCamera && name == "FPS Camera")
        {
            camera.fpsCamera = cameraEntity;
            if (debug) std::cout << " -> FPS Camera FOUND!" << std::endl;
        }

        if (!camera.opticCamera && name == "BaseOpticCamera(Clone)")
        {
            camera.opticCamera = cameraEntity;
            if (debug) std::cout << " -> Optic Camera FOUND!" << std::endl;
        }
    }

    if (Utils::valid_pointer(camera.fpsCamera) && Utils::valid_pointer(camera.opticCamera))
    {
        std::cout << "\n=== Camera Scan Results ===" << std::endl;

        std::cout << "FPS Camera     : ";
        if (camera.fpsCamera)
            std::cout << "0x" << std::hex << camera.fpsCamera << std::dec;
        else
            std::cout << "NOT FOUND";
        std::cout << std::endl;

        std::cout << "Optic Camera   : ";
        if (camera.opticCamera)
            std::cout << "0x" << std::hex << camera.opticCamera << std::dec;
        else
            std::cout << "NOT FOUND";
        std::cout << std::endl;

        std::cout << "==========================\n" << std::endl;
    }
}

class SightInterface
{
public:
    explicit SightInterface(std::uint64_t baseAddr = 0) : m_base(baseAddr) {}

    std::uint64_t Base() const { return m_base; }
    explicit operator bool() const { return m_base != 0; }

    std::uint64_t pZooms() const
    {
        return mem.Read<std::uint64_t>(m_base + sdk::SightInterface::Zooms);
    }

    // Equivalent to: MonoArray<ulong>.Create(pZooms, true)
    UnityArray<std::uint64_t> Zooms() const
    {
        return UnityArray<std::uint64_t>(static_cast<std::uintptr_t>(pZooms()));
    }

private:
    std::uint64_t m_base = 0;
};

class SightComponent
{
public:
    explicit SightComponent(std::uint64_t baseAddr = 0) : m_base(baseAddr) {}

    std::uint64_t Base() const { return m_base; }
    explicit operator bool() const { return m_base != 0; }

    // Field accessors (mirror the C# fields)
    std::uint64_t pSightInterface() const
    {
        return mem.Read<std::uint64_t>(m_base + sdk::SightComponent::_template);
    }

    std::uint64_t pScopeSelectedModes() const
    {
        return mem.Read<std::uint64_t>(m_base + sdk::SightComponent::ScopeSelectedModes);
    }

    int SelectedScope() const
    {
        return mem.Read<int>(m_base + sdk::SightComponent::SelectedScope);
    }

    float ScopeZoomValue() const
    {
        return mem.Read<float>(m_base + sdk::SightComponent::ScopeZoomValue);
    }

    SightInterface GetSightInterface() const
    {
        return mem.Read<SightInterface>(pSightInterface());
    }

    static inline bool IsNormalOrZero(float v)
    {
        if (!std::isfinite(v))
            return false;

        if (v == 0.0f)
            return true;

        return std::fabs(v) >= (std::numeric_limits<float>::min)();
    }

    float GetZoomLevel() const
    {
        try
        {
            const int selectedScope = SelectedScope();

            // Mirror the C# “SelectedScope is < 0 or > 10” safety constraint
            if (selectedScope < 0 || selectedScope > 10)
                return -1.0f;

            const SightInterface si = GetSightInterface();

            // Zooms array (C# "using var zoomArray = SightInterface.Zooms;")
            auto zoomArray = si.Zooms();
            if (selectedScope >= zoomArray.count)
                return -1.0f;

            // Selected scope mode per scope index
            const auto selectedScopeModes = UnityArray<int>(pScopeSelectedModes());
            const int selectedScopeMode =
                (selectedScope < selectedScopeModes.count) ? selectedScopeModes[selectedScope] : 0;

            const std::uint64_t zoomsForScopeAddr = zoomArray[selectedScope];
            const std::uint64_t zoomAddr = zoomsForScopeAddr + UnityArray<float>::ArrBaseOffset + (static_cast<std::uint64_t>(static_cast<std::uint32_t>(selectedScopeMode)) * 0x4ULL);

            const float zoomLevel = mem.Read<float>(zoomAddr, false);

            if (IsNormalOrZero(zoomLevel) && zoomLevel >= 0.0f && zoomLevel < 100.0f)
                return zoomLevel;

            return -1.0f;
        }
        catch (...)
        {
            LOGS.logError("[SIGHT] ERROR in GetZoomLevel");
            return -1.0f;
        }
    }

private:
    std::uint64_t m_base = 0;
};

bool Camera::checkIfOpticMatrix()
{

    try
    {
        const uint64_t opticsPtr =
            mem.Read<uint64_t>(mainGame.localPlayerPWA + sdk::ProceduralWeaponAnimation::_optics);


        MonoList<std::uint64_t> optics(opticsPtr);


        if (optics.count <= 0)
        {
            return false;
        }

        const uint64_t optic0 = optics[0];

        const uint64_t pSightComponent =
            mem.Read<uint64_t>(optic0 + sdk::SightNBone::Mod);


        if (!Utils::valid_pointer(pSightComponent))
        {
            return false;
        }

        SightComponent sightComponent(pSightComponent);

        const float scopeZoom = sightComponent.ScopeZoomValue();
       

        if (scopeZoom != 0.0f)
        {
            return scopeZoom > 0.0f;
        }

        const float zoomLevel = sightComponent.GetZoomLevel();

        const bool result = zoomLevel > 1.0f;
        return result;
    }
    catch (...)
    {
        return false;
    }
}


bool Camera::initedCamera = false;
void Camera::cameraTask()
{
    try
    {

        if (!camera.initedCamera)
        {
            getCameraPtrs();
            getMatrixPtrs();
            camera.initedCamera = true;
        }
        
        if (!Utils::valid_pointer(this->fpsCamera) || !Utils::valid_pointer(this->opticCamera))
        {
            getCameraPtrs();
            getMatrixPtrs();
        }

        auto handle = mem.CreateScatterHandle();

        mem.AddScatterReadRequest(handle, this->fpsCamera + UnityOffsets::Camera_FOVOffset, &this->gameFOV, sizeof(float));
        mem.AddScatterReadRequest(handle, this->fpsCamera + UnityOffsets::Camera_AspectRatioOffset, &this->gameAspect, sizeof(float));
        mem.AddScatterReadRequest(handle, this->opticMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset, &this->g_viewMatrixOpticRAW, sizeof(glm::highp_mat4));
        mem.AddScatterReadRequest(handle, this->fpsMatrixAddr + UnityOffsets::Camera_ViewMatrixOffset, &this->g_viewMatrixRAW, sizeof(glm::highp_mat4));

        mem.ExecuteReadScatter(handle);
        mem.CloseScatterHandle(handle);

        //transpose matrix
        this->g_viewMatrix = glm::transpose(this->g_viewMatrixRAW);
        this->g_viewMatrixOptic = glm::transpose(this->g_viewMatrixOpticRAW);

        if (mainGame.localIsScoped)
        {
            //check if we need optic matrix
            if (checkIfOpticMatrix())
                camera.localmpCamera = true;
            else
                camera.localmpCamera = false;
        }
        else
            camera.localmpCamera = false;

    }
    catch (const std::exception& e) {
        LOGS.logError("Exception caught in cameraUpdater: " + std::string(e.what()) + ". Retrying...");
        return;
    }
    catch (...) {
        LOGS.logError("Unknown exception caught in cameraUpdater. Retrying...");
        return;
    }
}

void Camera::getMatrixPtrs()
{

    if (Utils::valid_pointer(camera.fpsCamera))
    {
        //Get Matrix ptr
        uint64_t gameObject = mem.Read<uint64_t>(fpsCamera + 0x58);
        uint64_t ptr1 = mem.Read<uint64_t>(gameObject + 0x58);
        fpsMatrixAddr = mem.Read<uint64_t>(ptr1 + 0x18);
    }

    if (Utils::valid_pointer(camera.opticCamera))
    {
        //Get Matrix ptr
        uint64_t gameObject = mem.Read<uint64_t>(camera.opticCamera + 0x58);
        uint64_t ptr1 = mem.Read<uint64_t>(gameObject + 0x58);
        opticMatrixAddr = mem.Read<uint64_t>(ptr1 + 0x18);
    }
}