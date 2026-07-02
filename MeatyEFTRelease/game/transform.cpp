#include "headers/transform.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include "../memory/Memory.h"
#include "headers/unitysdk.h"

namespace {

constexpr int kMaxHierarchyCount = 512;

bool ValidateIndices(const std::vector<int>& indices, int count)
{
    if (count <= 0)
        return false;

    if (static_cast<int>(indices.size()) < count)
        return false;

    for (int i = 0; i < count; ++i)
    {
        const int parent = indices[static_cast<size_t>(i)];
        if (parent < -1 || parent >= count)
            return false;
    }

    return true;
}

void LogTransformIssueThrottled(const char* key, const std::string& message)
{
    using Clock = std::chrono::steady_clock;

    static std::mutex logMutex;
    static std::unordered_map<std::string, Clock::time_point> lastLogged;

    const auto now = Clock::now();
    const std::string throttleKey = key ? key : message;

    {
        std::lock_guard<std::mutex> lock(logMutex);
        const auto it = lastLogged.find(throttleKey);
        if (it != lastLogged.end() && (now - it->second) < std::chrono::seconds(30))
            return;

        lastLogged[throttleKey] = now;
    }

    std::cout << message << '\n';
}

} // namespace

UnityTransform::UnityTransform(uint64_t transformInternal, bool useCache)
    : TransformInternal(transformInternal), _useCache(useCache)
{
    _position = glm::vec3(0.0f);

    if (TransformInternal == 0)
    {
        _valid = false;
        return;
    }

    const int index = mem.Read<int>(TransformInternal + UnityOffsets::TransformAccess_IndexOffset);
    if (index < 0 || index >= kMaxHierarchyCount)
    {
        _valid = false;
        return;
    }
    _index = index;

    const uint64_t hierarchy = mem.Read<uint64_t>(TransformInternal + UnityOffsets::TransformAccess_HierarchyOffset);
    if (hierarchy == 0)
    {
        _valid = false;
        return;
    }
    _hierarchyAddr = hierarchy;

    const uint64_t vertices = mem.Read<uint64_t>(_hierarchyAddr + UnityOffsets::Hierarchy_VerticesOffset);
    const uint64_t indices = mem.Read<uint64_t>(_hierarchyAddr + UnityOffsets::Hierarchy_IndicesOffset);

    if (vertices == 0 || indices == 0)
    {
        _valid = false;
        return;
    }

    VerticesAddr = vertices;
    _indicesAddr = indices;

    _indices = ReadIndices();
    if (_indices.empty() || !ValidateIndices(_indices, Count()))
    {
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
            _valid = false;
            return _position;
        }
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
    {
        _valid = false;
        return _position;
    }

    if (static_cast<int>(vertices.size()) < count || static_cast<int>(_indices.size()) < count)
    {
        _valid = false;
        return _position;
    }

    glm::vec3 worldPos = vertices[static_cast<size_t>(_index)].t;

    int index = _indices[static_cast<size_t>(_index)];
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS || index >= count)
        {
            _valid = false;
            LogTransformIssueThrottled(
                "update_position_chain",
                "[UnityTransform] UpdatePosition: invalid parent chain (index=" + std::to_string(index) +
                ", count=" + std::to_string(count) + ")");
            return _position;
        }

        const TrsX& parent = vertices[static_cast<size_t>(index)];

        worldPos = parent.q * worldPos;
        worldPos *= parent.s;
        worldPos += parent.t;

        index = _indices[static_cast<size_t>(index)];
    }

    if (IsAbnormal(worldPos))
    {
        _valid = false;
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
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (static_cast<int>(vertices.size()) < count || static_cast<int>(_indices.size()) < count)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    glm::quat worldRot = vertices[static_cast<size_t>(_index)].q;

    int index = _indices[static_cast<size_t>(_index)];
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS || index >= count)
        {
            _valid = false;
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        const TrsX& parent = vertices[static_cast<size_t>(index)];
        worldRot = parent.q * worldRot;

        index = _indices[static_cast<size_t>(index)];
    }

    if (IsAbnormal(worldRot))
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    return worldRot;
}

glm::vec3 UnityTransform::GetRootPosition()
{
    return glm::vec3(0.0f);
}

glm::vec3 UnityTransform::GetLocalPosition()
{
    if (!_valid) return glm::vec3(0.0f);
    const uint64_t addr = VerticesAddr + static_cast<uint64_t>(_index) * static_cast<uint64_t>(sizeof(TrsX));
    return mem.Read<TrsX>(addr).t;
}

glm::vec3 UnityTransform::GetLocalScale()
{
    if (!_valid) return glm::vec3(0.0f);
    const uint64_t addr = VerticesAddr + static_cast<uint64_t>(_index) * static_cast<uint64_t>(sizeof(TrsX));
    return mem.Read<TrsX>(addr).s;
}

glm::quat UnityTransform::GetLocalRotation()
{
    if (!_valid) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const uint64_t addr = VerticesAddr + static_cast<uint64_t>(_index) * static_cast<uint64_t>(sizeof(TrsX));
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
            return glm::vec3(0.0f);
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return glm::vec3(0.0f);

    if (static_cast<int>(vertices.size()) < count || static_cast<int>(_indices.size()) < count)
        return glm::vec3(0.0f);

    glm::vec3 worldPos = localPoint;
    int index = _index;
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS || index >= count)
        {
            _valid = false;
            return glm::vec3(0.0f);
        }

        const TrsX& parent = vertices[static_cast<size_t>(index)];

        worldPos *= parent.s;
        worldPos = parent.q * worldPos;
        worldPos += parent.t;

        index = _indices[static_cast<size_t>(index)];
    }

    if (IsAbnormal(worldPos))
        return glm::vec3(0.0f);

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
            return glm::vec3(0.0f);
    }

    const int count = Count();
    if (count <= 0 || _index < 0 || _index >= count)
        return glm::vec3(0.0f);

    if (static_cast<int>(vertices.size()) < count || static_cast<int>(_indices.size()) < count)
        return glm::vec3(0.0f);

    glm::vec3 worldPos = vertices[static_cast<size_t>(_index)].t;
    glm::quat worldRot = vertices[static_cast<size_t>(_index)].q;
    glm::vec3 localScale = vertices[static_cast<size_t>(_index)].s;

    int index = _indices[static_cast<size_t>(_index)];
    int iterations = 0;

    while (index >= 0)
    {
        if (iterations++ > MAX_ITERATIONS || index >= count)
        {
            _valid = false;
            return glm::vec3(0.0f);
        }

        const TrsX& parent = vertices[static_cast<size_t>(index)];

        worldPos = parent.q * worldPos;
        worldPos *= parent.s;
        worldPos += parent.t;

        worldRot = parent.q * worldRot;

        index = _indices[static_cast<size_t>(index)];
    }

    const glm::vec3 local = glm::conjugate(worldRot) * (worldPoint - worldPos);

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
    if (count <= 0 || count > kMaxHierarchyCount)
        return {};

    if (_indicesAddr == 0)
        return {};

    std::vector<int> out(static_cast<size_t>(count));
    if (!mem.Read(_indicesAddr, out.data(), out.size() * sizeof(int), _useCache))
        return {};

    return out;
}

std::vector<UnityTransform::TrsX> UnityTransform::ReadVertices()
{
    const int count = Count();
    if (count <= 0 || count > kMaxHierarchyCount)
        return {};

    if (VerticesAddr == 0)
        return {};

    std::vector<TrsX> out(static_cast<size_t>(count));
    if (!mem.Read(VerticesAddr, out.data(), out.size() * sizeof(TrsX), _useCache))
        return {};

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
