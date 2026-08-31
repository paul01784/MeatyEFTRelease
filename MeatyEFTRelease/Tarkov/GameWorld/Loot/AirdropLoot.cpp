#include "AirdropLoot.h"

LootEntityKind AirdropLoot::getKind() const noexcept
{
    return LootEntityKind::Airdrop;
}

bool AirdropLoot::needsPositionRefresh() const noexcept
{
    return true;
}

void AirdropLoot::initialize(LootEntity& entity) const noexcept
{
    entity.setKind(getKind());
}
