#include "headers/transform.h"

#include <cmath>
#include <algorithm>
#include "../memory/Memory.h"
#include "headers/unitysdk.h"

UnityTransform::UnityTransform(uint64_t transformInternal, bool useCache)
    : TransformInternal(transformInternal), _useCache(useCache)
{
    _position = glm::vec3(0.0f);

    if (TransformInternal == 0)
    {
        std::cout << "[UnityTransform] TransformInternal is null\n";
        _valid = false;
        return;
    }

    const int index = mem.Read<int>(TransformInternal + UnityOffsets::TransformAccess_IndexOffset);
    if (index > 150000)
    {
        std::cout << "[UnityTransform] Index sanity check failed: " << index << "\n";
        _valid = false;
        return;
    }
    _index = index;

    const uint64_t hierarchy = mem.Read<uint64_t>(TransformInternal + UnityOffsets::TransformAccess_HierarchyOffset);
    if (hierarchy == 0)
    {
        std::cout << "[UnityTransform] Hierarchy is null\n";
        _valid = false;
        return;
    }
    _hierarchyAddr = hierarchy;

    const uint64_t vertices = mem.Read<uint64_t>(_hierarchyAddr + UnityOffsets::Hierarchy_VerticesOffset);
    const uint64_t indices = mem.Read<uint64_t>(_hierarchyAddr + UnityOffsets::Hierarchy_IndicesOffset);

    if (vertices == 0 || indices == 0)
    {
        std::cout << "[UnityTransform] Vertices/Indices pointer invalid (V=0x" << std::hex << vertices
            << ", I=0x" << indices << std::dec << ")\n";
        _valid = false;
        return;
    }

    VerticesAddr = vertices;
    _indicesAddr = indices;

    _indices = ReadIndices();
    if (_indices.empty())
    {
        std::cout << "[UnityTransform] ReadIndices failed/empty\n";
        _valid = false;
        return;
    }

    _valid = true;
}

const glm::vec3& UnityTransform::Position() const
{
    return _position;
}

int UnityTransform::Count() const
{
    if (_index < 0) return 0;
    return _index + 1;
}

glm::vec3& UnityTransform::UpdatePosition(std::span<TrsX> vertices)
{
    if (!_valid)
        return _position;

    std::vector<TrsX> standaloneVertices;
    if (vertices.empty())
    {
        standaloneVertices = ReadVertices();
        vertices = standaloneVertices;
        if (vertices.empty())
        {
            std::cout << "[UnityTransform] UpdatePosition: ReadVertices empty\n";
            return _position;
        }
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return _position;

    if ((int)vertices.size() < count || (int)_indices.size() < count)
    {
        std::cout << "[UnityTransform] UpdatePosition: vertices/indices smaller than Count\n";
        return _position;
    }

    glm::vec3 worldPos = vertices[(size_t)_index].t;

    int index = _indices[(size_t)_index];
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS)
        {
            std::cout << "[UnityTransform] UpdatePosition: exceeded MAX_ITERATIONS\n";
            return _position;
        }
        if (index >= count)
        {
            std::cout << "[UnityTransform] UpdatePosition: parent index out of range: " << index << "\n";
            return _position;
        }

        const TrsX& parent = vertices[(size_t)index];

        worldPos = parent.q * worldPos; // rotate
        worldPos *= parent.s;           // scale (component-wise)
        worldPos += parent.t;           // translate

        index = _indices[(size_t)index];
    }

    if (IsAbnormal(worldPos))
    {
        std::cout << "[UnityTransform] UpdatePosition: abnormal worldPos\n";
        return _position;
    }

    _position = worldPos;
    return _position;
}

