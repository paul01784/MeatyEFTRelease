#pragma once

#include "LootEntityModel.h"

class QuestLoot final : public LootEntityModel
{
public:
    [[nodiscard]] LootEntityKind getKind() const noexcept override;
    [[nodiscard]] bool canApplyWantedState() const noexcept override;

    void initialize(LootEntity& entity) const noexcept override;
};
