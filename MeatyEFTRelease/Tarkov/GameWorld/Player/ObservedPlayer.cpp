#include "ObservedPlayer.h"

#include "PlayerMemoryAccess.h"

#include "../../../memory/ScatterReadBatch.h"
#include "../../SDK/EftOffsets.h"
#include "../MainGame.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace
{
    constexpr std::chrono::milliseconds corpseReadInterval{ 100 };
    constexpr std::chrono::milliseconds healthReadInterval{ 2000 };
    constexpr std::chrono::milliseconds handsReadInterval{ 2000 };
    constexpr std::chrono::milliseconds failedReadRetryInterval{ 2500 };

    bool containsIgnoreCase(const std::string& value, const std::string& search)
    {
        return std::search(value.begin(), value.end(), search.begin(), search.end(), [](char left, char right)
            {
                return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
            }) != value.end();
    }

    AIRole getAiRole(const std::string& voice)
    {
        if (containsIgnoreCase(voice, "BossSanitar")) return { "Sanitar", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "BossBully")) return { "Reshala", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "BossGluhar")) return { "Gluhar", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "SectantPriest")) return { "Priest", PlayerType::AIBoss, false, true };
        if (containsIgnoreCase(voice, "SectantWarrior")) return { "Cultist", PlayerType::AIRaider, false, true };
        if (containsIgnoreCase(voice, "BossKilla")) return { "Killa", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "BossTagilla")) return { "Tagilla", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "Boss_Partizan")) return { "Partisan", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "BossBigPipe")) return { "Big Pipe", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "BossBirdEye")) return { "Birdeye", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "BossKnight")) return { "Knight", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "Boss_Kaban")) return { "Kaban", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "Boss_Kollontay")) return { "Kollontay", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "Boss_Sturman")) return { "Sturman", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "blackdivision") || containsIgnoreCase(voice, "black_division") || containsIgnoreCase(voice, "black division")) return { "BlackDiv", PlayerType::AIBoss, true };
        if (containsIgnoreCase(voice, "vsrf")) return { "VSRF", PlayerType::AIRaider };
        if (containsIgnoreCase(voice, "civilian")) return { "Civilian", PlayerType::AIScav };
        if (containsIgnoreCase(voice, "Arena_Guard")) return { "Arena Guard", PlayerType::AIScav };
        if (containsIgnoreCase(voice, "BossZombieTagilla")) return { "Zombie Tagilla", PlayerType::AIBoss };
        if (containsIgnoreCase(voice, "Zombie")) return { "Zombie", PlayerType::AIScav };
        if (containsIgnoreCase(voice, "usec")) return { "Usec", PlayerType::AIRaider };
        if (containsIgnoreCase(voice, "bear")) return { "Bear", PlayerType::AIRaider };
        if (containsIgnoreCase(voice, "scav")) return { "Scav", PlayerType::AIScav };

        return { voice, PlayerType::AIBoss };
    }

    std::string sideToString(EPlayerSide side)
    {
        switch (side)
        {
        case EPlayerSide::Usec: return "Usec";
        case EPlayerSide::Bear: return "Bear";
        case EPlayerSide::Savage: return "Savage";
        default: return "Unknown";
        }
    }
}

PlayerKind ObservedPlayer::getKind() const noexcept
{
    return PlayerKind::Observed;
}

bool ObservedPlayer::matches(std::string_view, bool isLocal) const noexcept
{
    return !isLocal;
}

uint64_t ObservedPlayer::getHeldItemOffset() const noexcept
{
    return sdk::ObservedPlayerHands::Item;
}

void ObservedPlayer::initialize(Player& player) const noexcept
{
    player.isLocal = false;
    player.setKind(getKind());
}

void ObservedPlayer::configureControllerAddresses(Player& player) const noexcept
{
    if (!player.P_ObservedPlayerController)
        return;

    player.P_InventoryControllerAddr = player.P_ObservedPlayerController + sdk::ObservedPlayerController::InventoryController;
    player.P_HandsControllerAddr = player.P_ObservedPlayerController + sdk::ObservedPlayerController::HandsController;
}

bool ObservedPlayer::tryResolveBoneMatrix(const Player& player, uint64_t& matrixPointer) const
{
    return PlayerMemoryAccess::tryReadChain(player.instance, { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 }, matrixPointer, DmaCacheMode::Uncached);
}

