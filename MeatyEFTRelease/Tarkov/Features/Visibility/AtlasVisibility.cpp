#include "AtlasVisibility.h"

#include "../../GameWorld/MainGame.h"
#include "../../GameWorld/RegisteredPlayers.h"
#include "../../GameWorld/Player/Player.h"
#include "../../../UI/debug.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

bool atlasVisibilityGlobals::enabled = false;
int atlasVisibilityGlobals::maxDistance = 250;
AtlasVisibilityService atlasVisibility;

namespace
{
    constexpr uint32_t kColliderStride = 96;
    constexpr uint32_t kHighPolyLayer = 12;
    constexpr uint32_t kTriggerFlag = 1U << 0;
    constexpr uint32_t kMeshCollider = 3;
    constexpr uint32_t kBoxCollider = 0;
    constexpr uint32_t kSphereCollider = 1;
    constexpr uint32_t kCapsuleCollider = 2;
    constexpr uint32_t kLeafSize = 12;
    constexpr float kRayEpsilon = 0.03f;

    struct Aabb
    {
        glm::vec3 min{ std::numeric_limits<float>::infinity() };
        glm::vec3 max{ -std::numeric_limits<float>::infinity() };

        void include(const glm::vec3& point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        void include(const Aabb& other)
        {
            include(other.min);
            include(other.max);
        }

        [[nodiscard]] glm::vec3 center() const
        {
            return (min + max) * 0.5f;
        }
    };

    struct BvhNode
    {
        Aabb bounds{};
        uint32_t start{};
        uint32_t count{};
    };

    struct AffineTransform
    {
        std::array<float, 12> values{};

        [[nodiscard]] glm::vec3 transformPoint(const glm::vec3& point) const
        {
            return
            {
                (values[0] * point.x) + (values[1] * point.y) + (values[2] * point.z) + values[3],
                (values[4] * point.x) + (values[5] * point.y) + (values[6] * point.z) + values[7],
                (values[8] * point.x) + (values[9] * point.y) + (values[10] * point.z) + values[11]
            };
        }

        [[nodiscard]] bool inverse(AffineTransform& outTransform) const
        {
            const float determinant =
                (values[0] * ((values[5] * values[10]) - (values[6] * values[9]))) -
                (values[1] * ((values[4] * values[10]) - (values[6] * values[8]))) +
                (values[2] * ((values[4] * values[9]) - (values[5] * values[8])));

            if (std::fabs(determinant) < 0.000001f)
                return false;

            const float inverseDeterminant = 1.0f / determinant;

            outTransform.values[0] = ((values[5] * values[10]) - (values[6] * values[9])) * inverseDeterminant;
            outTransform.values[1] = ((values[2] * values[9]) - (values[1] * values[10])) * inverseDeterminant;
            outTransform.values[2] = ((values[1] * values[6]) - (values[2] * values[5])) * inverseDeterminant;
            outTransform.values[4] = ((values[6] * values[8]) - (values[4] * values[10])) * inverseDeterminant;
            outTransform.values[5] = ((values[0] * values[10]) - (values[2] * values[8])) * inverseDeterminant;
            outTransform.values[6] = ((values[2] * values[4]) - (values[0] * values[6])) * inverseDeterminant;
            outTransform.values[8] = ((values[4] * values[9]) - (values[5] * values[8])) * inverseDeterminant;
            outTransform.values[9] = ((values[1] * values[8]) - (values[0] * values[9])) * inverseDeterminant;
            outTransform.values[10] = ((values[0] * values[5]) - (values[1] * values[4])) * inverseDeterminant;

            const glm::vec3 translation{ values[3], values[7], values[11] };
            const glm::vec3 inverseTranslation = outTransform.transformPoint(-translation);

            outTransform.values[3] = inverseTranslation.x;
            outTransform.values[7] = inverseTranslation.y;
            outTransform.values[11] = inverseTranslation.z;
            return true;
        }
    };

    struct MeshInfo
    {
        uint32_t vertexOffset{};
        uint32_t vertexCount{};
        uint32_t indexOffset{};
        uint32_t indexCount{};
    };

    struct MeshBvh
    {
        MeshInfo info{};
        std::vector<uint32_t> triangles;
        std::vector<BvhNode> nodes;
    };

