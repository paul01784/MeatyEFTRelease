#pragma once

#include "Player.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

class ScatterReadBatch;

struct PlayerRefreshContext
{
    std::chrono::steady_clock::time_point now;
    bool predictionEnabled{ false };
};

struct PlayerRuntimeRead
{
    uint64_t instance{ 0 };
    uint64_t cacheEntryId{ 0 };
    PlayerKind kind{ PlayerKind::Unknown };
    uint64_t btrView{ 0 };
    uint64_t rotationAddress{ 0 };
    uint64_t corpseAddress{ 0 };
    uint64_t handsControllerAddress{ 0 };
    uint64_t proceduralWeaponAnimation{ 0 };
    uint64_t observedHealthController{ 0 };
    uint64_t velocityAddress{ 0 };
    bool isLocal{ false };
    glm::vec3 location{};
    glm::vec3 rotationRaw{};
    glm::vec3 velocity{};
    uint64_t corpseClass{ 0 };
    uint64_t handsController{ 0 };
    int healthTag{ 0 };
    bool isAiming{ false };
    bool corpseDue{ false };
    bool healthDue{ false };
    bool handsDue{ false };
    bool locationQueued{ false };
    bool rotationQueued{ false };
    bool velocityQueued{ false };
    bool corpseQueued{ false };
    bool healthQueued{ false };
    bool handsQueued{ false };
    bool aimingQueued{ false };
};

class PlayerRuntimeModel
{
public:
    virtual ~PlayerRuntimeModel() = default;

    [[nodiscard]] virtual PlayerKind getKind() const noexcept = 0;
    [[nodiscard]] virtual bool matches(std::string_view className, bool isLocal) const noexcept = 0;
    [[nodiscard]] virtual uint64_t getHeldItemOffset() const noexcept = 0;

    virtual void initialize(Player& player) const noexcept = 0;
    virtual void configureControllerAddresses(Player& player) const noexcept = 0;
    virtual bool tryResolveBoneMatrix(const Player& player, uint64_t& matrixPointer) const = 0;
    [[nodiscard]] virtual std::optional<Player> tryCreate(uint64_t instance, std::string_view className) const = 0;
    virtual void prepareRefresh(const Player& player, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const = 0;
    virtual void queueRefresh(ScatterReadBatch& batch, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const = 0;
    virtual void applyRefresh(Player& player, const PlayerRuntimeRead& read, bool executed, const PlayerRefreshContext& context) const = 0;

protected:
    void applyVelocitySample(
        Player& player,
        const glm::vec3& sample,
        const std::chrono::steady_clock::time_point now) const noexcept
    {
        const float speed = glm::length(sample);
        const bool sampleValid =
            std::isfinite(sample.x) &&
            std::isfinite(sample.y) &&
            std::isfinite(sample.z) &&
            std::isfinite(speed) &&
            speed <= 15.0f;

        // Keep the last good sample across a transient bad DMA read. The aim
        // path already expires stale velocity, so one bad sample must not
        // instantly turn prediction off and back on again.
        if (!sampleValid)
            return;

        if (!player.velocityValid ||
            player.lastVelocityUpdate == std::chrono::steady_clock::time_point{})
        {
            player.velocity = sample;
        }
        else
        {
            constexpr float kSmoothingTimeSeconds = 0.08f;
            float elapsedSeconds =
                std::chrono::duration<float>(now - player.lastVelocityUpdate).count();

            if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f)
                elapsedSeconds = 0.02f;

            const float blend = std::clamp(
                1.0f - std::exp(-elapsedSeconds / kSmoothingTimeSeconds),
                0.05f,
                1.0f);

            player.velocity += (sample - player.velocity) * blend;

            if (speed < 0.01f && glm::length(player.velocity) < 0.01f)
                player.velocity = {};
        }

        // A stationary player is a valid zero-velocity prediction sample.
        player.velocityValid = true;
        player.lastVelocityUpdate = now;
    }

    void initializeSnapshot(Player& player) const noexcept
    {
        player.equipInited = false;
        player.lastEquipmentUpdate = {};
        player.playerBoneMatrixPtr = 0;
        player.bonePointersNeedResolve = true;
        player.invalidBones = false;
        player.minimalBonePointersReady = false;
        player.fullBonePointersReady = false;
        player.minimalBonePoseValid = false;
        player.fullBonePoseValid = false;
        player.consecutiveBadBoneSamples = 0;
        player.nextBonePointerResolve = {};
    }
};
