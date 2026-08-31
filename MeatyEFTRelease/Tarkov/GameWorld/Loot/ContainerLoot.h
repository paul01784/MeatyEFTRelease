#pragma once

#include "LootEntityModel.h"

class ContainerLoot final : public LootEntityModel
{
public:
    [[nodiscard]] LootEntityKind getKind() const noexcept override;
    [[nodiscard]] bool needsContainerStateUpdate() const noexcept override;

    void initialize(LootEntity& entity) const noexcept override;
};
