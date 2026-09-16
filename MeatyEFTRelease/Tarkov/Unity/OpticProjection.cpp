#include "OpticProjection.h"

// Credits to "The Beast" on our discord for idea and explaining on his UC post

#include "UnityOffsets.h"
#include "../SDK/EftOffsets.h"
#include "../../Core/Utilities.h"
#include "../../memory/Memory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{
constexpr float kClipW = 0.001f;
constexpr std::size_t kMaxTransformDepth = 64;
constexpr std::uint32_t kMaxTransformIndex = 65536;

struct RawCameraSample
{
    glm::mat4 view{};
    glm::mat4 activeProjection{};
    glm::mat4 nonJitteredProjection{};
    std::uint8_t nonJitteredSet = 0;
};

struct RendererBounds
{
    glm::vec3 center{};
    glm::vec3 extents{};
};

[[nodiscard]] bool validPointer(std::uint64_t value)
{
    return Utils::valid_pointer(value);
}

template <typename T> [[nodiscard]] bool readValue(std::uint64_t address, T& value)
{
    return mem.TryRead(address, value, DmaCacheMode::Uncached);
}

[[nodiscard]] bool readPointer(std::uint64_t address, std::uint64_t& value)
{
    return readValue(address, value) && validPointer(value);
}

[[nodiscard]] bool readBytes(std::uint64_t address, void* destination, std::size_t size)
{
    return validPointer(address) && destination && size > 0 && mem.Read(address, destination, size, DmaCacheMode::Uncached, "Optic projection");
}

[[nodiscard]] bool readStaticMeshBytes(std::uint64_t address, void* destination, std::size_t size)
{
    if (!validPointer(address) || !destination || size == 0)
        return false;

    if (mem.Read(address, destination, size, DmaCacheMode::Uncached, "Optic static mesh"))
    {
        return true;
    }

    return mem.Read(address, destination, size, DmaCacheMode::Cached, "Optic static mesh retry");
}

[[nodiscard]] bool finite(const glm::vec2& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool finite(const glm::vec4& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool adjacentLensSamplesAgree(const OpticProjectionState& left, const OpticProjectionState& right)
{
    constexpr float kQuadNdcTolerance = 0.0030f;
    constexpr float kBoundsNdcTolerance = 0.0030f;
    constexpr float kCameraRelativeToleranceM = 0.0040f;

    if (!left.valid || !right.valid || left.opticSight != right.opticSight || left.lensRenderer != right.lensRenderer || left.mesh != right.mesh ||
        left.material != right.material)
    {
        return false;
    }

    for (std::size_t index = 0; index < left.imageQuad.size(); ++index)
    {
        if (glm::length(left.imageQuad[index] - right.imageQuad[index]) > kQuadNdcTolerance)
        {
            return false;
        }
    }

    return glm::length(left.imageCenterNdc - right.imageCenterNdc) <= kQuadNdcTolerance &&
           glm::length(left.lensBoundsCenterNdc - right.lensBoundsCenterNdc) <= kBoundsNdcTolerance &&
           glm::length(left.lensBoundsRadiiNdc - right.lensBoundsRadiiNdc) <= kBoundsNdcTolerance &&
           glm::length(left.mainCameraRelativeLens - right.mainCameraRelativeLens) <= kCameraRelativeToleranceM &&
           glm::length(left.mainCameraRelativeHands - right.mainCameraRelativeHands) <= kCameraRelativeToleranceM;
}

[[nodiscard]] bool matrixLooksValid(const glm::mat4& matrix)
{
    constexpr float kMaximum = 100000.0f;
    constexpr float kEpsilon = 0.000001f;
    std::array<bool, 4> rows{};
    std::array<bool, 4> columns{};

    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            const float value = matrix[column][row];
            if (!std::isfinite(value) || std::fabs(value) > kMaximum)
                return false;
            if (std::fabs(value) > kEpsilon)
            {
                rows[static_cast<std::size_t>(row)] = true;
                columns[static_cast<std::size_t>(column)] = true;
            }
        }
    }

    return std::all_of(rows.begin(), rows.end(), [](bool value) { return value; }) &&
           std::all_of(columns.begin(), columns.end(), [](bool value) { return value; });
}

[[nodiscard]] bool makeCameraSample(const RawCameraSample& raw, CameraMatrixSample& output)
{
    output = {};
    const glm::mat4& projection = raw.nonJitteredSet != 0 ? raw.nonJitteredProjection : raw.activeProjection;

    if (!matrixLooksValid(raw.view) || !matrixLooksValid(projection))
        return false;

    const glm::mat4 viewProjection = projection * raw.view;
    if (!matrixLooksValid(viewProjection))
        return false;

    output.view = raw.view;
    output.projection = projection;
    output.viewProjection = viewProjection;
    output.valid = true;
    return true;
}

[[nodiscard]] std::uint32_t instanceHash(std::uint32_t id)
{
    std::uint32_t value = id * 0x1001u + 0x7ED55D16u;
    value = value ^ (value >> 19) ^ 0xC761C23Cu;
    value = value * 0x21u + 0x165667B1u;
    value = (value + 0xD3A2646Cu) ^ (value << 9);
    value = value * 9u + 0xFD7046C5u;
    return value ^ (value >> 16) ^ 0xB55A4F09u;
}

[[nodiscard]] std::uint64_t resolveInstance(std::uint64_t unityPlayer, std::uint32_t id)
{
    if (!validPointer(unityPlayer) || id == 0)
        return 0;

    std::uint64_t root = 0;
    std::uint64_t table = 0;
    std::uint32_t mask = 0;

    if (!readPointer(unityPlayer + UnityOffsets::InstanceTable, root) || !readPointer(root, table) || !readValue(root + 0x8, mask) || mask < 8 ||
        mask > (1u << 27) || (mask & 7u) != 0)
    {
        return 0;
    }

    const std::uint32_t capacityMinusOne = mask / 8u;
    if ((capacityMinusOne & (capacityMinusOne + 1u)) != 0)
        return 0;

    const std::uint32_t hash = instanceHash(id);
    std::uint32_t index = hash & mask;

    for (std::uint32_t step = 8; step <= 512; step += 8)
    {
        const std::uint64_t entry = table + static_cast<std::uint64_t>(index) * 3ull;
        std::uint32_t storedHash = 0;
        std::uint32_t storedId = 0;

        if (!readValue(entry, storedHash))
            return 0;
        if (storedHash == 0xFFFFFFFFu)
            break;

        if (storedHash == (hash & 0xFFFFFFFCu) && readValue(entry + 0x8, storedId) && storedId == id)
        {
            std::uint64_t instance = 0;
            return readPointer(entry + 0x10, instance) ? instance : 0;
        }

        index = (index + step) & mask;
    }

    return 0;
}

[[nodiscard]] std::uint64_t resolveComponent(std::uint64_t gameObject, std::uint64_t descriptor, std::uint64_t limit = 64)
{
    std::uint64_t entries = 0;
    std::uint64_t count = 0;
    std::uint32_t firstType = 0;
    std::uint32_t typeCount = 0;

    if (!readPointer(gameObject + UnityOffsets::NativeGameObject_ComponentArrayOffset, entries) ||
        !readValue(gameObject + UnityOffsets::NativeGameObject_ComponentCountOffset, count) || count == 0 || count > limit ||
        !readValue(descriptor + 0x30, firstType) || !readValue(descriptor + 0x34, typeCount) || typeCount == 0 || typeCount > 1024)
    {
        return 0;
    }

    for (std::uint64_t index = 0; index < count; ++index)
    {
        const std::uint64_t entry = entries + index * 16;
        std::uint32_t type = 0;
        if (!readValue(entry, type))
            return 0;

        if (static_cast<std::uint32_t>(type - firstType) < typeCount)
        {
            std::uint64_t component = 0;
            return readPointer(entry + 0x8, component) ? component : 0;
        }
    }

    return 0;
}

[[nodiscard]] bool resolveTransformChain(std::uint64_t nativeTransform, std::vector<std::uint64_t>& output)
{
    output.clear();
    std::uint64_t hierarchy = 0;
    std::uint64_t parents = 0;
    std::uint64_t nodes = 0;
    std::uint32_t index = 0;

    if (!readPointer(nativeTransform + 0x70, hierarchy) || !readValue(nativeTransform + 0x78, index) || index >= kMaxTransformIndex ||
        !readPointer(hierarchy + 0x40, parents) || !readPointer(hierarchy + 0x68, nodes))
    {
        return false;
    }

    std::array<std::uint32_t, kMaxTransformDepth> seen{};
    std::size_t seenCount = 0;

    while (index != 0xFFFFFFFFu)
    {
        if (index >= kMaxTransformIndex || seenCount >= seen.size() ||
            std::find(seen.begin(), seen.begin() + static_cast<std::ptrdiff_t>(seenCount), index) != seen.begin() + static_cast<std::ptrdiff_t>(seenCount))
        {
            output.clear();
            return false;
        }

        seen[seenCount++] = index;
        output.push_back(nodes + static_cast<std::uint64_t>(index) * 48ull);

        if (!readValue(parents + static_cast<std::uint64_t>(index) * 4ull, index))
        {
            output.clear();
            return false;
        }
    }

    return !output.empty();
}