    struct Collider
    {
        uint32_t kind{};
        int32_t meshId{ -1 };
        glm::vec3 center{};
        glm::vec3 shape{};
        AffineTransform localToWorld{};
        AffineTransform worldToLocal{};
        Aabb worldBounds{};
    };

    [[nodiscard]] bool isFiniteVector(const glm::vec3& value)
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    [[nodiscard]] bool isFactoryLocation(const std::string& location)
    {
        return location == "factory4_day" ||
            location == "factory4_night" ||
            location == "factory_rework";
    }

    [[nodiscard]] glm::vec3 toAtlasSpace(const glm::vec3& point)
    {
        return { -point.x, point.y, point.z };
    }

    [[nodiscard]] std::filesystem::path getExecutableDirectory()
    {
        std::array<wchar_t, MAX_PATH> modulePath{};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));

        if (length == 0 || length >= modulePath.size())
            return {};

        return std::filesystem::path(modulePath.data()).parent_path();
    }

    [[nodiscard]] std::filesystem::path findFactoryDataDirectory()
    {
        const std::filesystem::path relativePath = "assets/mapdata/factory_rework";
        const std::filesystem::path executablePath = getExecutableDirectory() / relativePath;

        if (std::filesystem::exists(executablePath / "manifest.json"))
            return executablePath;

        const std::filesystem::path workingPath = std::filesystem::current_path() / relativePath;

        if (std::filesystem::exists(workingPath / "manifest.json"))
            return workingPath;

        return {};
    }

    [[nodiscard]] std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file.is_open())
            return {};

        const std::streamsize size = file.tellg();

        if (size <= 0)
            return {};

        std::vector<uint8_t> data(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);

        if (!file.read(reinterpret_cast<char*>(data.data()), size))
            return {};

        return data;
    }

    template <typename Value>
    [[nodiscard]] Value readValue(const std::vector<uint8_t>& data, size_t offset)
    {
        Value value{};

        if (offset + sizeof(Value) > data.size())
            return value;

        std::memcpy(&value, data.data() + offset, sizeof(Value));
        return value;
    }

    [[nodiscard]] bool segmentIntersectsAabb(const glm::vec3& origin, const glm::vec3& direction, const Aabb& bounds)
    {
        float enter = 0.0f;
        float exit = 1.0f;

        for (int axis = 0; axis < 3; ++axis)
        {
            const float start = origin[axis];
            const float delta = direction[axis];
            const float lower = bounds.min[axis];
            const float upper = bounds.max[axis];

            if (std::fabs(delta) < 0.000001f)
            {
                if (start < lower || start > upper)
                    return false;

                continue;
            }

            const float inverseDelta = 1.0f / delta;
            float first = (lower - start) * inverseDelta;
            float second = (upper - start) * inverseDelta;

            if (first > second)
                std::swap(first, second);

            enter = (std::max)(enter, first);
            exit = (std::min)(exit, second);

            if (enter > exit)
                return false;
        }

        return true;
    }

    [[nodiscard]] bool segmentIntersectsTriangle(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& first, const glm::vec3& second, const glm::vec3& third)
    {
        const glm::vec3 edgeOne = second - first;
        const glm::vec3 edgeTwo = third - first;
        const glm::vec3 perpendicular = glm::cross(direction, edgeTwo);
        const float determinant = glm::dot(edgeOne, perpendicular);

        if (std::fabs(determinant) < 0.00000001f)
            return false;

        const float inverseDeterminant = 1.0f / determinant;
        const glm::vec3 offset = origin - first;
        const float u = glm::dot(offset, perpendicular) * inverseDeterminant;

        if (u < -0.00001f || u > 1.00001f)
            return false;

        const glm::vec3 cross = glm::cross(offset, edgeOne);
        const float v = glm::dot(direction, cross) * inverseDeterminant;

        if (v < -0.00001f || (u + v) > 1.00001f)
            return false;

        const float distance = glm::dot(edgeTwo, cross) * inverseDeterminant;
        return distance > 0.0001f && distance < 0.9999f;
    }

    [[nodiscard]] float axisValue(const glm::vec3& value, int axis)
    {
        return value[axis];
    }
}

