#include "Transform.h"

#include <cmath>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include "../../memory/Memory.h"
#include "UnityOffsets.h"
#include "../../Core/Utilities.h"
#include <unordered_set>

namespace {

constexpr int kMaxParentDepth = 512;
constexpr int kMaxTransformIndex = 1'000'000;

DmaCacheMode ToCacheMode(bool useCache)
{
    return useCache ? DmaCacheMode::Cached : DmaCacheMode::Uncached;
}

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

bool UnityTransform::TryResolveNative(
    uint64_t transformObject,
    uint64_t& nativeTransform,
    bool useCache)
{
    nativeTransform = 0;

    if (!Memory::IsValidPointer(transformObject))
        return false;

    const auto isNativeTransform = [useCache](uint64_t candidate)
    {
        if (!Memory::IsValidPointer(candidate))
            return false;

        const uint64_t hierarchy = mem.Read<uint64_t>(
            candidate + UnityOffsets::TransformAccess_HierarchyOffset,
            ToCacheMode(useCache)
        );

        const int index = mem.Read<int>(
            candidate + UnityOffsets::TransformAccess_IndexOffset,
            ToCacheMode(useCache)
        );

        return Memory::IsValidPointer(hierarchy) &&
            index >= 0 &&
            index < kMaxTransformIndex;
    };

    constexpr uint64_t kManagedNativePointerOffset = 0x10;
    const uint64_t candidate = mem.Read<uint64_t>(
        transformObject + kManagedNativePointerOffset,
        ToCacheMode(useCache)
    );

    if (isNativeTransform(candidate))
    {
        nativeTransform = candidate;
        return true;
    }

    // Some callers already hold the native TransformAccess pointer
    if (!isNativeTransform(transformObject))
        return false;

    nativeTransform = transformObject;
    return true;
}

UnityTransform::UnityTransform(
    uint64_t transformInternal,
    bool useCache)
    : TransformInternal(transformInternal),
    _useCache(useCache)
{
    _position = glm::vec3(0.0f);
    _valid = false;

    if (!Utils::valid_pointer(TransformInternal))
        return;

    const int index = mem.Read<int>(
        TransformInternal +
        UnityOffsets::TransformAccess_IndexOffset,
        ToCacheMode(_useCache)
    );

    if (index < 0 || index >= kMaxTransformIndex)
        return;

    _index = index;

    const uint64_t hierarchy = mem.Read<uint64_t>(
        TransformInternal +
        UnityOffsets::TransformAccess_HierarchyOffset,
        ToCacheMode(_useCache)
    );

    if (!Utils::valid_pointer(hierarchy))
        return;

    _hierarchyAddr = hierarchy;

    const uint64_t vertices = mem.Read<uint64_t>(
        _hierarchyAddr +
        UnityOffsets::Hierarchy_VerticesOffset,
        ToCacheMode(_useCache)
    );

    const uint64_t indices = mem.Read<uint64_t>(
        _hierarchyAddr +
        UnityOffsets::Hierarchy_IndicesOffset,
        ToCacheMode(_useCache)
    );

    if (!Utils::valid_pointer(vertices) ||
        !Utils::valid_pointer(indices))
    {
        return;
    }

    VerticesAddr = vertices;
    _indicesAddr = indices;

    if (!BuildParentChain())
        return;

    _valid = true;
}

const glm::vec3& UnityTransform::Position() const
{
    return _position;
}

bool UnityTransform::BuildParentChain()
{
    _parentChain.clear();

    if (_index < 0 || _index >= kMaxTransformIndex)
        return false;

    if (!Utils::valid_pointer(_indicesAddr))
        return false;

    std::unordered_set<int> visited;
    visited.reserve(32);

    int currentIndex = _index;

    for (int depth = 0; depth < kMaxParentDepth; ++depth)
    {
        if (currentIndex < 0 ||
            currentIndex >= kMaxTransformIndex)
        {
            _parentChain.clear();
            return false;
        }

        if (!visited.insert(currentIndex).second)
        {
            // Circular hierarchy.
            _parentChain.clear();
            return false;
        }

        _parentChain.push_back(currentIndex);

        int parentIndex = -1;

        const uint64_t parentAddress =
            _indicesAddr +
            static_cast<uint64_t>(currentIndex) *
            sizeof(int);

        if (!mem.Read(
            parentAddress,
            &parentIndex,
            sizeof(parentIndex),
            ToCacheMode(_useCache)))
        {
            _parentChain.clear();
            return false;
        }

        if (parentIndex == -1)
            return true;

        if (parentIndex < 0 ||
            parentIndex >= kMaxTransformIndex)
        {
            _parentChain.clear();
            return false;
        }

        currentIndex = parentIndex;
    }

    _parentChain.clear();
    return false;
}

bool UnityTransform::ReadTransformAt(
    int transformIndex,
    TrsX& out) const
{
    out = {};

    if (transformIndex < 0 ||
        transformIndex >= kMaxTransformIndex)
    {
        return false;
    }

    if (!Utils::valid_pointer(VerticesAddr))
        return false;

    const uint64_t address =
        VerticesAddr +
        static_cast<uint64_t>(transformIndex) *
        sizeof(TrsX);

    return mem.Read(
        address,
        &out,
        sizeof(TrsX),
        ToCacheMode(_useCache)
    );
}

glm::vec3& UnityTransform::UpdatePosition(
    std::span<TrsX> vertices)
{
    if (!_valid || _parentChain.empty())
        return _position;

    auto GetTransform = [&](int transformIndex,
        TrsX& out) -> bool
        {
            if (!vertices.empty())
            {
                if (transformIndex < 0 ||
                    static_cast<size_t>(transformIndex) >=
                    vertices.size())
                {
                    return false;
                }

                out = vertices[
                    static_cast<size_t>(transformIndex)
                ];

                return true;
            }

            return ReadTransformAt(transformIndex, out);
        };

    TrsX localTransform{};

    if (!GetTransform(_parentChain.front(), localTransform))
    {
        _valid = false;
        return _position;
    }

    glm::vec3 worldPos = localTransform.t;

    for (size_t i = 1;
        i < _parentChain.size();
        ++i)
    {
        TrsX parent{};

        if (!GetTransform(_parentChain[i], parent))
        {
            _valid = false;
            return _position;
        }

        worldPos *= parent.s;
        worldPos = parent.q * worldPos;
        worldPos += parent.t;
    }

    if (IsAbnormal(worldPos))
    {
        _valid = false;
        return _position;
    }

    _position = worldPos;
    return _position;
}

glm::quat UnityTransform::GetRotation(
    std::span<TrsX> vertices)
{
    if (!_valid || _parentChain.empty())
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    auto GetTransform = [&](int transformIndex,
        TrsX& out) -> bool
        {
            if (!vertices.empty())
            {
                if (transformIndex < 0 ||
                    static_cast<size_t>(transformIndex) >=
                    vertices.size())
                {
                    return false;
                }

                out = vertices[
                    static_cast<size_t>(transformIndex)
                ];

                return true;
            }

            return ReadTransformAt(transformIndex, out);
        };

    TrsX localTransform{};

    if (!GetTransform(_parentChain.front(), localTransform))
    {
        _valid = false;
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    glm::quat worldRotation = localTransform.q;

    for (size_t i = 1;
        i < _parentChain.size();
        ++i)
    {
        TrsX parent{};

        if (!GetTransform(_parentChain[i], parent))
        {
            _valid = false;
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }

        worldRotation = parent.q * worldRotation;
    }

    if (IsAbnormal(worldRotation))
    {
        _valid = false;
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    return worldRotation;
}

bool UnityTransform::UpdateWorldPose(
    glm::vec3& position,
    glm::quat& rotation)
{
    position = _position;
    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    if (!_valid || _parentChain.empty() ||
        !Utils::valid_pointer(VerticesAddr))
    {
        return false;
    }

    std::vector<TrsX> transforms(_parentChain.size());
    std::vector<Memory::ScatterReadRequest> requests;
    requests.reserve(_parentChain.size());

    for (size_t i = 0; i < _parentChain.size(); ++i)
    {
        const int transformIndex = _parentChain[i];

        if (transformIndex < 0 || transformIndex >= kMaxTransformIndex)
        {
            _valid = false;
            return false;
        }

        requests.push_back(
            {
                VerticesAddr + static_cast<uint64_t>(transformIndex) * sizeof(TrsX),
                &transforms[i],
                sizeof(TrsX)
            });
    }

    if (!mem.ReadScatter(
        requests.data(),
        requests.size(),
        ToCacheMode(_useCache),
        "Transform pose"))
    {
        return false;
    }

    glm::vec3 worldPosition = transforms.front().t;
    glm::quat worldRotation = transforms.front().q;

    for (size_t i = 1; i < transforms.size(); ++i)
    {
        const TrsX& parent = transforms[i];
        worldPosition *= parent.s;
        worldPosition = parent.q * worldPosition;
        worldPosition += parent.t;
        worldRotation = parent.q * worldRotation;
    }

    if (IsAbnormal(worldPosition) || IsAbnormal(worldRotation))
    {
        _valid = false;
        return false;
    }

    _position = worldPosition;
    position = worldPosition;
    rotation = worldRotation;
    return true;
}

glm::vec3 UnityTransform::GetRootPosition()
{
    return glm::vec3(0.0f);
}

glm::vec3 UnityTransform::GetLocalPosition()
{
    if (!_valid)
        return glm::vec3(0.0f);

    TrsX local{};

    if (!ReadTransformAt(_index, local))
        return glm::vec3(0.0f);

    return local.t;
}

glm::vec3 UnityTransform::GetLocalScale()
{
    if (!_valid)
        return glm::vec3(0.0f);

    TrsX local{};

    if (!ReadTransformAt(_index, local))
        return glm::vec3(0.0f);

    return local.s;
}

glm::quat UnityTransform::GetLocalRotation()
{
    if (!_valid)
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    TrsX local{};

    if (!ReadTransformAt(_index, local))
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    return local.q;
}

glm::vec3 UnityTransform::TransformPoint(
    glm::vec3 localPoint,
    std::span<TrsX> vertices)
{
    if (!_valid || _parentChain.empty())
        return glm::vec3(0.0f);

    auto GetTransform = [&](int transformIndex,
        TrsX& out) -> bool
        {
            if (!vertices.empty())
            {
                if (transformIndex < 0 ||
                    static_cast<size_t>(transformIndex) >=
                    vertices.size())
                {
                    return false;
                }

                out = vertices[
                    static_cast<size_t>(transformIndex)
                ];

                return true;
            }

            return ReadTransformAt(transformIndex, out);
        };

    glm::vec3 worldPos = localPoint;

    for (const int transformIndex : _parentChain)
    {
        TrsX transform{};

        if (!GetTransform(transformIndex, transform))
        {
            _valid = false;
            return glm::vec3(0.0f);
        }

        worldPos *= transform.s;
        worldPos = transform.q * worldPos;
        worldPos += transform.t;
    }

    if (IsAbnormal(worldPos))
    {
        _valid = false;
        return glm::vec3(0.0f);
    }

    return worldPos;
}

glm::vec3 UnityTransform::InverseTransformPoint(
    glm::vec3 worldPoint,
    std::span<TrsX> vertices)
{
    if (!_valid || _parentChain.empty())
        return glm::vec3(0.0f);

    auto GetTransform = [&](int transformIndex,
        TrsX& out) -> bool
        {
            if (!vertices.empty())
            {
                if (transformIndex < 0 ||
                    static_cast<size_t>(transformIndex) >=
                    vertices.size())
                {
                    return false;
                }

                out = vertices[
                    static_cast<size_t>(transformIndex)
                ];

                return true;
            }

            return ReadTransformAt(transformIndex, out);
        };

    TrsX localTransform{};

    if (!GetTransform(_parentChain.front(), localTransform))
    {
        _valid = false;
        return glm::vec3(0.0f);
    }

    glm::vec3 worldPosition = localTransform.t;
    glm::quat worldRotation = localTransform.q;
    glm::vec3 localScale = localTransform.s;

    for (size_t i = 1;
        i < _parentChain.size();
        ++i)
    {
        TrsX parent{};

        if (!GetTransform(_parentChain[i], parent))
        {
            _valid = false;
            return glm::vec3(0.0f);
        }

        worldPosition *= parent.s;
        worldPosition = parent.q * worldPosition;
        worldPosition += parent.t;

        worldRotation = parent.q * worldRotation;
    }

    const glm::vec3 local =
        glm::conjugate(worldRotation) *
        (worldPoint - worldPosition);

    const glm::vec3 safeScale(
        localScale.x == 0.0f
        ? 1.0f
        : localScale.x,

        localScale.y == 0.0f
        ? 1.0f
        : localScale.y,

        localScale.z == 0.0f
        ? 1.0f
        : localScale.z
    );

    return local / safeScale;
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