[[nodiscard]] std::uint64_t transformNodeAddress(std::uint64_t nativeTransform)
{
    std::uint64_t hierarchy = 0;
    std::uint64_t nodes = 0;
    std::uint32_t index = 0;
    if (!readPointer(nativeTransform + 0x70, hierarchy) || !readValue(nativeTransform + 0x78, index) || index >= kMaxTransformIndex ||
        !readPointer(hierarchy + 0x68, nodes))
    {
        return 0;
    }
    return nodes + static_cast<std::uint64_t>(index) * 48ull;
}

[[nodiscard]] bool managedTransformToNative(std::uint64_t managedTransform, std::uint64_t& nativeTransform)
{
    nativeTransform = 0;
    return readPointer(managedTransform + 0x10, nativeTransform);
}

[[nodiscard]] float cross2(const glm::vec2& left, const glm::vec2& right)
{
    return left.x * right.y - left.y * right.x;
}

[[nodiscard]] float polygonSignedArea(const std::vector<glm::vec2>& polygon)
{
    if (polygon.size() < 3)
        return 0.0f;
    double area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index)
    {
        const glm::vec2& current = polygon[index];
        const glm::vec2& next = polygon[(index + 1) % polygon.size()];
        area += static_cast<double>(current.x) * next.y - static_cast<double>(current.y) * next.x;
    }
    return static_cast<float>(area * 0.5);
}

void ensureCounterClockwise(std::vector<glm::vec2>& polygon)
{
    if (polygonSignedArea(polygon) < 0.0f)
        std::reverse(polygon.begin(), polygon.end());
}

[[nodiscard]] std::vector<glm::vec2> clipPolygonEdge(const std::vector<glm::vec2>& subject, const glm::vec2& edgeStart, const glm::vec2& edgeEnd)
{
    std::vector<glm::vec2> output;
    if (subject.empty())
        return output;

    const glm::vec2 edge = edgeEnd - edgeStart;
    auto inside = [&](const glm::vec2& point) { return cross2(edge, point - edgeStart) >= -1.0e-8f; };

    glm::vec2 previous = subject.back();
    bool previousInside = inside(previous);
    for (const glm::vec2& current : subject)
    {
        const bool currentInside = inside(current);
        if (currentInside != previousInside)
        {
            const glm::vec2 direction = current - previous;
            const float denominator = cross2(edge, direction);
            if (std::fabs(denominator) > 1.0e-12f)
            {
                const float amount = -cross2(edge, previous - edgeStart) / denominator;
                output.push_back(previous + direction * amount);
            }
        }
        if (currentInside)
            output.push_back(current);
        previous = current;
        previousInside = currentInside;
    }
    return output;
}

[[nodiscard]] std::vector<glm::vec2> clipPolygon(std::vector<glm::vec2> subject, std::vector<glm::vec2> clipper)
{
    ensureCounterClockwise(subject);
    ensureCounterClockwise(clipper);
    for (std::size_t index = 0; index < clipper.size() && !subject.empty(); ++index)
    {
        subject = clipPolygonEdge(subject, clipper[index], clipper[(index + 1) % clipper.size()]);
    }
    return subject;
}

[[nodiscard]] std::vector<glm::vec2> clipToUvSquare(const std::array<glm::vec2, 3>& triangle)
{
    std::vector<glm::vec2> polygon(triangle.begin(), triangle.end());
    std::vector<glm::vec2> square = {{0.45f, 0.45f}, {0.55f, 0.45f}, {0.55f, 0.55f}, {0.45f, 0.55f}};
    return clipPolygon(std::move(polygon), std::move(square));
}

[[nodiscard]] bool interpolateTriangle(const glm::vec2& uv, const std::array<glm::vec2, 3>& triangleUv, const std::array<glm::vec3, 3>& trianglePosition,
                                       glm::vec3& output)
{
    const glm::vec2 uvB = triangleUv[1] - triangleUv[0];
    const glm::vec2 uvC = triangleUv[2] - triangleUv[0];
    const float denominator = cross2(uvB, uvC);
    if (std::fabs(denominator) < 1.0e-12f)
        return false;

    const float weightB = cross2(uv - triangleUv[0], uvC) / denominator;
    const float weightC = cross2(uvB, uv - triangleUv[0]) / denominator;
    const float weightA = 1.0f - weightB - weightC;
    output = trianglePosition[0] * weightA + trianglePosition[1] * weightB + trianglePosition[2] * weightC;
    return finite(output);
}

struct MeshChannel
{
    std::uint8_t stream = 0;
    std::uint8_t offset = 0;
    std::uint8_t format = 0;
    std::uint8_t dimension = 0;
};

[[nodiscard]] float decodeHalf(std::uint16_t half)
{
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    std::uint32_t mantissa = half & 0x03FFu;
    std::uint32_t bits = 0;

    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign;
        }
        else
        {
            int unbiasedExponent = -14;
            while ((mantissa & 0x0400u) == 0)
            {
                mantissa <<= 1;
                --unbiasedExponent;
            }
            mantissa &= 0x03FFu;
            bits = sign | (static_cast<std::uint32_t>(unbiasedExponent + 127) << 23) | (mantissa << 13);
        }
    }
    else if (exponent == 0x1Fu)
    {
        bits = sign | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }

    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

[[nodiscard]] bool readMeshChannel(std::uint64_t data, std::uint32_t vertexCount, std::uint32_t channel, std::uint8_t wantedDimension,
                                   std::vector<float>& values, std::string& diagnostic)
{
    diagnostic.clear();
    MeshChannel descriptor{};
    if (!readBytes(data + 0x8 + static_cast<std::uint64_t>(channel) * 4, &descriptor, sizeof(descriptor)))
    {
        diagnostic = "descriptor read";
        return false;
    }

    descriptor.dimension &= 0x0Fu;
    const std::size_t componentSize = descriptor.format == 0 ? sizeof(float) : descriptor.format == 1 ? sizeof(std::uint16_t) : 0;
    if (descriptor.stream > 3 || componentSize == 0 || descriptor.dimension != wantedDimension)
    {
        diagnostic = "stream=" + std::to_string(descriptor.stream) + " format=" + std::to_string(descriptor.format) +
                     " dimension=" + std::to_string(descriptor.dimension);
        return false;
    }

    const std::uint64_t streamInfo = data + 0x40 + static_cast<std::uint64_t>(descriptor.stream) * 12;
    std::uint32_t channelMask = 0;
    std::uint32_t streamOffset = 0;
    std::uint8_t stride = 0;
    if (!readValue(streamInfo, channelMask) || !readValue(streamInfo + 0x4, streamOffset) || !readValue(streamInfo + 0x8, stride) ||
        (channelMask & (1u << channel)) == 0 || stride < descriptor.offset + descriptor.dimension * componentSize)
    {
        diagnostic = "stream layout mask=" + std::to_string(channelMask) + " stride=" + std::to_string(stride) + " offset=" + std::to_string(descriptor.offset);
        return false;
    }

    std::uint64_t vertexData = 0;
    if (!readPointer(data + 0xA0, vertexData))
    {
        diagnostic = "vertex buffer pointer";
        return false;
    }

    const std::size_t byteCount = static_cast<std::size_t>(vertexCount) * stride;
    if (byteCount == 0 || byteCount > 4u * 1024u * 1024u)
    {
        diagnostic = "vertex buffer size=" + std::to_string(byteCount);
        return false;
    }

    std::vector<std::uint8_t> buffer(byteCount);
    const std::uint64_t streamAddress = vertexData + streamOffset;
    if (!readStaticMeshBytes(streamAddress, buffer.data(), buffer.size()))
    {
        constexpr std::size_t kBatchSize = Memory::MaxDmaScatterRequests;
        const std::size_t channelBytes = static_cast<std::size_t>(wantedDimension) * componentSize;
        bool scatterComplete = true;
        std::size_t failedVertex = 0;

        for (std::size_t firstVertex = 0; firstVertex < vertexCount && scatterComplete; firstVertex += kBatchSize)
        {
            const std::size_t count = (std::min)(kBatchSize, static_cast<std::size_t>(vertexCount) - firstVertex);
            std::vector<Memory::ScatterReadRequest> requests;
            requests.reserve(count);

            for (std::size_t item = 0; item < count; ++item)
            {
                const std::size_t vertex = firstVertex + item;
                const std::size_t destinationOffset = vertex * stride + descriptor.offset;
                requests.push_back({streamAddress + destinationOffset, buffer.data() + destinationOffset, channelBytes});
            }

            std::vector<DWORD> bytesRead(count);
            const auto result = mem.TryReadScatter(requests.data(), requests.size(), DmaCacheMode::Cached, "Optic mesh channel fallback", bytesRead);
            if (result != Memory::TryScatterReadResult::Success)
            {
                scatterComplete = false;
                failedVertex = firstVertex;
                break;
            }

            for (std::size_t item = 0; item < count; ++item)
            {
                if (bytesRead[item] != requests[item].size)
                {
                    scatterComplete = false;
                    failedVertex = firstVertex + item;
                    break;
                }
            }
        }

        if (!scatterComplete)
        {
            diagnostic = "vertex buffer read address=" + std::to_string(streamAddress) + " bytes=" + std::to_string(byteCount) +
                         " stride=" + std::to_string(stride) + " streamOffset=" + std::to_string(streamOffset) +
                         " failedVertex=" + std::to_string(failedVertex);
            return false;
        }
    }

    values.resize(static_cast<std::size_t>(vertexCount) * wantedDimension);
    for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex)
    {
        const std::size_t source = static_cast<std::size_t>(vertex) * stride + descriptor.offset;
        for (std::uint8_t component = 0; component < wantedDimension; ++component)
        {
            float value = 0.0f;
            const std::size_t componentOffset = source + static_cast<std::size_t>(component) * componentSize;
            if (descriptor.format == 0)
            {
                std::memcpy(&value, buffer.data() + componentOffset, sizeof(value));
            }
            else
            {
                std::uint16_t half = 0;
                std::memcpy(&half, buffer.data() + componentOffset, sizeof(half));
                value = decodeHalf(half);
            }
            if (!std::isfinite(value))
            {
                diagnostic = "non-finite component at vertex=" + std::to_string(vertex);
                return false;
            }
            values[static_cast<std::size_t>(vertex) * wantedDimension + component] = value;
        }
    }
    return true;
}

