#pragma once

#include "LootEntityModel.h"

class AirdropLoot final : public LootEntityModel
{
public:
    [[nodiscard]] LootEntityKind getKind() const noexcept override;
    [[nodiscard]] bool needsPositionRefresh() const noexcept override;

    void initialize(LootEntity& entity) const noexcept override;
};