std::optional<Player> ObservedPlayer::tryCreate(uint64_t instance, std::string_view className) const
{
    if (!Utils::valid_pointer(instance))
        return std::nullopt;

    Player player{};
    player.instance = instance;
    player.className = className;
    initialize(player);
    initializeSnapshot(player);

    PlayerMemoryAccess::tryReadChain(instance, { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 }, player.playerBoneMatrixPtr, DmaCacheMode::Uncached);

    if (!PlayerMemoryAccess::tryReadPointer(instance + sdk::ObservedPlayerView::ObservedPlayerController, player.P_ObservedPlayerController) ||
        !PlayerMemoryAccess::tryReadPointer(player.P_ObservedPlayerController + sdk::ObservedPlayerController::HealthController, player.P_ObservedHealthController))
    {
        return std::nullopt;
    }

    configureControllerAddresses(player);

    uint64_t movementController = 0;

    if (!PlayerMemoryAccess::tryReadPointer(player.P_ObservedPlayerController + sdk::ObservedPlayerController::MovementController, movementController) ||
        !PlayerMemoryAccess::tryReadPointer(movementController + sdk::ObservedMovementController::ObservedPlayerStateContext, player.P_MovementContext))
    {
        return std::nullopt;
    }

    player.P_CorpseAddr = player.P_ObservedHealthController + sdk::ObservedHealthController::PlayerCorpse;
    player.P_RotationAddress = player.P_MovementContext + sdk::ObservedPlayerStateContext::Rotation;
    player.P_VelocityAddress = player.P_MovementContext + sdk::ObservedPlayerStateContext::Velocity;

    if (!Utils::valid_pointer(player.P_CorpseAddr) ||
        !Utils::valid_pointer(player.P_InventoryControllerAddr) ||
        !Utils::valid_pointer(player.P_HandsControllerAddr) ||
        !Utils::valid_pointer(player.P_RotationAddress))
    {
        return std::nullopt;
    }

    if (!PlayerMemoryAccess::tryRead(instance + sdk::ObservedPlayerView::IsAI, player.isAi) ||
        !PlayerMemoryAccess::tryRead(instance + sdk::ObservedPlayerView::Side, player.playerSide))
    {
        return std::nullopt;
    }

    player.side = sideToString(player.playerSide);
    const bool isSavage = (static_cast<uint32_t>(player.playerSide) & static_cast<uint32_t>(EPlayerSide::Savage)) != 0;

    if (isSavage && player.isAi)
    {
        uint64_t voicePointer = 0;
        PlayerMemoryAccess::tryReadPointer(instance + sdk::ObservedPlayerView::Voice, voicePointer);
        const AIRole role = getAiRole(PlayerMemoryAccess::readString(voicePointer));
		player.type = role.Type;
        player.name = role.Name.empty() ? "Ai" : role.Name;
        player.isBoss = role.Type == PlayerType::AIBoss;
        player.isBlackDivision = role.IsBlackDivision;
        player.isCultist = role.IsCultist;
        player.isPlayerScav = false;
        player.isPlayer = false;
    }
    else if (isSavage)
    {
		player.type = PlayerType::PScav;
        player.name = "PScav " + std::to_string(mainGame.pmcNumber++);
        player.isPlayerScav = true;
        player.isAi = false;
        player.isPlayer = true;
    }
    else
    {
		player.type = PlayerType::PMC;
        player.name = "PMC " + std::to_string(mainGame.pmcNumber++);
        player.isPlayerScav = false;
        player.isAi = false;
        player.isPlayer = true;
    }

    return player;
}

void ObservedPlayer::prepareRefresh(const Player& player, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const
{
    read = {};
    read.instance = player.instance;
    read.kind = player.getKind();
    read.rotationAddress = player.P_RotationAddress;
    read.corpseAddress = player.P_CorpseAddr;
    read.handsControllerAddress = player.P_HandsControllerAddr;
    read.observedHealthController = player.P_ObservedHealthController;
    read.velocityAddress = player.P_VelocityAddress;
    read.rotationRaw = player.rotationRAW;
    read.velocity = player.velocity;
    read.corpseClass = player.P_CorpseClass;
    read.handsController = player.P_HandsController;
    read.healthTag = player.healthETAG;
    read.corpseDue = player.nextCorpseRead == std::chrono::steady_clock::time_point{} || context.now >= player.nextCorpseRead;
    read.handsDue = player.nextHandsControllerRead == std::chrono::steady_clock::time_point{} || context.now >= player.nextHandsControllerRead;
    read.healthDue = player.nextHealthRead == std::chrono::steady_clock::time_point{} || context.now >= player.nextHealthRead;
}

void ObservedPlayer::queueRefresh(ScatterReadBatch& batch, PlayerRuntimeRead& read, const PlayerRefreshContext& context) const
{
    read.rotationQueued = batch.AddBytes(read.rotationAddress, &read.rotationRaw, sizeof(glm::vec2));

    if (context.predictionEnabled && Utils::valid_pointer(read.velocityAddress))
        read.velocityQueued = batch.Add(read.velocityAddress, read.velocity);

    if (read.corpseDue)
        read.corpseQueued = batch.Add(read.corpseAddress, read.corpseClass);

    if (read.handsDue)
        read.handsQueued = batch.Add(read.handsControllerAddress, read.handsController);

    if (read.healthDue)
        read.healthQueued = batch.AddBytes(read.observedHealthController + sdk::ObservedHealthController::HealthStatus, &read.healthTag, sizeof(ETagStatus));
}

void ObservedPlayer::applyRefresh(Player& player, const PlayerRuntimeRead& read, bool executed, const PlayerRefreshContext& context) const
{
    if (executed)
    {
        if (read.rotationQueued)
            player.rotationRAW = read.rotationRaw;

        if (read.velocityQueued)
        {
            const float speed = glm::length(read.velocity);
            const bool velocityValid = std::isfinite(read.velocity.x) && std::isfinite(read.velocity.y) && std::isfinite(read.velocity.z) && std::isfinite(speed) && speed >= 0.1f && speed <= 15.0f;
            player.velocity = velocityValid ? read.velocity : glm::vec3{};
            player.velocityValid = velocityValid;
            player.lastVelocityUpdate = context.now;
        }

        if (read.corpseQueued)
            player.P_CorpseClass = read.corpseClass;

        if (read.handsQueued)
            player.P_HandsController = read.handsController;

        if (read.healthQueued)
            player.healthETAG = read.healthTag;
    }

    if (read.corpseDue)
        player.nextCorpseRead = executed && read.corpseQueued ? context.now + corpseReadInterval : context.now + failedReadRetryInterval;

    if (read.handsDue)
        player.nextHandsControllerRead = executed && read.handsQueued ? context.now + handsReadInterval : context.now + failedReadRetryInterval;

    if (read.healthDue)
        player.nextHealthRead = executed && read.healthQueued ? context.now + healthReadInterval : context.now + failedReadRetryInterval;
}
