#include "BtrOperator.h"

#include "../../../UI/globals.h"
#include "../../../memory/ScatterReadBatch.h"
#include "../../SDK/EftOffsets.h"

PlayerKind BtrOperator::getKind() const noexcept
{
    return PlayerKind::Btr;
}

bool BtrOperator::matches(std::string_view, bool) const noexcept
{
    return false;
}

uint64_t BtrOperator::getHeldItemOffset() const noexcept
{
    return sdk::ObservedPlayerHands::Item;
}

void BtrOperator::initialize(Player& player) const noexcept
{
    player.setKind(getKind());
    player.isBTR = true;
    player.isAi = true;
    player.isBoss = false;
    player.isBlackDivision = false;
    player.isCultist = false;
    player.isPlayer = false;
    player.isPlayerScav = false;
    player.colour = coloursGlobals::aiBTR;
    player.name = "BTR";
}

void BtrOperator::configureControllerAddresses(Player& player) const noexcept
{
    if (!player.P_ObservedPlayerController)
        return;

    player.P_InventoryControllerAddr = player.P_ObservedPlayerController + sdk::ObservedPlayerController::InventoryController;
    player.P_HandsControllerAddr = player.P_ObservedPlayerController + sdk::ObservedPlayerController::HandsController;
}

bool BtrOperator::tryResolveBoneMatrix(const Player&, uint64_t& matrixPointer) const
{
    matrixPointer = 0;
    return false;
}

std::optional<Player> BtrOperator::tryCreate(uint64_t, std::string_view) const
{
    return std::nullopt;
}

void BtrOperator::prepareRefresh(const Player& player, PlayerRuntimeRead& read, const PlayerRefreshContext&) const
{
    read = {};
    read.instance = player.instance;
    read.kind = player.getKind();
    read.btrView = player.btrView;
    read.location = player.location;
}

void BtrOperator::queueRefresh(ScatterReadBatch& batch, PlayerRuntimeRead& read, const PlayerRefreshContext&) const
{
    read.locationQueued = batch.Add(read.btrView + sdk::BTRView::previousPosition, read.location);
}

void BtrOperator::applyRefresh(Player& player, const PlayerRuntimeRead& read, bool executed, const PlayerRefreshContext&) const
{
    if (executed && read.locationQueued)
        player.location = read.location;
}