[[nodiscard]] bool decodeMeshMap(std::uint64_t mesh, OpticProjectionEngine::MeshMap& output, std::string& diagnostic)
{
    output = {};
    diagnostic.clear();
    std::uint64_t data = 0;
    std::uint32_t vertexCount = 0;
    std::uint64_t submeshCount = 0;
    if (!readPointer(mesh + 0x60, data) || !readValue(data + 0x78, vertexCount) || vertexCount < 3 || vertexCount > 4096 ||
        !readValue(data + 0xE0, submeshCount) || submeshCount != 1)
    {
        diagnostic = "mesh header/layout";
        return false;
    }

    std::vector<float> positionValues;
    std::vector<float> uvValues;
    std::string channelDiagnostic;
    if (!readMeshChannel(data, vertexCount, 0, 3, positionValues, channelDiagnostic))
    {
        diagnostic = "position channel: " + channelDiagnostic;
        return false;
    }
    if (!readMeshChannel(data, vertexCount, 4, 2, uvValues, channelDiagnostic))
    {
        diagnostic = "UV0 channel: " + channelDiagnostic;
        return false;
    }

    std::vector<glm::vec3> positions(vertexCount);
    std::vector<glm::vec2> uvs(vertexCount);
    for (std::uint32_t index = 0; index < vertexCount; ++index)
    {
        positions[index] = {positionValues[static_cast<std::size_t>(index) * 3], positionValues[static_cast<std::size_t>(index) * 3 + 1],
                            positionValues[static_cast<std::size_t>(index) * 3 + 2]};
        uvs[index] = {uvValues[static_cast<std::size_t>(index) * 2], uvValues[static_cast<std::size_t>(index) * 2 + 1]};
    }

    std::uint64_t submesh = 0;
    std::uint32_t firstByte = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t topology = 0;
    std::uint32_t baseVertex = 0;
    std::uint32_t indexFormat = 0;
    std::uint64_t indexBufferSize = 0;
    std::uint64_t indexBuffer = 0;
    if (!readPointer(data + 0xD0, submesh) || !readValue(submesh + 0x20, firstByte) || !readValue(submesh + 0x24, indexCount) ||
        !readValue(submesh + 0x28, topology) || !readValue(submesh + 0x2C, baseVertex) || !readValue(data + 0xC8, indexFormat) ||
        !readValue(data + 0xB8, indexBufferSize) || !readPointer(data + 0xA8, indexBuffer) || topology != 0 || indexFormat > 1 || indexCount == 0 ||
        indexCount > 24576 || indexCount % 3 != 0)
    {
        diagnostic = "submesh/index-buffer layout";
        return false;
    }

    const std::size_t indexSize = indexFormat == 0 ? 2u : 4u;
    const std::uint64_t required = static_cast<std::uint64_t>(firstByte) + static_cast<std::uint64_t>(indexCount) * indexSize;
    if (required > indexBufferSize)
    {
        diagnostic = "index range exceeds buffer";
        return false;
    }

    std::vector<std::uint8_t> indexBytes(static_cast<std::size_t>(indexCount) * indexSize);
    if (!readStaticMeshBytes(indexBuffer + firstByte, indexBytes.data(), indexBytes.size()))
    {
        diagnostic = "index-buffer read";
        return false;
    }

    std::vector<std::uint32_t> indices(indexCount);
    for (std::uint32_t index = 0; index < indexCount; ++index)
    {
        std::uint32_t decoded = 0;
        std::memcpy(&decoded, indexBytes.data() + static_cast<std::size_t>(index) * indexSize, indexSize);
        decoded += baseVertex;
        if (decoded >= vertexCount)
        {
            diagnostic = "decoded index exceeds vertex count";
            return false;
        }
        indices[index] = decoded;
    }

    struct Piece
    {
        std::vector<glm::vec2> polygon;
        std::array<glm::vec2, 3> uv{};
        std::array<glm::vec3, 3> position{};
    };
    std::vector<Piece> pieces;
    double totalArea = 0.0;

    for (std::size_t index = 0; index < indices.size(); index += 3)
    {
        Piece piece{};
        for (int corner = 0; corner < 3; ++corner)
        {
            const std::uint32_t vertex = indices[index + corner];
            piece.uv[corner] = uvs[vertex];
            piece.position[corner] = positions[vertex];
        }
        piece.polygon = clipToUvSquare(piece.uv);
        const float area = std::fabs(polygonSignedArea(piece.polygon));
        if (piece.polygon.size() >= 3 && area > 1.0e-12f)
        {
            totalArea += area;
            pieces.push_back(std::move(piece));
        }
    }

    if (pieces.empty() || std::fabs(totalArea - 0.01) > 1.0e-6)
    {
        diagnostic = "UV square coverage area=" + std::to_string(totalArea) + " pieces=" + std::to_string(pieces.size());
        return false;
    }

    const Piece& seed = pieces.front();
    const glm::vec2 uvB = seed.uv[1] - seed.uv[0];
    const glm::vec2 uvC = seed.uv[2] - seed.uv[0];
    const float denominator = cross2(uvB, uvC);
    if (std::fabs(denominator) < 1.0e-12f)
    {
        diagnostic = "degenerate seed UV triangle";
        return false;
    }

    output.uAxis = ((seed.position[1] - seed.position[0]) * uvC.y - (seed.position[2] - seed.position[0]) * uvB.y) / denominator;
    output.vAxis = ((seed.position[2] - seed.position[0]) * uvB.x - (seed.position[1] - seed.position[0]) * uvC.x) / denominator;
    output.center = seed.position[0] + output.uAxis * (0.5f - seed.uv[0].x) + output.vAxis * (0.5f - seed.uv[0].y);

    if (!finite(output.center) || !finite(output.uAxis) || !finite(output.vAxis) || glm::length(glm::cross(output.uAxis, output.vAxis)) <= 1.0e-10f)
    {
        diagnostic = "degenerate local lens surface";
        return false;
    }

    for (const Piece& piece : pieces)
    {
        for (const glm::vec2& uv : piece.polygon)
        {
            glm::vec3 expected{};
            if (!interpolateTriangle(uv, piece.uv, piece.position, expected))
            {
                diagnostic = "degenerate intersecting UV triangle";
                return false;
            }
            const glm::vec3 mapped = output.center + output.uAxis * (uv.x - 0.5f) + output.vAxis * (uv.y - 0.5f);
            const float mappingError = glm::length(mapped - expected);
            if (mappingError > 1.0e-5f)
            {
                diagnostic = "non-affine UV map error=" + std::to_string(mappingError);
                return false;
            }
        }
    }

    for (std::size_t left = 0; left < pieces.size(); ++left)
    {
        for (std::size_t right = left + 1; right < pieces.size(); ++right)
        {
            std::vector<glm::vec2> overlap = clipPolygon(pieces[left].polygon, pieces[right].polygon);
            const float overlapArea = std::fabs(polygonSignedArea(overlap));
            if (overlapArea > 1.0e-8f)
            {
                diagnostic = "overlapping UV pieces area=" + std::to_string(overlapArea);
                return false;
            }
        }
    }

    output.valid = true;
    diagnostic.clear();
    return true;
}