class AtlasVisibilityService::CollisionMap
{
public:
    std::vector<uint8_t> meshData;
    std::vector<MeshBvh> meshBvhs;
    std::vector<Collider> colliders;
    std::vector<uint32_t> colliderIndexes;
    std::vector<BvhNode> colliderNodes;

    [[nodiscard]] glm::vec3 getMeshVertex(const MeshInfo& info, uint32_t vertexIndex) const
    {
        const size_t offset = static_cast<size_t>(info.vertexOffset) + (static_cast<size_t>(vertexIndex) * sizeof(float) * 3);

        return
        {
            readValue<float>(meshData, offset),
            readValue<float>(meshData, offset + sizeof(float)),
            readValue<float>(meshData, offset + (sizeof(float) * 2))
        };
    }

    [[nodiscard]] uint32_t getMeshIndex(const MeshInfo& info, uint32_t index) const
    {
        const size_t offset = static_cast<size_t>(info.indexOffset) + (static_cast<size_t>(index) * sizeof(uint32_t));
        return readValue<uint32_t>(meshData, offset);
    }

    [[nodiscard]] Aabb getTriangleBounds(const MeshBvh& mesh, uint32_t triangle) const
    {
        const uint32_t index = triangle * 3;
        const glm::vec3 first = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index));
        const glm::vec3 second = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index + 1));
        const glm::vec3 third = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index + 2));

        Aabb bounds{};
        bounds.include(first);
        bounds.include(second);
        bounds.include(third);
        return bounds;
    }

    [[nodiscard]] glm::vec3 getTriangleCenter(const MeshBvh& mesh, uint32_t triangle) const
    {
        const uint32_t index = triangle * 3;
        const glm::vec3 first = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index));
        const glm::vec3 second = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index + 1));
        const glm::vec3 third = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index + 2));
        return (first + second + third) / 3.0f;
    }

    void buildMeshBvh(MeshBvh& mesh)
    {
        const uint32_t triangleCount = mesh.info.indexCount / 3;

        if (triangleCount == 0)
            return;

        mesh.triangles.resize(triangleCount);

        for (uint32_t triangle = 0; triangle < triangleCount; ++triangle)
            mesh.triangles[triangle] = triangle;

        mesh.nodes.reserve((triangleCount * 2 / kLeafSize) + 8);
        mesh.nodes.push_back({});

        struct BuildRange
        {
            uint32_t nodeIndex{};
            uint32_t begin{};
            uint32_t end{};
        };

        std::vector<BuildRange> pending{ { 0, 0, triangleCount } };

        while (!pending.empty())
        {
            const BuildRange range = pending.back();
            pending.pop_back();

            Aabb bounds{};

            for (uint32_t index = range.begin; index < range.end; ++index)
                bounds.include(getTriangleBounds(mesh, mesh.triangles[index]));

            const uint32_t count = range.end - range.begin;

            if (count <= kLeafSize)
            {
                mesh.nodes[range.nodeIndex] = { bounds, range.begin, count };
                continue;
            }

            const glm::vec3 extent = bounds.max - bounds.min;
            const int axis = extent.x >= extent.y && extent.x >= extent.z
                ? 0
                : extent.y >= extent.z
                    ? 1
                    : 2;
            const uint32_t middle = range.begin + (count / 2);

            std::nth_element(
                mesh.triangles.begin() + range.begin,
                mesh.triangles.begin() + middle,
                mesh.triangles.begin() + range.end,
                [&](uint32_t first, uint32_t second)
                {
                    return axisValue(getTriangleCenter(mesh, first), axis) <
                        axisValue(getTriangleCenter(mesh, second), axis);
                });

            const uint32_t leftNode = static_cast<uint32_t>(mesh.nodes.size());
            mesh.nodes.push_back({});
            mesh.nodes.push_back({});
            mesh.nodes[range.nodeIndex] = { bounds, leftNode, 0 };
            pending.push_back({ leftNode, range.begin, middle });
            pending.push_back({ leftNode + 1, middle, range.end });
        }
    }

    [[nodiscard]] bool segmentHitsMesh(const MeshBvh& mesh, const glm::vec3& localOrigin, const glm::vec3& localTarget) const
    {
        if (mesh.nodes.empty())
            return false;

        const glm::vec3 direction = localTarget - localOrigin;
        std::array<uint32_t, 128> localStack{};
        std::vector<uint32_t> overflowStack;
        size_t stackSize = 1;
        localStack[0] = 0;

        auto push = [&](uint32_t value)
        {
            if (stackSize < localStack.size())
                localStack[stackSize++] = value;
            else
                overflowStack.push_back(value);
        };

        auto pop = [&]() -> uint32_t
        {
            if (!overflowStack.empty())
            {
                const uint32_t value = overflowStack.back();
                overflowStack.pop_back();
                return value;
            }

            return localStack[--stackSize];
        };

        while (stackSize > 0 || !overflowStack.empty())
        {
            const BvhNode& node = mesh.nodes[pop()];

            if (!segmentIntersectsAabb(localOrigin, direction, node.bounds))
                continue;

            if (node.count == 0)
            {
                push(node.start);
                push(node.start + 1);
                continue;
            }

            for (uint32_t offset = 0; offset < node.count; ++offset)
            {
                const uint32_t triangle = mesh.triangles[node.start + offset];
                const uint32_t index = triangle * 3;
                const glm::vec3 first = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index));
                const glm::vec3 second = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index + 1));
                const glm::vec3 third = getMeshVertex(mesh.info, getMeshIndex(mesh.info, index + 2));

                if (segmentIntersectsTriangle(localOrigin, direction, first, second, third))
                    return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool segmentHitsBox(const Collider& collider, const glm::vec3& localOrigin, const glm::vec3& localTarget) const
    {
        const glm::vec3 halfSize = collider.shape * 0.5f;
        const Aabb bounds{ collider.center - halfSize, collider.center + halfSize };
        return segmentIntersectsAabb(localOrigin, localTarget - localOrigin, bounds);
    }

    [[nodiscard]] bool segmentHitsSphere(const Collider& collider, const glm::vec3& localOrigin, const glm::vec3& localTarget) const
    {
        const glm::vec3 direction = localTarget - localOrigin;
        const glm::vec3 offset = localOrigin - collider.center;
        const float radius = collider.shape.x;
        const float a = glm::dot(direction, direction);

        if (a < 0.000001f)
            return false;

        const float b = 2.0f * glm::dot(offset, direction);
        const float c = glm::dot(offset, offset) - (radius * radius);
        const float discriminant = (b * b) - (4.0f * a * c);

        if (discriminant < 0.0f)
            return false;

        const float root = std::sqrt(discriminant);
        const float first = (-b - root) / (2.0f * a);
        const float second = (-b + root) / (2.0f * a);
        return (first > 0.0001f && first < 0.9999f) ||
            (second > 0.0001f && second < 0.9999f);
    }

    [[nodiscard]] bool segmentHitsCapsule(const Collider& collider, const glm::vec3& localOrigin, const glm::vec3& localTarget) const
    {
        const float radius = collider.shape.x;
        const float height = (std::max)(collider.shape.y, radius * 2.0f);
        const int axis = static_cast<int>(collider.shape.z);
        const glm::vec3 halfExtent = axis == 0
            ? glm::vec3(height * 0.5f, radius, radius)
            : axis == 2
                ? glm::vec3(radius, radius, height * 0.5f)
                : glm::vec3(radius, height * 0.5f, radius);
        const Aabb bounds{ collider.center - halfExtent, collider.center + halfExtent };
        return segmentIntersectsAabb(localOrigin, localTarget - localOrigin, bounds);
    }

    [[nodiscard]] bool segmentHitsCollider(const Collider& collider, const glm::vec3& worldOrigin, const glm::vec3& worldTarget) const
    {
        const glm::vec3 localOrigin = collider.worldToLocal.transformPoint(worldOrigin);
        const glm::vec3 localTarget = collider.worldToLocal.transformPoint(worldTarget);

        if (collider.kind == kMeshCollider && collider.meshId >= 0 && static_cast<size_t>(collider.meshId) < meshBvhs.size())
            return segmentHitsMesh(meshBvhs[collider.meshId], localOrigin, localTarget);

        if (collider.kind == kBoxCollider)
            return segmentHitsBox(collider, localOrigin, localTarget);

        if (collider.kind == kSphereCollider)
            return segmentHitsSphere(collider, localOrigin, localTarget);

        if (collider.kind == kCapsuleCollider)
            return segmentHitsCapsule(collider, localOrigin, localTarget);

        return false;
    }

    void buildColliderBvh()
    {
        if (colliders.empty())
            return;

        colliderIndexes.resize(colliders.size());

        for (uint32_t index = 0; index < colliderIndexes.size(); ++index)
            colliderIndexes[index] = index;

        colliderNodes.reserve((colliders.size() * 2 / kLeafSize) + 8);
        colliderNodes.push_back({});

        struct BuildRange
        {
            uint32_t nodeIndex{};
            uint32_t begin{};
            uint32_t end{};
        };

        std::vector<BuildRange> pending{ { 0, 0, static_cast<uint32_t>(colliderIndexes.size()) } };

        while (!pending.empty())
        {
            const BuildRange range = pending.back();
            pending.pop_back();

            Aabb bounds{};

            for (uint32_t index = range.begin; index < range.end; ++index)
                bounds.include(colliders[colliderIndexes[index]].worldBounds);

            const uint32_t count = range.end - range.begin;

            if (count <= kLeafSize)
            {
                colliderNodes[range.nodeIndex] = { bounds, range.begin, count };
                continue;
            }

            const glm::vec3 extent = bounds.max - bounds.min;
            const int axis = extent.x >= extent.y && extent.x >= extent.z
                ? 0
                : extent.y >= extent.z
                    ? 1
                    : 2;
            const uint32_t middle = range.begin + (count / 2);

            std::nth_element(
                colliderIndexes.begin() + range.begin,
                colliderIndexes.begin() + middle,
                colliderIndexes.begin() + range.end,
                [&](uint32_t first, uint32_t second)
                {
                    return axisValue(colliders[first].worldBounds.center(), axis) <
                        axisValue(colliders[second].worldBounds.center(), axis);
                });

            const uint32_t leftNode = static_cast<uint32_t>(colliderNodes.size());
            colliderNodes.push_back({});
            colliderNodes.push_back({});
            colliderNodes[range.nodeIndex] = { bounds, leftNode, 0 };
            pending.push_back({ leftNode, range.begin, middle });
            pending.push_back({ leftNode + 1, middle, range.end });
        }
    }

    [[nodiscard]] bool hasLineOfSight(const glm::vec3& gameOrigin, const glm::vec3& gameTarget) const
    {
        if (colliderNodes.empty())
            return false;

        glm::vec3 origin = toAtlasSpace(gameOrigin);
        glm::vec3 target = toAtlasSpace(gameTarget);
        glm::vec3 direction = target - origin;
        const float length = glm::length(direction);

        if (length < kRayEpsilon * 2.0f)
            return true;

        const glm::vec3 offset = (direction / length) * kRayEpsilon;
        origin += offset;
        target -= offset;
        direction = target - origin;

        std::array<uint32_t, 128> localStack{};
        std::vector<uint32_t> overflowStack;
        size_t stackSize = 1;
        localStack[0] = 0;

        auto push = [&](uint32_t value)
        {
            if (stackSize < localStack.size())
                localStack[stackSize++] = value;
            else
                overflowStack.push_back(value);
        };

        auto pop = [&]() -> uint32_t
        {
            if (!overflowStack.empty())
            {
                const uint32_t value = overflowStack.back();
                overflowStack.pop_back();
                return value;
            }

            return localStack[--stackSize];
        };

        while (stackSize > 0 || !overflowStack.empty())
        {
            const BvhNode& node = colliderNodes[pop()];

            if (!segmentIntersectsAabb(origin, direction, node.bounds))
                continue;

            if (node.count == 0)
            {
                push(node.start);
                push(node.start + 1);
                continue;
            }

            for (uint32_t offsetIndex = 0; offsetIndex < node.count; ++offsetIndex)
            {
                const Collider& collider = colliders[colliderIndexes[node.start + offsetIndex]];

                if (segmentHitsCollider(collider, origin, target))
                    return false;
            }
        }

        return true;
    }
};

