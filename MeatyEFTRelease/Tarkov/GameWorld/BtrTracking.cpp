#include "../../UI/includes.h"
#include "RegisteredPlayers.h"
#include "Player/PlayerClassifier.h"
#include "Player/PlayerLookup.h"

#include "../../UI/globals.h"
#include "MainGame.h"
#include "../../Core/Utilities.h"
#include "../../memory/Memory.h"

#include <chrono>

namespace
{
    constexpr float kBtrHeadingDistanceSquared = 0.0025f;
    constexpr float kBtrMaximumMotionSquared = 25.0f;
    constexpr float kRadiansToDegrees = 57.295779513f;

    bool IsUsableBtrPosition(const glm::vec3& position)
    {
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z) &&
            position.x * position.x + position.y * position.y + position.z * position.z > 1.0f;
    }
}

void RegisteredPlayers::tryFindBTR()
{
    if (!mem.vHandle)
        return;

    static auto nextSearch = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now < nextSearch)
        return;

    nextSearch = now + std::chrono::seconds(1);

    std::string selectedMap = TrimEFT(mainGame.selectedLocation);

    std::transform(
        selectedMap.begin(),
        selectedMap.end(),
        selectedMap.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (selectedMap != "tarkovstreets" && selectedMap != "woods" && selectedMap != "lighthouse")
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

            DWORD bytesRead = 0;
            const Memory::ScatterReadRequest request{ address, &out, sizeof(T) };
            return mem.TryReadScatter(&request, 1, DmaCacheMode::Cached, "BTR discovery", std::span<DWORD>(&bytesRead, 1)) == Memory::TryScatterReadResult::Success && bytesRead == sizeof(T);
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
        if (!wasAlreadyBTR || oldBtrView != btrView)
        {
            cachePlayer.btrPositionSampled = false;
            cachePlayer.btrHeadingValid = false;
            cachePlayer.rotation.x = 0.0f;
        }

        PlayerClassifier::get(PlayerKind::Btr).initialize(cachePlayer);
        cachePlayer.btrController = btrController;
        cachePlayer.btrView = btrView;
        cachePlayer.btrTurret = btrTurret;

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

bool RegisteredPlayers::updateBtrMotion()
{
    struct BtrMotionRead
    {
        uint64_t instance{};
        uint64_t cacheEntryId{};
        uint64_t view{};
        glm::vec3 position{};
    };

    BtrMotionRead read{};

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (const Player& player : playerCache)
        {
            if (!player.isBTR || player.isDead || player.hasExfiled ||
                !Utils::valid_pointer(player.instance) || !Utils::valid_pointer(player.btrView))
            {
                continue;
            }

            read.instance = player.instance;
            read.cacheEntryId = player.cacheEntryId;
            read.view = player.btrView;
            break;
        }
    }

    if (!Utils::valid_pointer(read.view))
        return false;

    DWORD bytesRead = 0;
    const Memory::ScatterReadRequest request{
        read.view + sdk::BTRView::previousPosition,
        &read.position,
        sizeof(read.position)
    };
    const ScopedDmaPriority priority(DmaPriority::High);
    const auto result = mem.TryReadScatter(
        &request,
        1,
        DmaCacheMode::Uncached,
        "BTR motion",
        std::span<DWORD>(&bytesRead, 1));

    if (result != Memory::TryScatterReadResult::Success || bytesRead != sizeof(read.position) ||
        !IsUsableBtrPosition(read.position))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(playerMutex);
    Player* player = PlayerLookup::findByInstance(playerCache, read.instance);

    if (!player || player->cacheEntryId != read.cacheEntryId || player->btrView != read.view || !player->isBTR)
        return false;

    player->location = read.position;
    player->distance = getDistance(read.position, mainGame.localLocation);

    if (!player->btrPositionSampled)
    {
        player->btrPreviousPosition = read.position;
        player->btrPositionSampled = true;
        return true;
    }

    const glm::vec3 movement = read.position - player->btrPreviousPosition;
    const float movementSquared = movement.x * movement.x + movement.z * movement.z;

    if (movementSquared >= kBtrHeadingDistanceSquared && movementSquared <= kBtrMaximumMotionSquared)
    {
        player->rotation.x = std::atan2(-movement.z, movement.x) * kRadiansToDegrees;
        player->btrHeadingValid = true;
        player->btrPreviousPosition = read.position;
    }
    else if (movementSquared > kBtrMaximumMotionSquared)
    {
        player->btrPreviousPosition = read.position;
    }

    return true;
}