glm::quat UnityTransform::GetRotation(std::span<TrsX> vertices)
{
    if (!_valid)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    std::vector<TrsX> standaloneVertices;
    if (vertices.empty())
    {
        standaloneVertices = ReadVertices();
        vertices = standaloneVertices;
        if (vertices.empty())
        {
            std::cout << "[UnityTransform] GetRotation: ReadVertices empty\n";
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if ((int)vertices.size() < count || (int)_indices.size() < count)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    glm::quat worldRot = vertices[(size_t)_index].q;

    int index = _indices[(size_t)_index];
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS)
        {
            std::cout << "[UnityTransform] GetRotation: exceeded MAX_ITERATIONS\n";
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        if (index >= count)
        {
            std::cout << "[UnityTransform] GetRotation: parent index out of range: " << index << "\n";
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        const TrsX& parent = vertices[(size_t)index];
        worldRot = parent.q * worldRot;

        index = _indices[(size_t)index];
    }

    if (IsAbnormal(worldRot))
    {
        std::cout << "[UnityTransform] GetRotation: abnormal worldRot\n";
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    return worldRot;
}

glm::vec3 UnityTransform::GetRootPosition()
{
    std::cout << "[UnityTransform] GetRootPosition: not implemented\n";
    return glm::vec3(0.0f);
}

glm::vec3 UnityTransform::GetLocalPosition()
{
    if (!_valid) return glm::vec3(0.0f);
    const uint64_t addr = VerticesAddr + (uint64_t)_index * (uint64_t)sizeof(TrsX);
    return mem.Read<TrsX>(addr).t;
}

glm::vec3 UnityTransform::GetLocalScale()
{
    if (!_valid) return glm::vec3(0.0f);
    const uint64_t addr = VerticesAddr + (uint64_t)_index * (uint64_t)sizeof(TrsX);
    return mem.Read<TrsX>(addr).s;
}

glm::quat UnityTransform::GetLocalRotation()
{
    if (!_valid) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const uint64_t addr = VerticesAddr + (uint64_t)_index * (uint64_t)sizeof(TrsX);
    return mem.Read<TrsX>(addr).q;
}

glm::vec3 UnityTransform::TransformPoint(glm::vec3 localPoint, std::span<TrsX> vertices)
{
    if (!_valid) return glm::vec3(0.0f);

    std::vector<TrsX> standaloneVertices;
    if (vertices.empty())
    {
        standaloneVertices = ReadVertices();
        vertices = standaloneVertices;
        if (vertices.empty())
        {
            std::cout << "[UnityTransform] TransformPoint: ReadVertices empty\n";
            return glm::vec3(0.0f);
        }
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return glm::vec3(0.0f);

    if ((int)vertices.size() < count || (int)_indices.size() < count)
        return glm::vec3(0.0f);

    glm::vec3 worldPos = localPoint;
    int index = _index;
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS)
        {
            std::cout << "[UnityTransform] TransformPoint: exceeded MAX_ITERATIONS\n";
            return glm::vec3(0.0f);
        }
        if (index >= count)
        {
            std::cout << "[UnityTransform] TransformPoint: index out of range: " << index << "\n";
            return glm::vec3(0.0f);
        }

        const TrsX& parent = vertices[(size_t)index];

        worldPos *= parent.s;
        worldPos = parent.q * worldPos;
        worldPos += parent.t;

        index = _indices[(size_t)index];
    }

    if (IsAbnormal(worldPos))
    {
        std::cout << "[UnityTransform] TransformPoint: abnormal worldPos\n";
        return glm::vec3(0.0f);
    }

    return worldPos;
}

glm::vec3 UnityTransform::InverseTransformPoint(glm::vec3 worldPoint, std::span<TrsX> vertices)
{
    if (!_valid) return glm::vec3(0.0f);

    std::vector<TrsX> standaloneVertices;
    if (vertices.empty())
    {
        standaloneVertices = ReadVertices();
        vertices = standaloneVertices;
        if (vertices.empty())
        {
            std::cout << "[UnityTransform] InverseTransformPoint: ReadVertices empty\n";
            return glm::vec3(0.0f);
        }
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return glm::vec3(0.0f);

    if ((int)vertices.size() < count || (int)_indices.size() < count)
        return glm::vec3(0.0f);

    glm::vec3 worldPos = vertices[(size_t)_index].t;
    glm::quat worldRot = vertices[(size_t)_index].q;
    glm::vec3 localScale = vertices[(size_t)_index].s;

    int index = _indices[(size_t)_index];
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS)
        {
            std::cout << "[UnityTransform] InverseTransformPoint: exceeded MAX_ITERATIONS\n";
            return glm::vec3(0.0f);
        }
        if (index >= count)
        {
            std::cout << "[UnityTransform] InverseTransformPoint: parent index out of range: " << index << "\n";
            return glm::vec3(0.0f);
        }

        const TrsX& parent = vertices[(size_t)index];

        worldPos = parent.q * worldPos;
        worldPos *= parent.s;
        worldPos += parent.t;

        worldRot = parent.q * worldRot;

        index = _indices[(size_t)index];
    }

    const glm::vec3 local = glm::conjugate(worldRot) * (worldPoint - worldPos);

    // Component-wise divide with guard
    const glm::vec3 safeScale(
        localScale.x == 0.0f ? 1.0f : localScale.x,
        localScale.y == 0.0f ? 1.0f : localScale.y,
        localScale.z == 0.0f ? 1.0f : localScale.z
    );
    return local / safeScale;
}

std::vector<int> UnityTransform::ReadIndices()
{
    const int count = Count();
    if (count <= 0 || count > 128000 + 1)
        return {};

    if (_indicesAddr == 0)
        return {};

    std::vector<int> out((size_t)count);
    mem.Read(_indicesAddr, out.data(), out.size() * sizeof(int));
    return out;
}

std::vector<UnityTransform::TrsX> UnityTransform::ReadVertices()
{
    const int count = Count();
    if (count <= 0 || count > 128000 + 1)
        return {};

    if (VerticesAddr == 0)
        return {};

    std::vector<TrsX> out((size_t)count);
    mem.Read(VerticesAddr, out.data(), out.size() * sizeof(TrsX));
    return out;
}

bool UnityTransform::IsAbnormal(const glm::vec3& v)
{
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        return true;

    const float limit = 1'000'000.0f;
    return (std::fabs(v.x) > limit) || (std::fabs(v.y) > limit) || (std::fabs(v.z) > limit);
}

bool UnityTransform::IsAbnormal(const glm::quat& q)
{
    if (!std::isfinite(q.w) || !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z))
        return true;

    const float limit = 1'000'000.0f;
    return (std::fabs(q.w) > limit) || (std::fabs(q.x) > limit) || (std::fabs(q.y) > limit) || (std::fabs(q.z) > limit);
}