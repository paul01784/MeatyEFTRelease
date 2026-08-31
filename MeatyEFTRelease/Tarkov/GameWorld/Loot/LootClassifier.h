#pragma once

#include "LootEntityModel.h"

class LootClassifier
{
public:
    [[nodiscard]] static const LootEntityModel& get(LootEntityKind kind) noexcept;
    [[nodiscard]] static const LootEntityModel& get(const LootEntity& entity) noexcept;

    static void initialize(LootEntity& entity, LootEntityKind kind) noexcept;
};