void RegisteredPlayers::updateBtrPassengerStates()
{
    constexpr float kPassengerRadiusSquared = 25.0f;
    constexpr float kEntryMinimumAngle = 80.0f;
    constexpr float kEntryMaximumAngle = 100.0f;
    constexpr float kRetainedMinimumAngle = 70.0f;
    constexpr float kRetainedMaximumAngle = 110.0f;
    constexpr std::size_t kPassengerCapacity = 4;

    struct BtrVehicle
    {
        Player* player{};
        std::size_t passengerCount{};
    };

    struct BtrCandidate
    {
        Player* player{};
        BtrVehicle* vehicle{};
        float distanceSquared{};
    };

    std::lock_guard<std::mutex> lock(playerMutex);
    std::vector<Player>& cache = registeredPlayers.getCache();
    std::vector<BtrVehicle> vehicles;
    vehicles.reserve(1);

    for (Player& player : cache)
    {
        if (!player.isBTR)
            continue;

        if (player.isDead || player.hasExfiled || !Utils::valid_pointer(player.instance) || !IsUsableBtrPosition(player.location))
        {
            player.btrPositionSampled = false;
            player.btrHeadingValid = false;
            continue;
        }
        vehicles.push_back({ &player });
    }

    std::vector<BtrCandidate> candidates;
    candidates.reserve(cache.size());

    for (Player& player : cache)
    {
        if (player.isBTR)
            continue;

        const bool human = player.isLocal || (!player.isAi && (player.isPlayer || player.isPlayerScav));
        if (!human || player.isDead || player.hasExfiled || !Utils::valid_pointer(player.instance) ||
            !IsUsableBtrPosition(player.location) || !std::isfinite(player.rotation.x))
        {
            continue;
        }

        BtrVehicle* nearestVehicle = nullptr;
        float nearestDistanceSquared = kPassengerRadiusSquared;

        for (BtrVehicle& vehicle : vehicles)
        {
            const Player& btr = *vehicle.player;
            if (!btr.btrHeadingValid)
                continue;

            const glm::vec3 difference = player.location - btr.location;
            const float distanceSquared = difference.x * difference.x + difference.y * difference.y + difference.z * difference.z;
            if (distanceSquared > nearestDistanceSquared)
                continue;

            const float angle = std::fabs(std::remainder(player.rotation.x - btr.rotation.x, 360.0f));
            const bool retained = player.isInBTR && player.btrPassengerVehicle == btr.instance;
            const float minimumAngle = retained ? kRetainedMinimumAngle : kEntryMinimumAngle;
            const float maximumAngle = retained ? kRetainedMaximumAngle : kEntryMaximumAngle;
            if (angle < minimumAngle || angle > maximumAngle)
                continue;

            nearestVehicle = &vehicle;
            nearestDistanceSquared = distanceSquared;
        }

        if (nearestVehicle)
            candidates.push_back({ &player, nearestVehicle, nearestDistanceSquared });
    }

    std::sort(candidates.begin(), candidates.end(), [](const BtrCandidate& left, const BtrCandidate& right)
    {
        const bool leftRetained = left.player->isInBTR && left.player->btrPassengerVehicle == left.vehicle->player->instance;
        const bool rightRetained = right.player->isInBTR && right.player->btrPassengerVehicle == right.vehicle->player->instance;
        if (leftRetained != rightRetained)
            return leftRetained;
        if (left.distanceSquared != right.distanceSquared)
            return left.distanceSquared < right.distanceSquared;
        return left.player->instance < right.player->instance;
    });

    std::vector<std::pair<Player*, std::uint64_t>> passengers;
    passengers.reserve(vehicles.size() * kPassengerCapacity);
    for (BtrCandidate& candidate : candidates)
    {
        if (candidate.vehicle->passengerCount >= kPassengerCapacity)
            continue;
        passengers.emplace_back(candidate.player, candidate.vehicle->player->instance);
        ++candidate.vehicle->passengerCount;
    }

    for (Player& player : cache)
    {
        if (player.isBTR)
            continue;

        const auto selected = std::find_if(passengers.begin(), passengers.end(), [&player](const auto& entry)
        {
            return entry.first == &player;
        });
        const std::uint64_t nextVehicle = selected == passengers.end() ? 0 : selected->second;

        if (player.isInBTR && player.btrPassengerVehicle != nextVehicle && !player.isDead && !player.hasExfiled)
        {
            // Passenger transforms can remain attached to the vehicle after exit.
            // Force a fresh hierarchy resolve before drawing the player's bones again.
            player.playerBoneMatrixPtr = 0;
            player.bonePointersNeedResolve = true;
            player.invalidBones = true;
            player.minimalBonePointersReady = false;
            player.fullBonePointersReady = false;
            player.minimalBonePoseValid = false;
            player.fullBonePoseValid = false;
            player.consecutiveBadBoneSamples = 0;
            player.nextBonePointerResolve = {};
            std::fill(player.bonePtrs.begin(), player.bonePtrs.end(), 0ULL);
            std::fill(player.bonePositions.begin(), player.bonePositions.end(), glm::vec3(0.0f));
            player.boneTransformCache.clear();
            player.btrExitBoneRefreshPending = true;
            LOGS.logInfo("[BTR] Passenger exited; refreshing bones: " + player.name);
        }

        player.isInBTR = nextVehicle != 0;
        player.btrPassengerVehicle = nextVehicle;
    }
}
