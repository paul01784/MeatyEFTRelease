// UnityTransform.h
#pragma once
#include <cstdint>
#include <vector>
#include <span>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <iostream>

class UnityTransform
{
public:
    static constexpr int MAX_ITERATIONS = 4000;

#pragma pack(push, 1)
    struct TrsX
    {
        glm::vec3 t;     // 0x00
        uint32_t  _pad0; // 0x0C..0x0F
        glm::quat q;     // 0x10
        glm::vec3 s;     // 0x20
        uint32_t  _pad1; // 0x2C..0x2F
    };
#pragma pack(pop)

public:
    UnityTransform(uint64_t transformInternal, bool useCache = false);

    const glm::vec3& Position() const;

    int Count() const;

    glm::vec3& UpdatePosition(std::span<TrsX> vertices = {});
    glm::quat  GetRotation(std::span<TrsX> vertices = {});

    glm::vec3 GetRootPosition(); 
    glm::vec3 GetLocalPosition();
    glm::vec3 GetLocalScale();
    glm::quat GetLocalRotation();

    glm::vec3 TransformPoint(glm::vec3 localPoint, std::span<TrsX> vertices = {});
    glm::vec3 InverseTransformPoint(glm::vec3 worldPoint, std::span<TrsX> vertices = {});

    bool IsValid() const { return _valid; }

public:
    uint64_t TransformInternal = 0;
    uint64_t VerticesAddr = 0;

private:
    std::vector<int>  ReadIndices();
    std::vector<TrsX> ReadVertices();

    static bool IsAbnormal(const glm::vec3& v);
    static bool IsAbnormal(const glm::quat& q);

private:
    bool _useCache = false;
    bool _valid = false;

    int      _index = -1;
    uint64_t _hierarchyAddr = 0;
    uint64_t _indicesAddr = 0;

    std::vector<int> _indices;
    glm::vec3 _position{ 0.0f };
};

struct UnityTransformExtensions
{
    static inline const glm::vec3 LeftV = glm::vec3(-1.f, 0.f, 0.f);
    static inline const glm::vec3 RightV = glm::vec3(1.f, 0.f, 0.f);
    static inline const glm::vec3 UpV = glm::vec3(0.f, 1.f, 0.f);
    static inline const glm::vec3 DownV = glm::vec3(0.f, -1.f, 0.f);
    static inline const glm::vec3 ForwardV = glm::vec3(0.f, 0.f, 1.f);

    static glm::vec3 Left(const glm::quat& q) { return q * LeftV; }
    static glm::vec3 Right(const glm::quat& q) { return q * RightV; }
    static glm::vec3 Up(const glm::quat& q) { return q * UpV; }
    static glm::vec3 Down(const glm::quat& q) { return q * DownV; }
    static glm::vec3 Forward(const glm::quat& q) { return q * ForwardV; }

    static glm::vec3 TransformDirection(const glm::quat& q, const glm::vec3& localDirection)
    {
        return q * localDirection;
    }

    static glm::vec3 InverseTransformDirection(const glm::quat& q, const glm::vec3& worldDirection)
    {
        return glm::conjugate(q) * worldDirection;
    }
};