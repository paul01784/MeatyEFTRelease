#include "../../UI/includes.h"
#include "RegisteredPlayers.h"
#include "Player/PlayerClassifier.h"

#include "../../UI/globals.h"
#include "MainGame.h"
#include "../../Core/Utilities.h"
#include "../../memory/Memory.h"

void RegisteredPlayers::tryFindBTR()
{
    

    if (!mem.vHandle)
        return;

    std::string selectedMap = TrimEFT(mainGame.selectedLocation);

    std::transform(
        selectedMap.begin(),
        selectedMap.end(),
        selectedMap.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (selectedMap != "tarkovstreets" && selectedMap != "woods")
        return;

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return;

    // Safe read helpers
    auto TryReadValue = [&](uint64_t address, auto& out) -> bool
        {
            using T = std::decay_t<decltype(out)>;

            out = {};

            if (!Utils::valid_pointer(address))
                return false;

            try
            {
                return mem.Read(address, &out, sizeof(T));
            }
            catch (...)
            {
                return false;
            }
        };

    auto TryReadPtr = [&](uint64_t address, uint64_t& out) -> bool
        {
            out = 0;

            if (!TryReadValue(address, out))
                return false;

            return Utils::valid_pointer(out);
        };

    // localGameWorld -> btrController -> btrView -> turret -> attachedBot
    uint64_t btrController = 0;
    uint64_t btrView = 0;
    uint64_t btrTurret = 0;
    uint64_t btrOper = 0;

    if (!TryReadPtr(
        mainGame.localGameWorld + sdk::ClientLocalGameWorld::btrController,
        btrController))
    {
        return;
    }

    if (!TryReadPtr(
        btrController + sdk::BtrController::BtrView,
        btrView))
    {
        return;
    }

    if (!TryReadPtr(
        btrView + sdk::BTRView::turret,
        btrTurret))
    {
        return;
    }

    if (!TryReadPtr(
        btrTurret + sdk::BTRTurretView::attachedBot,
        btrOper))
    {
        return;
    }

    std::vector<Player>& cache = registeredPlayers.getCache();

    if (cache.empty())
        return;

    // Find the AI/player cache entry matching attachedBot
    for (auto& cachePlayer : cache)
    {
        if (!Utils::valid_pointer(cachePlayer.instance))
            continue;

        if (cachePlayer.instance != btrOper)
            continue;

        
        if (cachePlayer.isLocal || cachePlayer.isPlayer || cachePlayer.isPlayerScav)
            return;

        const bool wasAlreadyBTR = cachePlayer.isBTR;
        const uint64_t oldBtrView = cachePlayer.btrView;

        PlayerClassifier::get(PlayerKind::Btr).initialize(cachePlayer);
        cachePlayer.btrView = btrView;

        glm::vec3 btrPosition{};

        if (TryReadValue(btrView + sdk::BTRView::previousPosition, btrPosition))
        {
            cachePlayer.location = btrPosition;
            cachePlayer.distance = getDistance(cachePlayer.location, mainGame.localLocation);
        }

        if (!mainGame.btrAllocated || !wasAlreadyBTR || oldBtrView != btrView)
        {
            mainGame.btrAllocated = true;

            std::ostringstream ss;
            ss << "[BTR] BTR Allocated | operator: 0x"
                << std::hex << btrOper
                << " view: 0x"
                << btrView;

            LOGS.logInfo(ss.str());
        }

        return;
    }
}

void RegisteredPlayers::recoverBtrStuckPlayers()
{
    using Clock = std::chrono::steady_clock;

    static constexpr float kBtrRadius = 4.0f;
    static constexpr float kBtrRadiusSquared = kBtrRadius * kBtrRadius;
    static constexpr float kStaticRotationEpsilon = 0.75f;
    static constexpr int kStaticRotationTicks = 5;
    static constexpr auto kStuckDuration = std::chrono::milliseconds(600);
    static constexpr auto kRecoveryCooldown = std::chrono::seconds(5);

    const Clock::time_point now = Clock::now();

    auto ResetTracking = [](Player& player)
        {
            player.isInBTR = false;
            player.btrNearSince = {};
            player.lastBtrRotation = 0.0f;
            player.btrStaticRotationTicks = 0;
            player.hasBtrRotationSample = false;
        };

    auto IsFinitePosition = [](const glm::vec3& position)
        {
            return std::isfinite(position.x) &&
                std::isfinite(position.y) &&
                std::isfinite(position.z);
        };

    auto IsNear = [](const glm::vec3& first, const glm::vec3& second)
        {
            const glm::vec3 delta = first - second;

            return
                (delta.x * delta.x) +
                (delta.y * delta.y) +
                (delta.z * delta.z) <=
                kBtrRadiusSquared;
        };

    auto RotationNearlyEqual = [](float first, float second)
        {
            float difference = std::fmod(
                std::fabs(first - second),
                360.0f
            );

            if (difference > 180.0f)
                difference = 360.0f - difference;

            return difference <= kStaticRotationEpsilon;
        };

    std::lock_guard<std::mutex> lock(playerMutex);

    std::vector<Player>& cache = registeredPlayers.getCache();

    if (cache.empty())
        return;

    std::vector<glm::vec3> btrPositions;
    btrPositions.reserve(1);

    for (const Player& player : cache)
    {
        if (!player.isBTR || !IsFinitePosition(player.location))
            continue;

        const float positionMagnitudeSquared =
            (player.location.x * player.location.x) +
            (player.location.y * player.location.y) +
            (player.location.z * player.location.z);

        if (positionMagnitudeSquared > 1.0f)
            btrPositions.emplace_back(player.location);
    }

    if (btrPositions.empty())
    {
        for (Player& player : cache)
            ResetTracking(player);

        return;
    }

    for (Player& player : cache)
    {
        if (player.isBTR)
            continue;

        const bool isHuman =
            player.isLocal ||
            (!player.isAi &&
                (player.isPlayer || player.isPlayerScav));

        if (!isHuman ||
            player.isDead ||
            player.hasExfiled ||
            !Utils::valid_pointer(player.instance) ||
            !IsFinitePosition(player.location))
        {
            ResetTracking(player);
            continue;
        }

        const bool nearBtr = std::any_of(
            btrPositions.begin(),
            btrPositions.end(),
            [&](const glm::vec3& btrPosition)
            {
                return IsNear(player.location, btrPosition);
            }
        );

        if (!nearBtr)
        {
            ResetTracking(player);
            continue;
        }

        player.isInBTR = true;

        const float currentRotation = player.rotation.x;

        if (!std::isfinite(currentRotation))
        {
            ResetTracking(player);
            continue;
        }

        if (!player.hasBtrRotationSample)
        {
            player.btrNearSince = now;
            player.lastBtrRotation = currentRotation;
            player.btrStaticRotationTicks = 0;
            player.hasBtrRotationSample = true;
            continue;
        }

        if (RotationNearlyEqual(
            currentRotation,
            player.lastBtrRotation))
        {
            ++player.btrStaticRotationTicks;
        }
        else
        {
            player.btrStaticRotationTicks = 0;
        }

        player.lastBtrRotation = currentRotation;

        if (now - player.btrNearSince < kStuckDuration ||
            player.btrStaticRotationTicks >= kStaticRotationTicks ||
            now < player.nextBtrRecovery)
        {
            continue;
        }

        // Refresh only the transform hierarchy
        player.playerBoneMatrixPtr = 0;
        player.bonePointersNeedResolve = true;
        player.invalidBones = true;

        std::fill(player.bonePtrs.begin(), player.bonePtrs.end(), 0ULL);

        std::fill(player.bonePositions.begin(), player.bonePositions.end(), glm::vec3(0.0f));

        player.boneTransformCache.clear();
        player.nextBtrRecovery = now + kRecoveryCooldown;

        std::ostringstream message;
        message << "[BTR][RECOVERY] Refreshing stuck player transforms: "
            << player.name
            << " (0x"
            << std::hex
            << player.instance
            << ')';

        LOGS.logInfo(message.str());

        ResetTracking(player);
        player.isInBTR = true;
    }
}
