#pragma once
#include <cstdint>
#include <vector>

namespace UnityOffsets
{
    constexpr uint64_t GameObjectManager = 0x1A233A0;
    constexpr uint64_t AllCamera = 0x19F3080; //0x19EACC0;

    // Compatibility values required only by the unchanged gym/hideout feature.
    constexpr uint64_t GameWorld = 0x70FD1A8;
    constexpr uint64_t GameObjectManager_LastActiveNodeOffset = 0x20;
    constexpr uint64_t GameObjectManager_ActiveNodesOffset = 0x28;
    constexpr uint64_t ComponentArray_SizeOffset = 0x10;
    constexpr uint64_t ComponentArray_CapacityOffset = 0x18;
    constexpr uint64_t ManagedObject_NativePointerOffset = 0x10;

    constexpr uint64_t GameObject_ObjectClassOffset = 0x80;
    constexpr uint64_t GameObject_ComponentsOffset = 0x58;
    constexpr uint64_t GameObject_NameOffset = 0x88;
    constexpr uint64_t MonoBehaviour_ObjectClassOffset = 0x38;
    constexpr uint64_t MonoBehaviour_GameObjectOffset = 0x48;
    constexpr uint64_t MonoBehaviour_EnabledOffset = 0x38;
    constexpr uint64_t MonoBehaviour_IsAddedOffset = 0x39;
    constexpr uint64_t Component_ObjectClassOffset = 0x20;
    constexpr uint64_t Component_GameObjectOffset = 0x58;
    constexpr uint64_t TransformInternal_TransformAccessOffset = 0x70;
    constexpr uint64_t TransformAccess_IndexOffset = 0x78;
    constexpr uint64_t TransformAccess_HierarchyOffset = 0x70; 
    constexpr uint64_t Hierarchy_VerticesOffset = 0x68; 
    constexpr uint64_t Hierarchy_IndicesOffset = 0x40; 
    constexpr uint64_t Hierarchy_RootPositionOffset = 0xB0;
    constexpr uint64_t Camera_WorldToCameraMatrixOffset = 0xA8;
    constexpr uint64_t Camera_ProjectionMatrixOffset = 0xE8;
    constexpr uint64_t Camera_CullingMatrixOffset = 0x128;
    constexpr uint64_t Camera_NonJitteredProjectionSetOffset = 0x57C;
    constexpr uint64_t Camera_NonJitteredProjectionMatrixOffset = 0x780;
    constexpr uint64_t Camera_ViewMatrixOffset = Camera_CullingMatrixOffset; // legacy fallback
    constexpr uint64_t Camera_FOVOffset = 0x1A8;
    constexpr uint64_t Camera_AspectRatioOffset = 0x518;
    constexpr uint64_t Camera_ZoomLevelOffset = 0xE8;

    constexpr uint64_t NativeObject_GameObjectOffset = 0x58;
    constexpr uint64_t NativeGameObject_ComponentArrayOffset = 0x58;
    constexpr uint64_t NativeGameObject_ComponentCountOffset = 0x68;

    constexpr uint64_t InstanceTable = 0x1A237F8;
    constexpr uint64_t PropertyNameRegistry = 0x1A8E910;
    constexpr uint64_t PropertyNameTable1 = 0x19FE2C0;
    constexpr uint64_t PropertyNameTable2 = 0x19FE6A0;
    constexpr uint64_t PropertyNameTable3 = 0x19FE750;

    constexpr uint64_t MeshRendererType = 0x17B0FC8;
    constexpr uint64_t RendererMaterialArrayType = 0x17B1100;
    constexpr uint64_t TransformTypeDescriptor = 0x19CEBC0;
    constexpr uint64_t MeshFilterTypeDescriptor = 0x19CD980;
    constexpr uint64_t ShaderType = 0x1743CD0;
}

static std::vector<uint64_t> TransformChain = {
    0x10,
    UnityOffsets::Component_GameObjectOffset,
    UnityOffsets::GameObject_ComponentsOffset,
    0x8,
    UnityOffsets::Component_ObjectClassOffset,
    0x10 // Transform Internal
};
