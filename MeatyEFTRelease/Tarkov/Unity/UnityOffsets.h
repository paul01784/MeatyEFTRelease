#pragma once
#include <cstdint>
#include <vector>

namespace UnityOffsets
{

    inline uint64_t GameObjectManager = 0x2054CB0;
    inline uint64_t AllCamera = 0x1F7DB00;

    // Compatibility values required only by the gym/hideout feature
    inline uint64_t GameObjectManager_LastActiveNodeOffset = 0x18;
    inline uint64_t GameObjectManager_ActiveNodesOffset = 0x20;
    constexpr uint64_t ObjectListNode_PreviousOffset = 0x0;
    constexpr uint64_t ObjectListNode_NextOffset = 0x8;
    constexpr uint64_t ObjectListNode_ObjectOffset = 0x10;
    constexpr uint64_t ComponentArray_SizeOffset = 0x10;
    constexpr uint64_t ComponentArray_CapacityOffset = 0x18;
    constexpr uint64_t ManagedObject_NativePointerOffset = 0x10;

    constexpr uint64_t GameObject_ObjectClassOffset = 0x48;
    constexpr uint64_t Camera_GameObjectOffset = 0x48;
    constexpr uint64_t GameObject_ComponentsOffset = 0x48;
    constexpr uint64_t GameObject_ComponentCountOffset = 0x58;
    constexpr uint64_t GameObject_ComponentStepOffset = 0x18;
    constexpr uint64_t GameObject_LocalGameWorldOffset = 0x10;
    constexpr uint64_t GameObject_NameOffset = 0x78;
    constexpr uint64_t MonoBehaviour_ObjectClassOffset = 0x38;
    constexpr uint64_t MonoBehaviour_GameObjectOffset = 0x48;
    constexpr uint64_t MonoBehaviour_EnabledOffset = 0x38;
    constexpr uint64_t MonoBehaviour_IsAddedOffset = 0x39;
    constexpr uint64_t Component_ObjectClassOffset = 0x20;
    constexpr uint64_t Component_GameObjectOffset = 0x48;
    constexpr uint64_t Transform_GameObjectOffset = 0x48;
    constexpr uint64_t Transform_ChildrenOffset = 0x88;
    constexpr uint64_t Transform_ChildCountOffset = 0x98;
    // Native Transform stores its TransformAccess data inline at +0x50
    // hierarchy at +0x50 and index at +0x58
    constexpr uint64_t TransformInternal_TransformAccessOffset = 0x50;
    constexpr uint64_t TransformAccess_IndexOffset = 0x58;
    constexpr uint64_t TransformAccess_HierarchyOffset = 0x50;
    constexpr uint64_t Hierarchy_VerticesOffset = 0xa8; 
    constexpr uint64_t Hierarchy_IndicesOffset = 0x78; 
    constexpr uint64_t Hierarchy_RootPositionOffset = 0xB0;
    constexpr uint64_t Camera_WorldToCameraMatrixOffset = 0xA8;
    constexpr uint64_t Camera_ProjectionMatrixOffset = 0xE8;
    constexpr uint64_t Camera_CurrentViewOffset = 0x18;
    inline uint64_t Camera_CullingMatrixOffset = 0x128;
    constexpr uint64_t Camera_NonJitteredProjectionSetOffset = 0x57C;
    constexpr uint64_t Camera_NonJitteredProjectionMatrixOffset = 0x780;
    inline uint64_t Camera_ViewMatrixOffset = 0x118;
    inline uint64_t Camera_FOVOffset = 0x198;
    inline uint64_t Camera_AspectRatioOffset = 0x510;
    constexpr uint64_t Camera_ZoomLevelOffset = 0xE8;

    constexpr uint64_t NativeObject_GameObjectOffset = 0x48;
    constexpr uint64_t NativeGameObject_ComponentArrayOffset = 0x48;
    constexpr uint64_t NativeGameObject_ComponentCountOffset = GameObject_ComponentCountOffset;

    // Managed Transform -> native Transform wrapper
    constexpr uint64_t TransformChain_Step1Offset = 0x10;
    constexpr uint64_t TransformChain_ComponentEntryOffset = 0x8;
    constexpr uint64_t TransformChain_NativeWrapperOffset = 0x10;
    constexpr uint64_t TransformChain_InternalOffset = 0x0;



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
    UnityOffsets::TransformChain_Step1Offset,
    UnityOffsets::Transform_GameObjectOffset,
    UnityOffsets::GameObject_ComponentsOffset,
    UnityOffsets::TransformChain_ComponentEntryOffset,
    UnityOffsets::TransformChain_NativeWrapperOffset,
    UnityOffsets::TransformChain_InternalOffset
};