namespace
{
    [[nodiscard]] Aabb getTransformedBounds(const AffineTransform& transform, const Aabb& localBounds)
    {
        Aabb bounds{};

        for (int x = 0; x < 2; ++x)
        {
            for (int y = 0; y < 2; ++y)
            {
                for (int z = 0; z < 2; ++z)
                {
                    const glm::vec3 point
                    {
                        x == 0 ? localBounds.min.x : localBounds.max.x,
                        y == 0 ? localBounds.min.y : localBounds.max.y,
                        z == 0 ? localBounds.min.z : localBounds.max.z
                    };

                    bounds.include(transform.transformPoint(point));
                }
            }
        }

        return bounds;
    }

    [[nodiscard]] std::shared_ptr<const AtlasVisibilityService::CollisionMap> loadFactoryMap(const std::filesystem::path& dataDirectory, std::string& error)
    {
        const std::filesystem::path manifestPath = dataDirectory / "manifest.json";
        const std::filesystem::path collidersPath = dataDirectory / "colliders.bin";
        const std::filesystem::path meshPath = dataDirectory / "collider_meshes.bin";

        std::ifstream manifestFile(manifestPath);

        if (!manifestFile.is_open())
        {
            error = "Factory manifest is missing";
            return nullptr;
        }

        nlohmann::json manifest;

        try
        {
            manifestFile >> manifest;
        }
        catch (const nlohmann::json::exception&)
        {
            error = "Factory manifest is invalid";
            return nullptr;
        }

        if (!manifest.contains("colliderMeshes") || !manifest["colliderMeshes"].is_array())
        {
            error = "Factory manifest has no collider mesh list";
            return nullptr;
        }

        const std::vector<uint8_t> colliderData = readBinaryFile(collidersPath);
        std::vector<uint8_t> meshData = readBinaryFile(meshPath);

        if (colliderData.empty() || meshData.empty() || colliderData.size() % kColliderStride != 0)
        {
            error = "Factory collider files are incomplete";
            return nullptr;
        }

        auto map = std::make_shared<AtlasVisibilityService::CollisionMap>();
        map->meshData = std::move(meshData);
        map->meshBvhs.resize(manifest["colliderMeshes"].size());

        for (const nlohmann::json& meshJson : manifest["colliderMeshes"])
        {
            const size_t id = meshJson.value("id", static_cast<size_t>(map->meshBvhs.size()));

            if (id >= map->meshBvhs.size())
                continue;

            MeshInfo info{};
            info.vertexOffset = meshJson.value("vtxOffset", 0U);
            info.vertexCount = meshJson.value("vtxCount", 0U);
            info.indexOffset = meshJson.value("idxOffset", 0U);
            info.indexCount = meshJson.value("idxCount", 0U);

            const size_t vertexEnd = static_cast<size_t>(info.vertexOffset) + (static_cast<size_t>(info.vertexCount) * sizeof(float) * 3);
            const size_t indexEnd = static_cast<size_t>(info.indexOffset) + (static_cast<size_t>(info.indexCount) * sizeof(uint32_t));

            if (info.vertexCount == 0 || info.indexCount < 3 || info.indexCount % 3 != 0 || vertexEnd > map->meshData.size() || indexEnd > map->meshData.size())
                continue;

            map->meshBvhs[id].info = info;
        }

        std::unordered_set<int32_t> usedMeshIds;
        const size_t colliderCount = colliderData.size() / kColliderStride;
        map->colliders.reserve(colliderCount / 2);

        for (size_t index = 0; index < colliderCount; ++index)
        {
            const size_t offset = index * kColliderStride;
            const uint32_t layer = readValue<uint32_t>(colliderData, offset + 80);
            const uint32_t flags = readValue<uint32_t>(colliderData, offset + 84);
            const uint32_t kind = readValue<uint32_t>(colliderData, offset + 48);

            if (layer != kHighPolyLayer || (flags & kTriggerFlag) != 0)
                continue;

            Collider collider{};
            collider.kind = kind;
            collider.meshId = readValue<int32_t>(colliderData, offset + 52);
            collider.center =
            {
                readValue<float>(colliderData, offset + 56),
                readValue<float>(colliderData, offset + 60),
                readValue<float>(colliderData, offset + 64)
            };
            collider.shape =
            {
                readValue<float>(colliderData, offset + 68),
                readValue<float>(colliderData, offset + 72),
                readValue<float>(colliderData, offset + 76)
            };

            for (size_t valueIndex = 0; valueIndex < collider.localToWorld.values.size(); ++valueIndex)
                collider.localToWorld.values[valueIndex] = readValue<float>(colliderData, offset + (valueIndex * sizeof(float)));

            if (!collider.localToWorld.inverse(collider.worldToLocal))
                continue;

            if (kind == kMeshCollider)
            {
                if (collider.meshId < 0 || static_cast<size_t>(collider.meshId) >= map->meshBvhs.size() || map->meshBvhs[collider.meshId].info.indexCount == 0)
                    continue;

                usedMeshIds.insert(collider.meshId);
            }
            else if (kind != kBoxCollider && kind != kSphereCollider && kind != kCapsuleCollider)
            {
                continue;
            }

            map->colliders.emplace_back(std::move(collider));
        }

        for (const int32_t meshId : usedMeshIds)
        {
            MeshBvh& mesh = map->meshBvhs[meshId];
            map->buildMeshBvh(mesh);
        }

        std::vector<Collider> validColliders;
        validColliders.reserve(map->colliders.size());

        for (Collider& collider : map->colliders)
        {
            Aabb localBounds{};

            if (collider.kind == kMeshCollider)
            {
                const MeshBvh& mesh = map->meshBvhs[collider.meshId];

                if (mesh.nodes.empty())
                    continue;

                localBounds = mesh.nodes.front().bounds;
            }
            else if (collider.kind == kBoxCollider)
            {
                const glm::vec3 halfSize = collider.shape * 0.5f;
                localBounds = { collider.center - halfSize, collider.center + halfSize };
            }
            else if (collider.kind == kSphereCollider)
            {
                localBounds =
                {
                    collider.center - glm::vec3(collider.shape.x),
                    collider.center + glm::vec3(collider.shape.x)
                };
            }
            else
            {
                const float radius = collider.shape.x;
                const float height = (std::max)(collider.shape.y, radius * 2.0f);
                const int axis = static_cast<int>(collider.shape.z);
                const glm::vec3 halfExtent = axis == 0
                    ? glm::vec3(height * 0.5f, radius, radius)
                    : axis == 2
                        ? glm::vec3(radius, radius, height * 0.5f)
                        : glm::vec3(radius, height * 0.5f, radius);
                localBounds = { collider.center - halfExtent, collider.center + halfExtent };
            }

            collider.worldBounds = getTransformedBounds(collider.localToWorld, localBounds);

            if (isFiniteVector(collider.worldBounds.min) && isFiniteVector(collider.worldBounds.max))
                validColliders.emplace_back(std::move(collider));
        }

        map->colliders = std::move(validColliders);
        map->buildColliderBvh();

        if (map->colliderNodes.empty())
        {
            error = "Factory collision data had no usable solid colliders";
            return nullptr;
        }

        return map;
    }

