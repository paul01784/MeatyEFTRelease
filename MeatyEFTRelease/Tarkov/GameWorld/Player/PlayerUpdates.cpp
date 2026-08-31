#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerAppearance.h"
#include "PlayerClassifier.h"
#include "PlayerLookup.h"
#include "PlayerPosition.h"

#include "../../../Core/Utilities.h"
#include "../../../UI/aimLineTargeting.h"
#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "../MainGame.h"

#include <chrono>

namespace
{
    bool isLocalGroupRosterProtectionActive()
    {
        constexpr int maximumRegisteredPlayers = 512;

        if (!Utils::valid_pointer(mainGame.localPlayerPtr) || mainGame.localGroupId.empty())
            return false;

        if (mainGame.registeredPlayersCount <= 0 || mainGame.registeredPlayersCount > maximumRegisteredPlayers)
            return false;

        bool hasRegisteredPlayer = false;

        for (int index = 0; index < mainGame.registeredPlayersCount; ++index)
        {
            const uint64_t instance = mainGame.player_buffer[index];

            if (!Utils::valid_pointer(instance))
                continue;

            hasRegisteredPlayer = true;

            if (instance == mainGame.localPlayerPtr)
                return false;
        }

        return hasRegisteredPlayer;
    }

    bool isProtectedLocalGroupMember(const Player& player, bool rosterProtectionActive)
    {
        return rosterProtectionActive && !player.isLocal && player.instance != mainGame.localPlayerPtr && player.groupId == mainGame.localGroupId;
    }

    void resetAimLineTarget(Player& player)
    {
        player.aimLineTargetConfirmed = false;
        player.aimLineTargetIsLocal = false;
        player.aimLineTargetLocation = {};
        player.aimLineTargetSince = {};
    }

    void updateAimLineTarget(Player& player, const PlayerCollection& cache, std::chrono::steady_clock::time_point now)
    {
        glm::vec3 targetLocation{};
        bool targetIsLocal = false;

        if (!AimLineTargeting::FindLookedAtTarget(
            player,
            cache,
            mainGame.localLocation,
            mainGame.localGroupId,
            radarGlobals::aimLineTargetAngle,
            static_cast<float>(radarGlobals::aimLineTargetMaxDistance),
            targetLocation,
            targetIsLocal))
        {
            resetAimLineTarget(player);
            return;
        }

        player.aimLineTargetLocation = targetLocation;
        player.aimLineTargetIsLocal = targetIsLocal;

        if (player.aimLineTargetSince == std::chrono::steady_clock::time_point{})
        {
            player.aimLineTargetSince = now;
            player.aimLineTargetConfirmed = false;
            return;
        }

        player.aimLineTargetConfirmed = now - player.aimLineTargetSince >= std::chrono::seconds(1);
    }
}

void RegisteredPlayers::updateEntity()
{
    if (!mem.vHandle)
        return;

    const PlayerRefreshContext context{ std::chrono::steady_clock::now(), aimGlobals::predictionEnabled };
    std::vector<PlayerRuntimeRead> reads;

    {
        std::lock_guard<std::mutex> lock(playerMutex);
        reads.reserve(playerCache.size());

        for (const Player& player : playerCache)
        {
            if (!Utils::valid_pointer(player.instance) || (!player.isBTR && (player.isDead || player.hasExfiled)))
                continue;

            PlayerRuntimeRead read{};
            PlayerClassifier::get(player).prepareRefresh(player, read, context);
            reads.emplace_back(std::move(read));
        }
    }

    bool executed = true;
    bool queuedAnything = false;

    if (!reads.empty())
    {
        ScatterReadBatch batch(mem, DmaCacheMode::Uncached, "Player update");

        if (!batch.Valid())
        {
            LOGS.logError("[PLAYERS][UPDATE] Failed to create scatter handle");
            return;
        }

        for (PlayerRuntimeRead& read : reads)
        {
            PlayerClassifier::get(read.kind).queueRefresh(batch, read, context);
            queuedAnything = queuedAnything || read.locationQueued || read.rotationQueued || read.velocityQueued || read.corpseQueued || read.healthQueued || read.handsQueued || read.aimingQueued;
        }

        if (queuedAnything)
            executed = batch.Execute();
    }

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (const PlayerRuntimeRead& read : reads)
        {
            Player* player = PlayerLookup::findByInstance(playerCache, read.instance);

            if (player)
                PlayerClassifier::get(read.kind).applyRefresh(*player, read, executed, context);
        }
    }

    if (!executed)
    {
        LOGS.logError("[PLAYERS][UPDATE] Player scatter execute failed");
        return;
    }

    const bool rosterProtectionActive = isLocalGroupRosterProtectionActive();

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (Player& player : playerCache)
        {
            if (player.isBTR)
            {
                player.colour = coloursGlobals::aiBTR;
                player.distance = getDistance(player.location, mainGame.localLocation);
                continue;
            }

            if (isProtectedLocalGroupMember(player, rosterProtectionActive))
            {
                player.isDead = false;
                player.hasExfiled = false;
                player.P_CorpseClass = 0;
            }

            if (player.isDead || player.hasExfiled)
            {
                player.distance = getDistance(player.location, mainGame.localLocation);
                PlayerAppearance::updateColour(player);
                continue;
            }

            if (Utils::valid_pointer(player.P_CorpseClass))
            {
                player.isDead = true;
                player.distance = getDistance(player.location, mainGame.localLocation);
                PlayerAppearance::updateColour(player);
                continue;
            }

            if (!Utils::valid_pointer(player.instance))
                continue;

            const glm::vec3 location = PlayerPosition::getBestBasePosition(player);

            if (location.x != 0.0f || location.y != 0.0f || location.z != 0.0f)
                player.location = location;

            if (player.isLocal)
                mainGame.localLocation = player.location;

            player.distance = getDistance(player.location, mainGame.localLocation);

            try
            {
                player.rotation = Utils::Player::Rotation::correctRotation2d(player.rotationRAW);
            }
            catch (...)
            {
                player.rotation = {};
                LOGS.logError("[PLAYERS][UPDATE] Rotation correction failed");
            }

            if (!Utils::valid_pointer(player.P_HandsController))
            {
                player.itemInHand.clear();
                player.observedHandsInfo.reset();
                player.lastHeldItemHandsController = 0;
                player.nextHeldItemRefresh = {};
            }
            else if (player.lastHeldItemHandsController != player.P_HandsController)
            {
                player.lastHeldItemHandsController = player.P_HandsController;
                player.nextHeldItemRefresh = context.now;
            }

            PlayerAppearance::updateColour(player);

            if (player.isLocal && mainGame.localPlayerPtr == player.instance)
            {
                mainGame.localLocation = player.location;
                mainGame.localRotation = player.rotation;
                mainGame.localGroupId = player.groupId;
                mainGame.localPlayerHands = player.P_HandsController;
                mainGame.localIsScoped = player.isAiming;
                mainGame.localPlayerPWA = player.P_PWA;
                player.colour = coloursGlobals::playerLocal;
            }
        }

        for (Player& player : playerCache)
        {
            if (!radarGlobals::drawAimLineTargets ||
                !Utils::valid_pointer(player.instance) ||
                player.isLocal ||
                player.isBTR ||
                player.isInBTR ||
                player.isDead ||
                player.hasExfiled ||
                player.isZombie)
            {
                resetAimLineTarget(player);
                continue;
            }

            updateAimLineTarget(player, playerCache, context.now);
        }
    }
}
