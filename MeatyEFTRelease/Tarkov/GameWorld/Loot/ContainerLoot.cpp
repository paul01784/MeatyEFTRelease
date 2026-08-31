#include "ContainerLoot.h"

LootEntityKind ContainerLoot::getKind() const noexcept
{
    return LootEntityKind::Container;
}

bool ContainerLoot::needsContainerStateUpdate() const noexcept
{
    return true;
}

void ContainerLoot::initialize(LootEntity& entity) const noexcept
{
    entity.setKind(getKind());
}
