#include "LocalPlayer.h"

#include "../MainGame.h"
#include "../QuestManager.h"
#include "../../SDK/EftOffsets.h"

PlayerKind LocalPlayer::getKind() const noexcept
{
    return PlayerKind::Local;
}

bool LocalPlayer::matches(std::string_view, bool isLocal) const noexcept
{
    return isLocal;
}

uint64_t LocalPlayer::getHeldItemOffset() const noexcept
{
    return sdk::ItemHandsController::Item;
}

void LocalPlayer::initialize(Player& player) const noexcept
{
    player.isLocal = true;
    player.setKind(getKind());
}

void LocalPlayer::configureControllerAddresses(Player& player) const noexcept
{
    player.P_InventoryControllerAddr = player.instance + sdk::Player::_inventoryController;
    player.P_HandsControllerAddr = player.instance + sdk::Player::_handsController;
}

bool LocalPlayer::tryResolveBoneMatrix(const Player& player, uint64_t& matrixPointer) const
{
    return ClientPlayer::tryResolveBoneMatrix(player, matrixPointer);
}

std::optional<Player> LocalPlayer::tryCreate(uint64_t instance, std::string_view className) const
{
    auto player = ClientPlayer::tryCreate(instance, className);

    if (!player.has_value())
        return std::nullopt;

    const bool isSavage = (static_cast<uint32_t>(player->playerSide) & static_cast<uint32_t>(EPlayerSide::Savage)) != 0;
    mainGame.localIsSavage = isSavage;
    mainGame.localplayerProfile = player->P_Profile;
    player->isPlayer = !isSavage;
    player->isPlayerScav = isSavage;

    try
    {
        questManager.initQuestManager();
    }
    catch (...)
    {
    }

    return player;
}

void LocalPlayer::prepareRefresh(const Player& player, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const
{
    ClientPlayer::prepareRefresh(player, read, context);
}

void LocalPlayer::queueRefresh(ScatterReadBatch& batch, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const
{
    ClientPlayer::queueRefresh(batch, read, context);
}

void LocalPlayer::applyRefresh(Player& player, const PlayerRuntimeRead& read, bool executed, const PlayerRefreshContext& context) const
{
    ClientPlayer::applyRefresh(player, read, executed, context);
}