[[nodiscard]] std::string readCString(std::uint64_t address, std::size_t maximum)
{
    if (!validPointer(address) || maximum == 0)
        return {};
    std::vector<char> bytes(maximum);
    if (!readBytes(address, bytes.data(), bytes.size()))
        return {};
    std::size_t length = 0;
    while (length < bytes.size() && bytes[length] != '\0')
    {
        const unsigned char value = static_cast<unsigned char>(bytes[length]);
        if (value < 0x20 || value > 0x7E)
            return {};
        ++length;
    }
    if (length == bytes.size())
        return {};
    return std::string(bytes.data(), length);
}

[[nodiscard]] std::string shaderString(std::uint64_t address)
{
    std::uint8_t local = 0;
    if (!readValue(address + 0x20, local))
        return {};

    std::uint64_t data = address;
    std::uint64_t length = 0;
    if (local == 1)
    {
        std::int8_t encodedLength = 0;
        if (!readValue(address + 0x18, encodedLength))
            return {};
        const int decoded = 24 - static_cast<int>(encodedLength);
        if (decoded < 0 || decoded > 24)
            return {};
        length = static_cast<std::uint64_t>(decoded);
    }
    else
    {
        if (!readValue(address + 0x10, length) || length > 128)
            return {};
        if (length > 0 && !readPointer(address, data))
            return {};
    }

    if (length == 0)
        return {};
    std::vector<char> bytes(static_cast<std::size_t>(length));
    if (!readBytes(data, bytes.data(), bytes.size()))
        return {};
    for (const char byte : bytes)
    {
        const unsigned char value = static_cast<unsigned char>(byte);
        if (value < 0x20 || value > 0x7E)
            return {};
    }
    return std::string(bytes.begin(), bytes.end());
}

[[nodiscard]] std::string propertyName(std::uint64_t unityPlayer, std::uint32_t id)
{
    const std::uint32_t tag = id >> 30;
    const std::uint32_t index = id & 0x3FFFFFFFu;
    std::uint64_t slot = 0;

    if (tag == 0)
    {
        std::uint64_t root = 0;
        std::uint64_t slots = 0;
        std::uint32_t count = 0;
        if (!readPointer(unityPlayer + UnityOffsets::PropertyNameRegistry, root) || !readValue(root + 0x10, count) || index >= count || index > (1u << 20) ||
            !readPointer(root, slots))
        {
            return {};
        }
        slot = slots + static_cast<std::uint64_t>(index) * 8ull;
    }
    else
    {
        constexpr std::array<std::uint64_t, 4> tables = {0, UnityOffsets::PropertyNameTable1, UnityOffsets::PropertyNameTable2,
                                                         UnityOffsets::PropertyNameTable3};
        constexpr std::array<std::uint32_t, 4> last = {0, 122, 20, 25};
        if (tag > 3 || index > last[tag])
            return {};
        slot = unityPlayer + tables[tag] + static_cast<std::uint64_t>(index) * 8ull;
    }

    std::uint64_t stringAddress = 0;
    return readPointer(slot, stringAddress) ? readCString(stringAddress, 128) : std::string{};
}

[[nodiscard]] bool resolveMaterialVectors(std::uint64_t unityPlayer, std::uint64_t material, std::uint64_t& scales, std::uint64_t& shifts,
                                          std::uint64_t& shiftDirection)
{
    scales = shifts = shiftDirection = 0;
    std::uint64_t sheetRoot = 0;
    if (!readPointer(material + 0x108, sheetRoot))
        return false;
    const std::uint64_t sheet = sheetRoot + 0x60;

    std::uint64_t ids = 0;
    std::uint64_t descriptors = 0;
    std::uint64_t values = 0;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    std::int32_t bias = 0;
    if (!readPointer(sheet + 0x10, ids) || !readPointer(sheet + 0x30, descriptors) || !readPointer(sheet + 0x50, values) || !readValue(sheet + 0x80, begin) ||
        !readValue(sheet + 0x88, end) || !readValue(sheet + 0xC0, bias) || begin > end || end > 256)
    {
        return false;
    }

    for (std::uint64_t index = begin; index < end; ++index)
    {
        std::uint32_t id = 0;
        if (!readValue(ids + index * 4ull, id))
            return false;
        const std::string name = propertyName(unityPlayer, id);
        if (name != "_Scales" && name != "_Shifts" && name != "_ShiftDirection")
            continue;

        std::uint64_t descriptor = 0;
        if (!readValue(descriptors + index * 8ull, descriptor) || (descriptor & (1ull << 40)) != 0)
        {
            return false;
        }
        const std::int64_t offset = static_cast<std::int64_t>(bias) + static_cast<std::int64_t>(descriptor & 0xFFFFFull);
        if (offset < 0 || offset > (1ll << 20))
            return false;
        const std::uint64_t address = values + static_cast<std::uint64_t>(offset);
        if (name == "_Scales")
            scales = address;
        else if (name == "_Shifts")
            shifts = address;
        else
            shiftDirection = address;
    }

    return validPointer(scales) && validPointer(shifts) && validPointer(shiftDirection);
}

[[nodiscard]] glm::mat4 composeTransform(const std::vector<std::uint64_t>& chain, const std::vector<UnityTransform::TrsX>& nodes,
                                         const std::unordered_set<std::uint64_t>* fovNodes, float intendedScale, std::size_t firstNode = 0)
{
    if (chain.size() != nodes.size() || firstNode >= chain.size())
        return glm::mat4(0.0f);

    glm::mat4 matrix(1.0f);
    for (std::size_t index = firstNode; index < chain.size(); ++index)
    {
        glm::vec3 position = nodes[index].t;
        glm::quat rotation = nodes[index].q;
        glm::vec3 scale = nodes[index].s;
        if (!finite(position) || !finite(scale) || !std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
            !std::isfinite(rotation.w))
        {
            return glm::mat4(0.0f);
        }

        const float rotationLength = glm::dot(rotation, rotation);
        if (rotationLength < 0.5f || rotationLength > 1.5f)
            return glm::mat4(0.0f);
        rotation = glm::normalize(rotation);

        if (fovNodes && fovNodes->contains(chain[index]))
        {
            if (std::fabs(scale.x - 1.0f) > 0.001f || std::fabs(scale.y - 1.0f) > 0.001f || intendedScale < 0.1f || intendedScale > 3.0f || scale.z < 0.1f ||
                scale.z > 3.0f)
            {
                return glm::mat4(0.0f);
            }
            scale.z = intendedScale;
        }

        const glm::mat4 trs = glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
        matrix = trs * matrix;
    }
    return matrix;
}

[[nodiscard]] bool cameraConsistent(const glm::mat4& view, const std::vector<std::uint64_t>& chain, const std::vector<UnityTransform::TrsX>& nodes)
{
    const glm::mat4 world = composeTransform(chain, nodes, nullptr, 1.0f);
    if (!matrixLooksValid(world))
        return false;

    const glm::vec3 cameraPosition = glm::vec3(world[3]);
    const glm::vec4 origin = view * glm::vec4(cameraPosition, 1.0f);
    if (!finite(origin) || glm::length(glm::vec3(origin)) > 0.001f)
        return false;

    glm::quat worldRotation(1.0f, 0.0f, 0.0f, 0.0f);
    for (std::size_t index = 0; index < nodes.size(); ++index)
        worldRotation = glm::normalize(nodes[index].q) * worldRotation;

    const std::array<glm::vec3, 3> transformAxes = {worldRotation * glm::vec3(1.0f, 0.0f, 0.0f), worldRotation * glm::vec3(0.0f, 1.0f, 0.0f),
                                                    worldRotation * glm::vec3(0.0f, 0.0f, 1.0f)};
    const std::array<glm::vec3, 3> viewAxes = {glm::vec3(view[0][0], view[1][0], view[2][0]), glm::vec3(view[0][1], view[1][1], view[2][1]),
                                               -glm::vec3(view[0][2], view[1][2], view[2][2])};

    constexpr float kRadians = 0.05f * 0.01745329251994329577f;
    for (std::size_t index = 0; index < transformAxes.size(); ++index)
    {
        const glm::vec3 left = glm::normalize(transformAxes[index]);
        const glm::vec3 right = glm::normalize(viewAxes[index]);
        const float angle = std::atan2(glm::length(glm::cross(left, right)), glm::dot(left, right));
        if (!std::isfinite(angle) || angle > kRadians)
            return false;
    }
    return true;
}

