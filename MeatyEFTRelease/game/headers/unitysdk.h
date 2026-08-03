#pragma once
#include <cstdint>
#include <vector>

namespace UnityOffsets
{
    constexpr uint64_t GameObjectManager = 0x1A46AF0;
    constexpr uint64_t AllCamera = 0x19EACC0;
    constexpr uint64_t GameObject_ObjectClassOffset = 0x48;
    constexpr uint64_t GameObject_ComponentsOffset = 0x48;
    constexpr uint64_t GameObject_NameOffset = 0x78;
    constexpr uint64_t MonoBehaviour_ObjectClassOffset = 0x38;
    constexpr uint64_t MonoBehaviour_GameObjectOffset = 0x48;
    constexpr uint64_t MonoBehaviour_EnabledOffset = 0x38;
    constexpr uint64_t MonoBehaviour_IsAddedOffset = 0x39;
    constexpr uint64_t Component_ObjectClassOffset = 0x38;
    constexpr uint64_t Component_GameObjectOffset = 0x48;
    constexpr uint64_t TransformInternal_TransformAccessOffset = 0x78;
    constexpr uint64_t TransformAccess_IndexOffset = 0x80;
    constexpr uint64_t TransformAccess_HierarchyOffset = 0x78; 
    constexpr uint64_t Hierarchy_VerticesOffset = 0x48; 
    constexpr uint64_t Hierarchy_IndicesOffset = 0x8; 
    constexpr uint64_t Hierarchy_RootPositionOffset = 0xB0;
    constexpr uint64_t Camera_ViewMatrixOffset = 0x118; // m_WorldToCameraMatrix Matrix4x4
    constexpr uint64_t Camera_FOVOffset = 0x198;
    constexpr uint64_t Camera_AspectRatioOffset = 0x508;
    constexpr uint64_t Camera_ZoomLevelOffset = 0xE8;
}

static std::vector<uint64_t> TransformChain = {
    0x10,
    UnityOffsets::Component_GameObjectOffset,
    UnityOffsets::GameObject_ComponentsOffset,
    0x8,
    UnityOffsets::Component_ObjectClassOffset,
    0x10 // Transform Internal
};