    [[nodiscard]] bool getBonePosition(const Player& player, boneListIndexes bone, glm::vec3& position)
    {
        const size_t index = static_cast<size_t>(bone);

        if (index >= player.bonePositions.size() || index >= player.bonePtrs.size() || !Utils::valid_pointer(player.bonePtrs[index]))
            return false;

        position = player.bonePositions[index];
        return isFiniteVector(position) && glm::dot(position, position) > 0.0001f;
    }

    [[nodiscard]] glm::vec3 getLocalEyePosition(const Player& localPlayer)
    {
        glm::vec3 head{};

        if (getBonePosition(localPlayer, boneListIndexes::Head, head))
            return head;

        return localPlayer.location + glm::vec3(0.0f, 1.55f, 0.0f);
    }

    [[nodiscard]] std::array<glm::vec3, 3> getPlayerVisibilityTargets(const Player& player)
    {
        std::array<glm::vec3, 3> targets
        {
            player.location + glm::vec3(0.0f, 1.55f, 0.0f),
            player.location + glm::vec3(0.0f, 1.15f, 0.0f),
            player.location + glm::vec3(0.0f, 0.85f, 0.0f)
        };

        getBonePosition(player, boneListIndexes::Head, targets[0]);
        getBonePosition(player, boneListIndexes::Neck, targets[1]);
        getBonePosition(player, boneListIndexes::Pelvis, targets[2]);
        return targets;
    }
}

