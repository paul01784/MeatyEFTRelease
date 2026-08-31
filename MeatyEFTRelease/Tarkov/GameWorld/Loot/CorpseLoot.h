#pragma once

#include "LootEntityModel.h"

class CorpseLoot final : public LootEntityModel
{
public:
    [[nodiscard]] LootEntityKind getKind() const noexcept override;
    [[nodiscard]] bool needsCorpseUpdate() const noexcept override;

    void initialize(LootEntity& entity) const noexcept override;
};