[[nodiscard]] bool clipSegmentToConvex(const glm::vec2& start, const glm::vec2& end, const std::vector<glm::vec2>& mask, float& low, float& high)
{
    low = 0.0f;
    high = 1.0f;
    if (mask.size() < 3)
        return false;

    for (std::size_t index = 0; index < mask.size(); ++index)
    {
        const glm::vec2& a = mask[index];
        const glm::vec2& b = mask[(index + 1) % mask.size()];
        const float initial = cross2(b - a, start - a);
        const float slope = cross2(b - a, end - start);
        if (std::fabs(slope) < 1.0e-12f)
        {
            if (initial < 0.0f)
                return false;
            continue;
        }

        const float amount = -initial / slope;
        if (slope > 0.0f)
            low = (std::max)(low, amount);
        else
            high = (std::min)(high, amount);
        if (low > high)
            return false;
    }
    return true;
}

[[nodiscard]] bool clipHomogeneousNear(const glm::mat4& matrix, glm::vec4& worldStart, glm::vec4& worldEnd)
{
    glm::vec4 clipStart = matrix * worldStart;
    glm::vec4 clipEnd = matrix * worldEnd;
    if (!finite(clipStart) || !finite(clipEnd) || (clipStart.w <= kClipW && clipEnd.w <= kClipW))
    {
        return false;
    }

    if (clipStart.w <= kClipW)
    {
        const float amount = (kClipW - clipStart.w) / (clipEnd.w - clipStart.w);
        worldStart = glm::mix(worldStart, worldEnd, amount);
    }
    else if (clipEnd.w <= kClipW)
    {
        const float amount = (kClipW - clipStart.w) / (clipEnd.w - clipStart.w);
        worldEnd = glm::mix(worldStart, worldEnd, amount);
    }
    return true;
}

[[nodiscard]] glm::vec2 toPixels(const glm::vec2& ndc, float width, float height)
{
    return {(ndc.x + 1.0f) * width * 0.5f, (1.0f - ndc.y) * height * 0.5f};
}

void addPixelSegment(const glm::vec2& start, const glm::vec2& end, float low, float high, float width, float height, std::vector<CameraScreenSegment>& output)
{
    if (high - low <= 1.0e-6f)
        return;
    output.push_back({toPixels(glm::mix(start, end, low), width, height), toPixels(glm::mix(start, end, high), width, height)});
}
}

bool OpticProjectionEngine::readCamera(std::uint64_t nativeCamera, CameraMatrixSample& out, bool* busy)
{
    out = {};
    if (busy)
        *busy = false;
    if (!validPointer(nativeCamera))
        return false;

    RawCameraSample raw{};
    Memory::ScatterReadRequest requests[] = {
        {nativeCamera + UnityOffsets::Camera_WorldToCameraMatrixOffset, &raw.view, sizeof(raw.view)},
        {nativeCamera + UnityOffsets::Camera_ProjectionMatrixOffset, &raw.activeProjection, sizeof(raw.activeProjection)},
        {nativeCamera + UnityOffsets::Camera_NonJitteredProjectionSetOffset, &raw.nonJitteredSet, sizeof(raw.nonJitteredSet)},
        {nativeCamera + UnityOffsets::Camera_NonJitteredProjectionMatrixOffset, &raw.nonJitteredProjection, sizeof(raw.nonJitteredProjection)}};
    DWORD bytesRead[std::size(requests)]{};
    const auto result = mem.TryReadScatter(requests, std::size(requests), DmaCacheMode::Uncached, "Camera VP", bytesRead);
    if (result == Memory::TryScatterReadResult::Busy)
    {
        if (busy)
            *busy = true;
        return false;
    }
    if (result != Memory::TryScatterReadResult::Success)
        return false;
    for (std::size_t index = 0; index < std::size(requests); ++index)
    {
        const bool unusedNonJitteredProjection = index == 3 && raw.nonJitteredSet == 0;
        if (bytesRead[index] != requests[index].size && !unusedNonJitteredProjection)
            return false;
    }
    return makeCameraSample(raw, out);
}

void OpticProjectionEngine::reset()
{
    m_route = {};
    m_failureReason.clear();
    m_sampleSequence = 0;
    m_adjacentCandidate = {};
    m_rootHistory = {};
}

const std::string& OpticProjectionEngine::failureReason() const noexcept
{
    return m_failureReason;
}

OpticProjectionEngine::RootCorrectionDecision OpticProjectionEngine::evaluateRootCorrection(const glm::vec3& measuredCameraLocal,
                                                                                            std::chrono::steady_clock::time_point sampledAt) const
{
    RootCorrectionDecision decision{};
    decision.measuredCameraLocal = measuredCameraLocal;
    decision.sampledAt = sampledAt;

    if (m_rootHistory.trustedCount < 2)
        return decision;

    const RootMotionSample& previous = m_rootHistory.trusted[0];
    const RootMotionSample& latest = m_rootHistory.trusted[1];
    if (previous.sampledAt == std::chrono::steady_clock::time_point{} || latest.sampledAt == std::chrono::steady_clock::time_point{} ||
        latest.sampledAt <= previous.sampledAt || sampledAt < latest.sampledAt)
    {
        decision.resetHistory = true;
        return decision;
    }

    const double historySeconds = std::chrono::duration<double>(latest.sampledAt - previous.sampledAt).count();
    const double predictionSeconds = std::chrono::duration<double>(sampledAt - latest.sampledAt).count();
    constexpr double kMaximumPredictionSeconds = 0.050;
    if (historySeconds < 0.001 || historySeconds > 0.050 || predictionSeconds > kMaximumPredictionSeconds)
    {
        decision.resetHistory = true;
        return decision;
    }

    const glm::vec3 velocity = (latest.cameraLocalPosition - previous.cameraLocalPosition) / static_cast<float>(historySeconds);
    if (!finite(velocity) || glm::length(velocity) > 2.5f)
    {
        decision.resetHistory = true;
        return decision;
    }

    const glm::vec3 predicted = latest.cameraLocalPosition + velocity * static_cast<float>(predictionSeconds);
    const glm::vec3 correction = predicted - measuredCameraLocal;
    const float error = glm::length(correction);
    decision.predictionErrorMm = error * 1000.0f;
    constexpr float kMinimumCorrectionM = 0.0015f;
    constexpr float kMaximumCorrectionM = 0.0300f;
    if (!finite(predicted) || !finite(correction) || error > kMaximumCorrectionM)
    {
        decision.resetHistory = true;
        return decision;
    }

    const bool correctionWindowOpen =
        m_rootHistory.correctionStartedAt == std::chrono::steady_clock::time_point{} ||
        (sampledAt >= m_rootHistory.correctionStartedAt && sampledAt - m_rootHistory.correctionStartedAt <= std::chrono::milliseconds(75));
    if (error >= kMinimumCorrectionM && correctionWindowOpen)
    {
        decision.correctionCameraLocal = correction;
        decision.corrected = true;
    }
    else if (error >= kMinimumCorrectionM)
    {
        decision.correctionWindowExpired = true;
        decision.resetHistory = true;
    }

    return decision;
}

void OpticProjectionEngine::commitRootCorrection(const RootCorrectionDecision& decision)
{
    if (decision.corrected)
    {
        if (m_rootHistory.correctionStartedAt == std::chrono::steady_clock::time_point{})
        {
            m_rootHistory.correctionStartedAt = decision.sampledAt;
        }
        return;
    }

    m_rootHistory.correctionStartedAt = {};
    RootMotionSample sample{};
    sample.cameraLocalPosition = decision.measuredCameraLocal;
    sample.sampledAt = decision.sampledAt;
    if (decision.resetHistory || m_rootHistory.trustedCount == 0)
    {
        m_rootHistory.trusted = {};
        m_rootHistory.trusted[1] = sample;
        m_rootHistory.trustedCount = 1;
        return;
    }

    if (m_rootHistory.trustedCount == 1)
    {
        m_rootHistory.trusted[0] = m_rootHistory.trusted[1];
        m_rootHistory.trusted[1] = sample;
        m_rootHistory.trustedCount = 2;
        return;
    }

    m_rootHistory.trusted[0] = m_rootHistory.trusted[1];
    m_rootHistory.trusted[1] = sample;
}

