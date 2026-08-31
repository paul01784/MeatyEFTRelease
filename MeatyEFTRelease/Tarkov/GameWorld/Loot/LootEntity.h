#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

enum class LootEntityKind : uint8_t
{
    Unknown = 0,
    Item,
    QuestItem,
    Container,
    Corpse,
    Airdrop
};

struct CorpseEquipment
{
    std::string name;
    int value = 0;
    bool wanted = false;
};

struct ItemLootState
{
};

struct QuestLootState
{
};

struct ContainerLootState
{
    bool opened = false;
};

struct CorpseLootState
{
    bool ownerResolved = false;
    std::string ownerName;
    int value = 0;
    std::vector<CorpseEquipment> equipment;
};

struct AirdropLootState
{
};

using LootEntityState = std::variant<
    std::monostate,
    ItemLootState,
    QuestLootState,
    ContainerLootState,
    CorpseLootState,
    AirdropLootState>;

struct LootEntity
{
    uint64_t instance = 0;
    uint64_t m_itemObject = 0;
    uint64_t m_interactiveClass = 0;
    uint64_t m_baseObject = 0;
    uint64_t m_gameObject = 0;
    uint64_t m_pGameObjectName = 0;
    std::string m_objectClassName;
    uint64_t m_objectClass = 0;
    uint64_t m_pointerToTransform1 = 0;
    uint64_t m_pointerToTransform2 = 0;

    glm::vec3 worldLocation{};

    std::string gameObjectName;
    std::string bsgId;
    std::string longName;
    std::string shortName;

    int avgMarketPrice = 0;
    int traderPrice = 0;
    int distance = 0;

    LootEntityKind kind = LootEntityKind::Unknown;
    LootEntityState state{};

    bool wanted = false;
    bool forceWanted = false;
    bool filterWanted = false;
    glm::vec4 color{};
    glm::vec4 forceColor{};

    bool failed = false;
    bool retryableFailure = true;
    bool hasValidPosition = false;
    std::string failureReason;
    bool pendingResolve = false;
    std::uint8_t resolveAttempts = 0;

    std::chrono::steady_clock::time_point nextResolveAttempt{};
    std::chrono::steady_clock::time_point lastPositionUpdate{};
    std::chrono::steady_clock::time_point lastCorpseEquipmentUpdate{};

    void setKind(LootEntityKind entityKind) noexcept
    {
        kind = entityKind;

        switch (kind)
        {
        case LootEntityKind::Item: state.emplace<ItemLootState>(); break;
        case LootEntityKind::QuestItem: state.emplace<QuestLootState>(); break;
        case LootEntityKind::Container: state.emplace<ContainerLootState>(); break;
        case LootEntityKind::Corpse: state.emplace<CorpseLootState>(); break;
        case LootEntityKind::Airdrop: state.emplace<AirdropLootState>(); break;
        case LootEntityKind::Unknown:
        default: state.emplace<std::monostate>(); break;
        }
    }

    [[nodiscard]] bool isItem() const noexcept
    {
        return kind == LootEntityKind::Item;
    }

    [[nodiscard]] bool isQuestItem() const noexcept
    {
        return kind == LootEntityKind::QuestItem;
    }

    [[nodiscard]] bool isContainer() const noexcept
    {
        return kind == LootEntityKind::Container || kind == LootEntityKind::Airdrop;
    }

    [[nodiscard]] bool isCorpse() const noexcept
    {
        return kind == LootEntityKind::Corpse;
    }

    [[nodiscard]] bool isAirdrop() const noexcept
    {
        return kind == LootEntityKind::Airdrop;
    }

    [[nodiscard]] ContainerLootState& getContainerState()
    {
        return std::get<ContainerLootState>(state);
    }

    [[nodiscard]] const ContainerLootState& getContainerState() const
    {
        return std::get<ContainerLootState>(state);
    }

    [[nodiscard]] CorpseLootState& getCorpseState()
    {
        return std::get<CorpseLootState>(state);
    }

    [[nodiscard]] const CorpseLootState& getCorpseState() const
    {
        return std::get<CorpseLootState>(state);
    }

    [[nodiscard]] int getCorpseValue() const noexcept
    {
        const auto* corpseState = std::get_if<CorpseLootState>(&state);
        return corpseState ? corpseState->value : 0;
    }
};
