#include "LootClassifier.h"

#include "AirdropLoot.h"
#include "ContainerLoot.h"
#include "CorpseLoot.h"
#include "ItemLoot.h"
#include "QuestLoot.h"

namespace
{
    class UnknownLoot final : public LootEntityModel
    {
    public:
        [[nodiscard]] LootEntityKind getKind() const noexcept override
        {
            return LootEntityKind::Unknown;
        }

        void initialize(LootEntity& entity) const noexcept override
        {
            entity.setKind(getKind());
        }
    };

    const AirdropLoot airdropLoot;
    const ContainerLoot containerLoot;
    const CorpseLoot corpseLoot;
    const ItemLoot itemLoot;
    const QuestLoot questLoot;
    const UnknownLoot unknownLoot;
}

const LootEntityModel& LootClassifier::get(LootEntityKind kind) noexcept
{
    switch (kind)
    {
    case LootEntityKind::Item: return itemLoot;
    case LootEntityKind::QuestItem: return questLoot;
    case LootEntityKind::Container: return containerLoot;
    case LootEntityKind::Corpse: return corpseLoot;
    case LootEntityKind::Airdrop: return airdropLoot;
    case LootEntityKind::Unknown:
    default: return unknownLoot;
    }
}

const LootEntityModel& LootClassifier::get(const LootEntity& entity) noexcept
{
    return get(entity.kind);
}

void LootClassifier::initialize(LootEntity& entity, LootEntityKind kind) noexcept
{
    get(kind).initialize(entity);
}
