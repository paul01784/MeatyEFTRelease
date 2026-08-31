#pragma once

#include "Player.h"

#include <chrono>
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
    void initializeSnapshot(Player& player) const noexcept
    {
        player.equipInited = false;
        player.lastEquipmentUpdate = {};
        player.lastHandsUpdate = {};
        player.playerBoneMatrixPtr = 0;
        player.bonePointersNeedResolve = true;
        player.invalidBones = false;
    }
};
