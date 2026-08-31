#include "CorpseLoot.h"

LootEntityKind CorpseLoot::getKind() const noexcept
{
    return LootEntityKind::Corpse;
}

bool CorpseLoot::needsCorpseUpdate() const noexcept
{
    return true;
}

void CorpseLoot::initialize(LootEntity& entity) const noexcept
{
    entity.setKind(getKind());
}
