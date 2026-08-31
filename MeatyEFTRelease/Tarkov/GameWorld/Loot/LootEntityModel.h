#pragma once

#include "LootEntity.h"

class LootEntityModel
{
public:
    virtual ~LootEntityModel() = default;

    [[nodiscard]] virtual LootEntityKind getKind() const noexcept = 0;
    [[nodiscard]] virtual bool canApplyWantedState() const noexcept;
    [[nodiscard]] virtual bool needsContainerStateUpdate() const noexcept;
    [[nodiscard]] virtual bool needsCorpseUpdate() const noexcept;
    [[nodiscard]] virtual bool needsPositionRefresh() const noexcept;

    virtual void initialize(LootEntity& entity) const noexcept = 0;
};