void AtlasVisibilityService::clearVisibilityFlags()
{
    const PlayerSnapshot snapshot = registeredPlayers.getCacheSnapshot();
    std::vector<PlayerVisibilityEdit> edits;

    for (const Player& player : *snapshot)
    {
        if (!player.isLocal && player.visibleToLocal)
            edits.push_back({ player.instance, false });
    }

    registeredPlayers.applyVisibilityEdits(edits);
}

void AtlasVisibilityService::startFactoryLoad()
{
    std::lock_guard<std::mutex> lock(stateMutex);

    if (factoryLoadStarted || factoryMap)
        return;

    const std::filesystem::path dataDirectory = findFactoryDataDirectory();

    if (dataDirectory.empty())
    {
        statusText = "Factory collision data is missing";
        factoryLoadStarted = true;
        LOGS.logError("[VISIBILITY] Factory collision data is missing");
        return;
    }

    factoryLoadStarted = true;
    statusText = "Factory collision data is loading";
    factoryLoad = std::async(
        std::launch::async,
        [dataDirectory]()
        {
            std::string error;
            std::shared_ptr<const CollisionMap> map = loadFactoryMap(dataDirectory, error);

            if (!map)
                throw std::runtime_error(error.empty() ? "Factory collision data could not load" : error);

            return map;
        });
}

