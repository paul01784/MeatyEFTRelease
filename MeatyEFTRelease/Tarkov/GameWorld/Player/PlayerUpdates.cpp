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
#include "../../Unity/cameraManager.h"

#include <chrono>
#include <cmath>

namespace
{
    bool isLocalGroupRosterProtectionActive(const PlayerCollection& cache)
    {
        constexpr int maximumRegisteredPlayers = 512;

        if (mainGame.localGroupId.empty())
            return false;

        for (const Player& player : cache)
        {
            const bool isLocalPlayer =
                player.isLocal ||
                (Utils::valid_pointer(mainGame.localPlayerPtr) && player.instance == mainGame.localPlayerPtr);

            if (isLocalPlayer && (player.isDead || player.hasExfiled))
                return true;
        }

        if (!Utils::valid_pointer(mainGame.localPlayerPtr))
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

    bool isUsableWorldPosition(const glm::vec3& position)
    {
        constexpr float kMaximumCoordinate = 1000000.0f;

        return std::isfinite(position.x) &&
            std::isfinite(position.y) &&
            std::isfinite(position.z) &&
            std::fabs(position.x) <= kMaximumCoordinate &&
            std::fabs(position.y) <= kMaximumCoordinate &&
            std::fabs(position.z) <= kMaximumCoordinate &&
            (std::fabs(position.x) >= 0.001f ||
                std::fabs(position.y) >= 0.001f ||
                std::fabs(position.z) >= 0.001f);
    }

    bool tryGetLocalCameraPositionFallback(const Player& player, std::chrono::steady_clock::time_point now, glm::vec3& position)
    {
        constexpr auto kMaximumCameraSampleAge = std::chrono::milliseconds(250);

        const CameraManagerSnapshot camera = cameraManagerTest.snapshot();
        if (!camera ||
            !camera->valid ||
            !camera->fpsCameraWorldPositionValid ||
            camera->publishedAt == std::chrono::steady_clock::time_point{} ||
            now < camera->publishedAt ||
            (now - camera->publishedAt) > kMaximumCameraSampleAge ||
            !isUsableWorldPosition(camera->fpsCameraWorldPosition))
        {
            return false;
        }

        position = camera->fpsCameraWorldPosition;

        // The FPS camera is at eye height. Retain the last trustworthy base
        // height so the radar marker remains on the player's floor position.
        if (isUsableWorldPosition(player.location))
            position.y = player.location.y;

        return isUsableWorldPosition(position);
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

    const bool rosterProtectionActive = isLocalGroupRosterProtectionActive(playerCache);

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

            const bool isProtectedGroupMember =
                isProtectedLocalGroupMember(player, rosterProtectionActive);

            if (isProtectedGroupMember)
            {
                player.isDead = false;
                player.hasExfiled = false;
            }

            if (player.isDead || player.hasExfiled)
            {
                player.distance = getDistance(player.location, mainGame.localLocation);
                PlayerAppearance::updateColour(player);
                continue;
            }

            if (!isProtectedGroupMember &&
                Utils::valid_pointer(player.P_CorpseClass))
            {
                player.isDead = true;
                player.distance = getDistance(player.location, mainGame.localLocation);
                PlayerAppearance::updateColour(player);
                continue;
            }

            if (!Utils::valid_pointer(player.instance))
                continue;

            glm::vec3 location = PlayerPosition::getBestBasePosition(player);
            const bool liveLocalPositionAvailable = !player.isLocal || !player.bonePointersNeedResolve || player.internalTransformPositionValid;
            bool usingCameraPositionFallback = false;

            if (player.isLocal && !liveLocalPositionAvailable)
                usingCameraPositionFallback = tryGetLocalCameraPositionFallback(player, context.now, location);

            if (location.x != 0.0f || location.y != 0.0f || location.z != 0.0f)
                player.location = location;

            if (player.isLocal)
            {
                if (usingCameraPositionFallback && !player.usingCameraPositionFallback)
                    LOGS.logNotice(NoticeColour::RED, "Local player pose unavailable, using FPS camera position fallback");
                else if (liveLocalPositionAvailable && player.usingCameraPositionFallback)
                    LOGS.logNotice(NoticeColour::GREEN, "Local player position recovered from pose data");

                if (usingCameraPositionFallback || liveLocalPositionAvailable)
                    player.usingCameraPositionFallback = usingCameraPositionFallback;

                mainGame.localLocation = player.location;
            }

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
