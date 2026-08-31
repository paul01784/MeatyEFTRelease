#pragma once

#include "PlayerRuntimeModel.h"

class BtrOperator final : public PlayerRuntimeModel
{
public:
    [[nodiscard]] PlayerKind getKind() const noexcept override;
    [[nodiscard]] bool matches(std::string_view className, bool isLocal) const noexcept override;
    [[nodiscard]] uint64_t getHeldItemOffset() const noexcept override;

    void initialize(Player& player) const noexcept override;
    void configureControllerAddresses(Player& player) const noexcept override;
    bool tryResolveBoneMatrix(const Player& player, uint64_t& matrixPointer) const override;
    [[nodiscard]] std::optional<Player> tryCreate(uint64_t instance, std::string_view className) const override;
    void prepareRefresh(const Player& player, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const override;
    void queueRefresh(ScatterReadBatch& batch, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const override;
    void applyRefresh(Player& player, const PlayerRuntimeRead& read, bool executed, const PlayerRefreshContext& context) const override;
};