void AtlasVisibilityService::pollFactoryLoad()
{
    std::lock_guard<std::mutex> lock(stateMutex);

    if (!factoryLoad.valid() || factoryMap || factoryLoad.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    try
    {
        factoryMap = factoryLoad.get();
        statusText = "Factory collision data is ready";
        LOGS.logInfo("[VISIBILITY] Factory collision data is ready");
    }
    catch (const std::exception& exception)
    {
        statusText = std::string("Factory collision data failed: ") + exception.what();
        LOGS.logError("[VISIBILITY] " + statusText);
    }
}

std::string AtlasVisibilityService::getStatusText() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return statusText;
}

void AtlasVisibilityService::visibilityTask()
{
    if (!atlasVisibilityGlobals::enabled)
    {
        clearVisibilityFlags();
        return;
    }

    if (!isFactoryLocation(mainGame.selectedLocation))
    {
        clearVisibilityFlags();
        return;
    }

    startFactoryLoad();
    pollFactoryLoad();

    std::shared_ptr<const CollisionMap> map;

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        map = factoryMap;
    }

    if (!map)
        return;

    const PlayerSnapshot snapshot = registeredPlayers.getCacheSnapshot();
    const Player* localPlayer = nullptr;

    for (const Player& player : *snapshot)
    {
        if (player.isLocal)
        {
            localPlayer = &player;
            break;
        }
    }

    if (!localPlayer || !isFiniteVector(localPlayer->location))
    {
        clearVisibilityFlags();
        return;
    }

    const glm::vec3 origin = getLocalEyePosition(*localPlayer);
    std::vector<PlayerVisibilityEdit> edits;
    edits.reserve(snapshot->size());

    for (const Player& player : *snapshot)
    {
        if (player.isLocal || player.isBTR || player.isDead || player.hasExfiled || player.isZombie || !Utils::valid_pointer(player.instance))
        {
            if (!player.isLocal)
                edits.push_back({ player.instance, false });

            continue;
        }

        if (player.distance <= 0 || player.distance > atlasVisibilityGlobals::maxDistance || !isFiniteVector(player.location))
        {
            edits.push_back({ player.instance, false });
            continue;
        }

        const std::array<glm::vec3, 3> targets = getPlayerVisibilityTargets(player);
        bool visible = false;

        for (const glm::vec3& target : targets)
        {
            if (map->hasLineOfSight(origin, target))
            {
                visible = true;
                break;
            }
        }

        edits.push_back({ player.instance, visible });
    }

    registeredPlayers.applyVisibilityEdits(edits);
}
