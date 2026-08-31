#include "QuestLoot.h"

LootEntityKind QuestLoot::getKind() const noexcept
{
    return LootEntityKind::QuestItem;
}

bool QuestLoot::canApplyWantedState() const noexcept
{
    return true;
}

void QuestLoot::initialize(LootEntity& entity) const noexcept
{
    entity.setKind(getKind());
}