bool OpticProjectionEngine::discover(std::uint64_t localPlayer, std::uint64_t opticSight, std::uint64_t mainCamera, std::uint64_t opticCamera)
{
    reset();
    m_failureReason = "invalid optic route input";
    const std::uint64_t unityPlayer = mem.GetTarkovPointerSnapshot().unityPlayerBase;
    if (!validPointer(unityPlayer) || !validPointer(localPlayer) || !validPointer(opticSight) || !validPointer(mainCamera) || !validPointer(opticCamera))
    {
        return false;
    }

    Route route{};
    route.localPlayer = localPlayer;
    route.opticSight = opticSight;
    route.mainCamera = mainCamera;
    route.opticCamera = opticCamera;

    m_failureReason = "OpticSight.LensRenderer";
    if (!readPointer(opticSight + sdk::OpticSight::LensRenderer, route.lensManaged) || !readPointer(route.lensManaged + 0x10, route.lens))
    {
        return false;
    }

    std::uint64_t lensType = 0;
    std::uint64_t materialArrayType = 0;
    std::uint64_t gameObject = 0;
    std::uint16_t staticBatchSubmeshCount = 0;
    std::uint32_t extraVertexStreamId = 0;
    m_failureReason = "native lens renderer validation";
    if (!readPointer(route.lens, lensType) || lensType != unityPlayer + UnityOffsets::MeshRendererType || !readPointer(route.lens + 0x60, materialArrayType) ||
        materialArrayType != unityPlayer + UnityOffsets::RendererMaterialArrayType || !readPointer(route.lens + 0x58, gameObject) ||
        !readValue(route.lens + 0x122, staticBatchSubmeshCount) || staticBatchSubmeshCount != 0 || !readValue(route.lens + 0x25C, extraVertexStreamId) ||
        extraVertexStreamId != 0)
    {
        return false;
    }

    route.lensTransform = resolveComponent(gameObject, unityPlayer + UnityOffsets::TransformTypeDescriptor);
    route.meshFilter = resolveComponent(gameObject, unityPlayer + UnityOffsets::MeshFilterTypeDescriptor);
    std::uint64_t componentGameObject = 0;
    m_failureReason = "lens Transform or MeshFilter";
    if (!validPointer(route.lensTransform) || !validPointer(route.meshFilter) || !readPointer(route.lensTransform + 0x58, componentGameObject) ||
        componentGameObject != gameObject || !readPointer(route.meshFilter + 0x58, componentGameObject) || componentGameObject != gameObject ||
        !readValue(route.meshFilter + 0x60, route.meshId))
    {
        return false;
    }
    route.mesh = resolveInstance(unityPlayer, route.meshId);

    std::uint32_t materialCount = 0;
    m_failureReason = "lens mesh or material reference";
    if (!validPointer(route.mesh) || !readValue(route.lens + 0x180, materialCount) || materialCount != 1 ||
        !readPointer(route.lens + 0x170, route.materialIds) || !readValue(route.materialIds, route.materialId))
    {
        return false;
    }
    route.material = resolveInstance(unityPlayer, route.materialId);
    if (!validPointer(route.material))
        return false;

    std::uint32_t shaderId = 0;
    if (!readValue(route.material + 0x60, shaderId))
        return false;
    const std::uint64_t shader = resolveInstance(unityPlayer, shaderId);
    std::uint64_t shaderType = 0;
    std::uint64_t parsed = 0;
    m_failureReason = "CW FX/OpticSight shader validation";
    if (!validPointer(shader) || !readPointer(shader, shaderType) || shaderType != unityPlayer + UnityOffsets::ShaderType)
    {
        return false;
    }
    readValue(shader + 0x3D0, parsed);
    std::string shaderName = validPointer(parsed) ? shaderString(parsed + 0x80) : std::string{};
    if (shaderName.empty())
        shaderName = shaderString(shader + 0x78);
    if (shaderName != "CW FX/OpticSight")
        return false;

    m_failureReason = "optic material vectors";
    if (!resolveMaterialVectors(unityPlayer, route.material, route.scalesAddress, route.shiftsAddress, route.shiftDirectionAddress))
    {
        return false;
    }
    m_failureReason = "lens mesh UV map";
    std::string meshDiagnostic;
    if (!decodeMeshMap(route.mesh, route.meshMap, meshDiagnostic))
    {
        if (!meshDiagnostic.empty())
            m_failureReason += ": " + meshDiagnostic;
        return false;
    }
    m_failureReason = "lens transform chain";
    if (!resolveTransformChain(route.lensTransform, route.lensChain))
    {
        return false;
    }

    const auto resolveCameraChain = [&](std::uint64_t camera, std::vector<std::uint64_t>& chain)
    {
        std::uint64_t cameraObject = 0;
        if (!readPointer(camera + 0x58, cameraObject))
            return false;
        const std::uint64_t cameraTransform = resolveComponent(cameraObject, unityPlayer + UnityOffsets::TransformTypeDescriptor, 256);
        return validPointer(cameraTransform) && resolveTransformChain(cameraTransform, chain);
    };
    m_failureReason = "camera transform chains";
    if (!resolveCameraChain(mainCamera, route.mainCameraChain) || !resolveCameraChain(opticCamera, route.opticCameraChain))
    {
        return false;
    }

    std::uint64_t handsController = 0;
    std::uint64_t handsHierarchy = 0;
    std::uint64_t handsSelfManaged = 0;
    std::uint64_t handsSelfNative = 0;
    m_failureReason = "HandsHierarchy.Self";
    if (!readPointer(localPlayer + sdk::Player::_handsController, handsController) ||
        !readPointer(handsController + sdk::ItemHandsController::HandsHierarchy, handsHierarchy) ||
        !readPointer(handsHierarchy + sdk::TransformLinks::Self, handsSelfManaged) || !managedTransformToNative(handsSelfManaged, handsSelfNative))
    {
        return false;
    }
    const std::uint64_t handsNode = transformNodeAddress(handsSelfNative);
    m_failureReason = "HandsHierarchy.Self is not a lens ancestor";
    if (!validPointer(handsNode) ||
        std::find(route.lensChain.begin() + (route.lensChain.size() > 1 ? 1 : 0), route.lensChain.end(), handsNode) == route.lensChain.end())
    {
        return false;
    }
    route.handsNode = handsNode;
    route.handsNodeIndex =
        static_cast<std::size_t>(std::distance(route.lensChain.begin(), std::find(route.lensChain.begin(), route.lensChain.end(), handsNode)));
    route.fovNodes.insert(handsNode);

    std::uint64_t playerBones = 0;
    if (readPointer(localPlayer + sdk::Player::PlayerBones, playerBones))
    {
        std::uint64_t ribcage = 0;
        std::uint64_t ribcageManaged = 0;
        std::uint64_t ribcageNative = 0;
        if (readPointer(playerBones + sdk::PlayerBones::Ribcage, ribcage) && readPointer(ribcage + sdk::BifacialTransform::Original, ribcageManaged) &&
            managedTransformToNative(ribcageManaged, ribcageNative))
        {
            const std::uint64_t node = transformNodeAddress(ribcageNative);
            if (std::find(route.lensChain.begin(), route.lensChain.end(), node) != route.lensChain.end())
            {
                route.fovNodes.insert(node);
            }
        }

        std::uint64_t weaponRootManaged = 0;
        std::uint64_t weaponRootNative = 0;
        if (readPointer(playerBones + sdk::PlayerBones::WeaponRootThird, weaponRootManaged) && managedTransformToNative(weaponRootManaged, weaponRootNative))
        {
            const std::uint64_t node = transformNodeAddress(weaponRootNative);
            if (std::find(route.lensChain.begin(), route.lensChain.end(), node) != route.lensChain.end())
            {
                route.fovNodes.insert(node);
            }
        }
    }

    route.valid = true;
    m_route = std::move(route);
    m_failureReason.clear();
    return true;
}

