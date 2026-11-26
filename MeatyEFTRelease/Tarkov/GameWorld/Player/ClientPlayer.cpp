#include "ClientPlayer.h"

#include "PlayerMemoryAccess.h"

#include "../../../memory/ScatterReadBatch.h"
#include "../../SDK/EftOffsets.h"

#include <cmath>

namespace
{
    constexpr std::chrono::milliseconds corpseReadInterval{ 20 };
    constexpr std::chrono::milliseconds corpseFailedReadRetryInterval{ 20 };
    constexpr std::chrono::milliseconds handsReadInterval{ 2000 };
    constexpr std::chrono::milliseconds failedReadRetryInterval{ 2500 };
}

PlayerKind ClientPlayer::getKind() const noexcept
{
    return PlayerKind::Client;
}

bool ClientPlayer::matches(std::string_view className, bool isLocal) const noexcept
{
    return !isLocal && (className == "ClientPlayer" || className == "LocalPlayer");
}

uint64_t ClientPlayer::getHeldItemOffset() const noexcept
{
    return sdk::ItemHandsController::Item;
}

void ClientPlayer::initialize(Player& player) const noexcept
{
    player.isLocal = false;
    player.setKind(getKind());
}

void ClientPlayer::configureControllerAddresses(Player& player) const noexcept
{
    player.P_InventoryControllerAddr = player.instance + sdk::Player::_inventoryController;
    player.P_HandsControllerAddr = player.instance + sdk::Player::_handsController;
}

bool ClientPlayer::tryResolveBoneMatrix(const Player& player, uint64_t& matrixPointer) const
{
    return PlayerMemoryAccess::tryReadChain(player.instance, { sdk::Player::_playerBody, 0x40, 0x30, 0x10 }, matrixPointer, DmaCacheMode::Uncached);
}

std::optional<Player> ClientPlayer::tryCreate(uint64_t instance, std::string_view className) const
{
    if (!Utils::valid_pointer(instance) || (className != "ClientPlayer" && className != "LocalPlayer"))
        return std::nullopt;

    Player player{};
    player.instance = instance;
    player.className = className;
    initialize(player);
    initializeSnapshot(player);

    if (player.isLocal)
        player.name = "LocalPlayer";
    else
    {
        player.name = "Ai";
        player.isAi = true;
		player.type = PlayerType::AIScav;
    }

    player.P_CorpseAddr = instance + sdk::Player::Corpse;

    PlayerMemoryAccess::initializationStage("profile");
    if (!PlayerMemoryAccess::tryReadPointer(instance + sdk::Player::Profile, player.P_Profile, DmaCacheMode::Uncached))
        return std::nullopt;
    PlayerMemoryAccess::initializationStage("profile info");
    if (!PlayerMemoryAccess::tryReadPointer(player.P_Profile + sdk::Profile::Info, player.P_Info, DmaCacheMode::Uncached))
        return std::nullopt;
    PlayerMemoryAccess::initializationStage("player body");
    if (!PlayerMemoryAccess::tryReadPointer(instance + sdk::Player::_playerBody, player.P_Body, DmaCacheMode::Uncached))
        return std::nullopt;

    PlayerMemoryAccess::tryReadPointer(instance + sdk::Player::ProceduralWeaponAnimation, player.P_PWA, DmaCacheMode::Uncached);
    configureControllerAddresses(player);

    PlayerMemoryAccess::initializationStage("player side");
    if (!PlayerMemoryAccess::tryRead(player.P_Info + sdk::PlayerInfo::Side, player.playerSide, DmaCacheMode::Uncached))
        return std::nullopt;

    const uint32_t sideValue = static_cast<uint32_t>(player.playerSide);
    if (sideValue != static_cast<uint32_t>(EPlayerSide::Usec) &&
        sideValue != static_cast<uint32_t>(EPlayerSide::Bear) &&
        sideValue != static_cast<uint32_t>(EPlayerSide::Savage))
    {
        return std::nullopt;
    }

    PlayerMemoryAccess::initializationStage("movement context");
    if (!PlayerMemoryAccess::tryReadPointer(instance + sdk::Player::MovementContext, player.P_MovementContext, DmaCacheMode::Uncached))
        return std::nullopt;

    player.P_RotationAddress = player.P_MovementContext + sdk::MovementContext::_rotation;

    uint64_t characterController = 0;

    if (PlayerMemoryAccess::tryReadPointer(instance + sdk::Player::CharacterController, characterController, DmaCacheMode::Uncached))
        player.P_VelocityAddress = characterController + sdk::SimpleCharacterController::Velocity;

    if (!Utils::valid_pointer(player.P_CorpseAddr) ||
        !Utils::valid_pointer(player.P_InventoryControllerAddr) ||
        !Utils::valid_pointer(player.P_HandsControllerAddr) ||
        !Utils::valid_pointer(player.P_RotationAddress))
    {
        return std::nullopt;
    }

    return player;
}

void ClientPlayer::prepareRefresh(const Player& player, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const
{
    read = {};
    read.instance = player.instance;
    read.cacheEntryId = player.cacheEntryId;
    read.kind = player.getKind();
    read.rotationAddress = player.P_RotationAddress;
    read.corpseAddress = player.P_CorpseAddr;
    read.handsControllerAddress = player.P_HandsControllerAddr;
    read.proceduralWeaponAnimation = player.P_PWA;
    read.velocityAddress = player.P_VelocityAddress;
    read.isLocal = player.isLocal;
    read.rotationRaw = player.rotationRAW;
    read.velocity = player.velocity;
    read.corpseClass = player.P_CorpseClass;
    read.handsController = player.P_HandsController;
    read.isAiming = player.isAiming;
    read.corpseDue = player.nextCorpseRead == std::chrono::steady_clock::time_point{} || context.now >= player.nextCorpseRead;
    read.handsDue = player.nextHandsControllerRead == std::chrono::steady_clock::time_point{} || context.now >= player.nextHandsControllerRead;
}

void ClientPlayer::queueRefresh(ScatterReadBatch& batch, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const
{
    read.rotationQueued = batch.AddBytes(read.rotationAddress, &read.rotationRaw, sizeof(glm::vec2));

    if ((read.isLocal || context.predictionEnabled) && Utils::valid_pointer(read.velocityAddress))
        read.velocityQueued = batch.Add(read.velocityAddress, read.velocity);

    if (read.corpseDue)
        read.corpseQueued = batch.Add(read.corpseAddress, read.corpseClass);

    if (read.handsDue)
        read.handsQueued = batch.Add(read.handsControllerAddress, read.handsController);

    read.aimingQueued = batch.Add(read.proceduralWeaponAnimation + sdk::ProceduralWeaponAnimation::_isAiming, read.isAiming);
}

void ClientPlayer::applyRefresh(Player& player, const PlayerRuntimeRead& read, bool executed, const PlayerRefreshContext& context) const
{
    if (executed)
    {
        if (read.rotationQueued)
            player.rotationRAW = read.rotationRaw;

        if (read.velocityQueued)
            applyVelocitySample(player, read.velocity, context.now);

        if (read.corpseQueued)
            player.P_CorpseClass = read.corpseClass;

        if (read.handsQueued)
            player.P_HandsController = read.handsController;

        if (read.aimingQueued)
            player.isAiming = read.isAiming;
    }

    if (read.corpseDue)
        player.nextCorpseRead = executed && read.corpseQueued ? context.now + corpseReadInterval : context.now + corpseFailedReadRetryInterval;

    if (read.handsDue)
        player.nextHandsControllerRead = executed && read.handsQueued ? context.now + handsReadInterval : context.now + failedReadRetryInterval;
}
