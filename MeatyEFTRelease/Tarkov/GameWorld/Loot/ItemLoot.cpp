#include "ItemLoot.h"

LootEntityKind ItemLoot::getKind() const noexcept
{
    return LootEntityKind::Item;
}

bool ItemLoot::canApplyWantedState() const noexcept
{
    return true;
}

void ItemLoot::initialize(LootEntity& entity) const noexcept
{
    entity.setKind(getKind());
}