bool OpticProjectionEngine::update(std::uint64_t localPlayer, std::uint64_t opticSight, std::uint64_t mainCamera, std::uint64_t opticCamera,
                                   CameraMatrixSample& mainSample, CameraMatrixSample& opticSample, OpticProjectionState& out, bool* busy)
{
    mainSample = {};
    opticSample = {};
    out = {};
    if (busy)
        *busy = false;

    const bool routeChanged = !m_route.valid || m_route.localPlayer != localPlayer || m_route.opticSight != opticSight || m_route.mainCamera != mainCamera ||
                              m_route.opticCamera != opticCamera;
    if (routeChanged)
    {
        if (!discover(localPlayer, opticSight, mainCamera, opticCamera))
            return false;
        m_adjacentCandidate = {};
        m_rootHistory = {};
        m_sampleSequence = 0;
    }

    RawCameraSample rawMain{};
    RawCameraSample rawOptic{};
    std::vector<UnityTransform::TrsX> lensNodes(m_route.lensChain.size());
    std::vector<UnityTransform::TrsX> mainCameraNodes(m_route.mainCameraChain.size());
    std::vector<UnityTransform::TrsX> opticCameraNodes(m_route.opticCameraChain.size());
    glm::vec4 scales{};
    glm::vec4 shifts{};
    glm::vec4 shiftDirection{};
    RendererBounds bounds{};
    float intendedScale = 0.0f;

    std::vector<Memory::ScatterReadRequest> requests;
    requests.reserve(16 + lensNodes.size() + mainCameraNodes.size() + opticCameraNodes.size());
    auto addCamera = [&](std::uint64_t camera, RawCameraSample& sample)
    {
        requests.push_back({camera + UnityOffsets::Camera_WorldToCameraMatrixOffset, &sample.view, sizeof(sample.view)});
        requests.push_back({camera + UnityOffsets::Camera_ProjectionMatrixOffset, &sample.activeProjection, sizeof(sample.activeProjection)});
        requests.push_back({camera + UnityOffsets::Camera_NonJitteredProjectionSetOffset, &sample.nonJitteredSet, sizeof(sample.nonJitteredSet)});
        requests.push_back(
            {camera + UnityOffsets::Camera_NonJitteredProjectionMatrixOffset, &sample.nonJitteredProjection, sizeof(sample.nonJitteredProjection)});
    };
    addCamera(mainCamera, rawMain);
    addCamera(opticCamera, rawOptic);
    for (std::size_t index = 0; index < lensNodes.size(); ++index)
        requests.push_back({m_route.lensChain[index], &lensNodes[index], sizeof(lensNodes[index])});
    for (std::size_t index = 0; index < mainCameraNodes.size(); ++index)
        requests.push_back({m_route.mainCameraChain[index], &mainCameraNodes[index], sizeof(mainCameraNodes[index])});
    for (std::size_t index = 0; index < opticCameraNodes.size(); ++index)
        requests.push_back({m_route.opticCameraChain[index], &opticCameraNodes[index], sizeof(opticCameraNodes[index])});
    requests.push_back({m_route.scalesAddress, &scales, sizeof(scales)});
    requests.push_back({m_route.shiftsAddress, &shifts, sizeof(shifts)});
    requests.push_back({m_route.shiftDirectionAddress, &shiftDirection, sizeof(shiftDirection)});
    requests.push_back({m_route.lens + 0xE8, &bounds, sizeof(bounds)});
    requests.push_back({localPlayer + sdk::Player::RibcageScaleCurrent, &intendedScale, sizeof(intendedScale)});

    if (requests.size() > Memory::MaxDmaScatterRequests)
    {
        m_failureReason = "paired sample exceeds scatter limit";
        return false;
    }
    std::vector<DWORD> bytesRead(requests.size());
    const auto result = mem.TryReadScatter(requests.data(), requests.size(), DmaCacheMode::Uncached, "Proper optic projection", bytesRead);
    if (result == Memory::TryScatterReadResult::Busy)
    {
        if (busy)
            *busy = true;
        m_failureReason = "DMA busy during paired sample";
        return false;
    }
    if (result != Memory::TryScatterReadResult::Success)
    {
        m_failureReason = "paired scatter execution";
        return false;
    }
    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        const bool unusedMainNonJittered = index == 3 && rawMain.nonJitteredSet == 0;
        const bool unusedOpticNonJittered = index == 7 && rawOptic.nonJitteredSet == 0;
        if (bytesRead[index] != requests[index].size && !unusedMainNonJittered && !unusedOpticNonJittered)
        {
            m_failureReason = "incomplete paired scatter read";
            return false;
        }
    }

    if (!makeCameraSample(rawMain, mainSample) || !makeCameraSample(rawOptic, opticSample))
    {
        m_failureReason = "paired camera matrices";
        return false;
    }
    if (!finite(scales) || !finite(shifts) || !finite(shiftDirection) || !finite(bounds.center) || !finite(bounds.extents))
    {
        m_failureReason = "lens material values or bounds";
        return false;
    }
    if (!cameraConsistent(mainSample.view, m_route.mainCameraChain, mainCameraNodes) ||
        !cameraConsistent(opticSample.view, m_route.opticCameraChain, opticCameraNodes))
    {
        m_failureReason = "camera pose consistency";
        return false;
    }

    glm::mat4 lensMatrix = composeTransform(m_route.lensChain, lensNodes, &m_route.fovNodes, intendedScale);
    if (!matrixLooksValid(lensMatrix))
    {
        m_failureReason = "corrected lens transform";
        return false;
    }
    const glm::mat4 handsMatrix = composeTransform(m_route.lensChain, lensNodes, &m_route.fovNodes, intendedScale, m_route.handsNodeIndex);
    if (!matrixLooksValid(handsMatrix))
    {
        m_failureReason = "corrected hands-root transform";
        return false;
    }

    const glm::vec4 cameraRelativeLens = mainSample.view * glm::vec4(glm::vec3(lensMatrix[3]), 1.0f);
    const glm::vec4 cameraRelativeHands = mainSample.view * glm::vec4(glm::vec3(handsMatrix[3]), 1.0f);
    if (!finite(cameraRelativeLens) || !finite(cameraRelativeHands))
    {
        m_failureReason = "camera-relative lens diagnostics";
        return false;
    }

    const RootCorrectionDecision rootDecision = evaluateRootCorrection(glm::vec3(cameraRelativeHands), std::chrono::steady_clock::now());
    if (rootDecision.corrected)
    {
        const glm::mat4 cameraToWorld = glm::inverse(mainSample.view);
        const glm::vec4 correctionWorld = cameraToWorld * glm::vec4(rootDecision.correctionCameraLocal, 0.0f);
        if (!finite(correctionWorld))
        {
            m_failureReason = "hands-root correction transform";
            return false;
        }
        lensMatrix[3] += glm::vec4(glm::vec3(correctionWorld), 0.0f);
    }

    const float xLength = glm::length(glm::vec3(lensMatrix[0]));
    const float yLength = glm::length(glm::vec3(lensMatrix[1]));
    if (xLength <= 1.0e-6f || yLength <= 1.0e-6f)
    {
        m_failureReason = "lens shader axes";
        return false;
    }
    lensMatrix[1] *= xLength / yLength;

    const glm::mat4 mainLens = mainSample.viewProjection * lensMatrix;
    const glm::vec4 hu = mainLens * glm::vec4(m_route.meshMap.uAxis * scales.x * 0.05f, 0.0f);
    const glm::vec4 hv = mainLens * glm::vec4(m_route.meshMap.vAxis * scales.x * 0.05f, 0.0f);
    const glm::vec3 shiftedCenter = m_route.meshMap.center * scales.x + glm::vec3(shiftDirection) * shifts.x;
    const glm::vec4 hc = mainLens * glm::vec4(shiftedCenter, 1.0f);
    if (!finite(hu) || !finite(hv) || !finite(hc) || hc.w - std::fabs(hu.w) - std::fabs(hv.w) <= kClipW)
    {
        m_failureReason = "lens image basis";
        return false;
    }

    glm::mat4 combined = opticSample.viewProjection;
    for (int row : {0, 1, 3})
    {
        for (int column = 0; column < 4; ++column)
        {
            combined[column][row] = (hu[row] * opticSample.viewProjection[column][0] + hv[row] * opticSample.viewProjection[column][1] +
                                     hc[row] * opticSample.viewProjection[column][3]) /
                                    hc.w;
        }
    }
    if (!matrixLooksValid(combined))
    {
        m_failureReason = "combined optic projection";
        return false;
    }

    std::vector<glm::vec2> imageQuad;
    imageQuad.reserve(4);
    constexpr std::array<glm::vec2, 4> corners = {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f)};
    for (const glm::vec2& corner : corners)
    {
        const glm::vec4 projected = hc + hu * corner.x + hv * corner.y;
        if (!finite(projected) || projected.w <= kClipW)
        {
            m_failureReason = "projected optic image quad";
            return false;
        }
        imageQuad.push_back(glm::vec2(projected) / projected.w);
    }
    ensureCounterClockwise(imageQuad);

    glm::vec2 minimum((std::numeric_limits<float>::max)());
    glm::vec2 maximum(std::numeric_limits<float>::lowest());
    for (int x = -1; x <= 1; x += 2)
    {
        for (int y = -1; y <= 1; y += 2)
        {
            for (int z = -1; z <= 1; z += 2)
            {
                const glm::vec3 world = bounds.center + bounds.extents * glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                glm::vec2 ndc{};
                if (!projectPoint(mainSample.viewProjection, world, ndc))
                {
                    m_failureReason = "projected lens bounds";
                    return false;
                }
                minimum = {std::fmin(minimum.x, ndc.x), std::fmin(minimum.y, ndc.y)};
                maximum = {std::fmax(maximum.x, ndc.x), std::fmax(maximum.y, ndc.y)};
            }
        }
    }
    const glm::vec2 ellipseCenter = (minimum + maximum) * 0.5f;
    const glm::vec2 radii = (maximum - minimum) * 0.5f;
    if (!finite(ellipseCenter) || !finite(radii) || radii.x <= 1.0e-6f || radii.y <= 1.0e-6f)
    {
        m_failureReason = "lens bounds ellipse";
        return false;
    }

    std::vector<glm::vec2> ellipse;
    ellipse.reserve(48);
    constexpr float kPi = 3.14159265358979323846f;
    for (int index = 0; index < 48; ++index)
    {
        const float angle = 2.0f * kPi * static_cast<float>(index) / 48.0f;
        ellipse.push_back(ellipseCenter + glm::vec2(radii.x * std::cos(angle), radii.y * std::sin(angle)));
    }
    std::vector<glm::vec2> mask = clipPolygon(ellipse, imageQuad);
    mask = clipPolygon(std::move(mask), {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}});
    ensureCounterClockwise(mask);
    if (mask.size() < 3 || std::fabs(polygonSignedArea(mask)) <= 1.0e-12f)
    {
        m_failureReason = "visible lens mask";
        return false;
    }

    std::uint64_t lensManagedAfter = 0;
    std::uint64_t materialIdsAfter = 0;
    std::uint32_t materialCountAfter = 0;
    std::uint32_t materialIdAfter = 0;
    std::uint32_t meshIdAfter = 0;
    if (!readPointer(opticSight + sdk::OpticSight::LensRenderer, lensManagedAfter) || lensManagedAfter != m_route.lensManaged ||
        !readValue(m_route.meshFilter + 0x60, meshIdAfter) || meshIdAfter != m_route.meshId || !readValue(m_route.lens + 0x180, materialCountAfter) ||
        materialCountAfter != 1 || !readPointer(m_route.lens + 0x170, materialIdsAfter) || materialIdsAfter != m_route.materialIds ||
        !readValue(materialIdsAfter, materialIdAfter) || materialIdAfter != m_route.materialId)
    {
        reset();
        m_failureReason = "lens mesh or material changed during sample";
        return false;
    }

    out.combined = combined;
    std::copy_n(imageQuad.begin(), 4, out.imageQuad.begin());
    out.mask = std::move(mask);
    out.imageCenterNdc = glm::vec2(hc) / hc.w;
    out.lensBoundsCenterNdc = ellipseCenter;
    out.lensBoundsRadiiNdc = radii;
    out.mainCameraRelativeLens = glm::vec3(cameraRelativeLens);
    out.mainCameraRelativeHands = glm::vec3(cameraRelativeHands);
    for (std::size_t index = 0; index < m_route.lensChain.size() && out.fovNodeCount < out.fovNodeScaleZ.size(); ++index)
    {
        if (m_route.fovNodes.contains(m_route.lensChain[index]))
            out.fovNodeScaleZ[out.fovNodeCount++] = lensNodes[index].s.z;
    }
    out.intendedFovScale = intendedScale;
    out.materialScale = scales.x;
    out.materialShift = shifts.x;
    out.rootPredictionErrorMm = rootDecision.predictionErrorMm;
    out.rootCorrectionMm = glm::length(rootDecision.correctionCameraLocal) * 1000.0f;
    out.rootTranslationCorrected = rootDecision.corrected;
    out.rootHistoryReset = rootDecision.resetHistory;
    out.rootCorrectionWindowExpired = rootDecision.correctionWindowExpired;
    out.opticSight = opticSight;
    out.lensRenderer = m_route.lens;
    out.mesh = m_route.mesh;
    out.material = m_route.material;
    out.valid = true;

    const auto sampledAt = std::chrono::steady_clock::now();
    out.sampledAt = sampledAt;
    constexpr auto kMaximumAdjacentAge = std::chrono::milliseconds(10);
    const bool candidateCurrent = m_adjacentCandidate.valid && m_adjacentCandidate.sampledAt != std::chrono::steady_clock::time_point{} &&
                                  sampledAt >= m_adjacentCandidate.sampledAt && sampledAt - m_adjacentCandidate.sampledAt <= kMaximumAdjacentAge &&
                                  m_adjacentCandidate.opticSight == out.opticSight && m_adjacentCandidate.lensRenderer == out.lensRenderer;

    if (!candidateCurrent)
    {
        m_adjacentCandidate = out;
        out = {};
        m_failureReason = "awaiting adjacent lens sample";
        return false;
    }

    if (!adjacentLensSamplesAgree(m_adjacentCandidate, out))
    {
        m_adjacentCandidate = out;
        out = {};
        m_failureReason = "adjacent lens samples disagree";
        return false;
    }

    m_adjacentCandidate = {};
    out.sampleSequence = ++m_sampleSequence;
    commitRootCorrection(rootDecision);
    m_failureReason.clear();
    return true;
}

