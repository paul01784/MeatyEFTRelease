#include "LootEntityModel.h"

bool LootEntityModel::canApplyWantedState() const noexcept
{
    return false;
}

bool LootEntityModel::needsContainerStateUpdate() const noexcept
{
    return false;
}

bool LootEntityModel::needsCorpseUpdate() const noexcept
{
    return false;
}

bool LootEntityModel::needsPositionRefresh() const noexcept
{
    return false;
}