bool OpticProjectionEngine::projectPoint(const glm::mat4& matrix, const glm::vec3& world, glm::vec2& ndc, float minimumW)
{
    const glm::vec4 clip = matrix * glm::vec4(world, 1.0f);
    if (!finite(clip) || clip.w <= minimumW)
        return false;
    ndc = glm::vec2(clip) / clip.w;
    return finite(ndc);
}

bool OpticProjectionEngine::pointInConvexMask(const glm::vec2& point, const std::vector<glm::vec2>& mask)
{
    if (mask.size() < 3 || !finite(point))
        return false;
    for (std::size_t index = 0; index < mask.size(); ++index)
    {
        if (cross2(mask[(index + 1) % mask.size()] - mask[index], point - mask[index]) < -1.0e-6f)
        {
            return false;
        }
    }
    return true;
}

bool OpticProjectionEngine::projectSegment(const glm::mat4& mainViewProjection, const glm::mat4& opticViewProjection, const OpticProjectionState& optic,
                                           const glm::vec3& worldStart, const glm::vec3& worldEnd, float viewportWidth, float viewportHeight,
                                           std::vector<CameraScreenSegment>& out, bool opticOnly)
{
    out.clear();
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0.0f || viewportHeight <= 0.0f)
    {
        return false;
    }

    const std::vector<glm::vec2> screenMask = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

    auto projectNdcSegment = [&](const glm::mat4& matrix, glm::vec4 start, glm::vec4 end, glm::vec2& ndcStart, glm::vec2& ndcEnd)
    {
        if (!clipHomogeneousNear(matrix, start, end))
            return false;
        const glm::vec4 clipStart = matrix * start;
        const glm::vec4 clipEnd = matrix * end;
        if (!finite(clipStart) || !finite(clipEnd) || clipStart.w <= kClipW || clipEnd.w <= kClipW)
        {
            return false;
        }
        ndcStart = glm::vec2(clipStart) / clipStart.w;
        ndcEnd = glm::vec2(clipEnd) / clipEnd.w;
        return finite(ndcStart) && finite(ndcEnd);
    };

    if (!optic.valid || !opticOnly)
    {
        glm::vec4 mainStart(worldStart, 1.0f);
        glm::vec4 mainEnd(worldEnd, 1.0f);
        glm::vec2 mainNdcStart{};
        glm::vec2 mainNdcEnd{};
        if (projectNdcSegment(mainViewProjection, mainStart, mainEnd, mainNdcStart, mainNdcEnd))
        {
            float screenLow = 0.0f;
            float screenHigh = 1.0f;
            if (clipSegmentToConvex(mainNdcStart, mainNdcEnd, screenMask, screenLow, screenHigh))
            {
                const glm::vec2 clippedStart = glm::mix(mainNdcStart, mainNdcEnd, screenLow);
                const glm::vec2 clippedEnd = glm::mix(mainNdcStart, mainNdcEnd, screenHigh);
                if (!optic.valid)
                {
                    addPixelSegment(clippedStart, clippedEnd, 0.0f, 1.0f, viewportWidth, viewportHeight, out);
                }
                else
                {
                    float maskLow = 0.0f;
                    float maskHigh = 1.0f;
                    if (clipSegmentToConvex(clippedStart, clippedEnd, optic.mask, maskLow, maskHigh))
                    {
                        addPixelSegment(clippedStart, clippedEnd, 0.0f, maskLow, viewportWidth, viewportHeight, out);
                        addPixelSegment(clippedStart, clippedEnd, maskHigh, 1.0f, viewportWidth, viewportHeight, out);
                    }
                    else
                    {
                        addPixelSegment(clippedStart, clippedEnd, 0.0f, 1.0f, viewportWidth, viewportHeight, out);
                    }
                }
            }
        }
    }

    if (optic.valid)
    {
        glm::vec4 opticWorldStart(worldStart, 1.0f);
        glm::vec4 opticWorldEnd(worldEnd, 1.0f);
        if (clipHomogeneousNear(opticViewProjection, opticWorldStart, opticWorldEnd))
        {
            glm::vec2 opticNdcStart{};
            glm::vec2 opticNdcEnd{};
            const glm::vec4 combinedStart = optic.combined * opticWorldStart;
            const glm::vec4 combinedEnd = optic.combined * opticWorldEnd;
            if (finite(combinedStart) && finite(combinedEnd) && combinedStart.w > kClipW && combinedEnd.w > kClipW)
            {
                opticNdcStart = glm::vec2(combinedStart) / combinedStart.w;
                opticNdcEnd = glm::vec2(combinedEnd) / combinedEnd.w;
                if (!finite(opticNdcStart) || !finite(opticNdcEnd))
                    return !out.empty();
                float low = 0.0f;
                float high = 1.0f;
                if (clipSegmentToConvex(opticNdcStart, opticNdcEnd, optic.mask, low, high))
                {
                    addPixelSegment(opticNdcStart, opticNdcEnd, low, high, viewportWidth, viewportHeight, out);
                }
            }
        }
    }

    return !out.empty();
}
