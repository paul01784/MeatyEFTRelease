#include "../app/includes.h"
#include "headers/players.h"
#include "../app/globals.h"
#include "../app/aimLineTargeting.h"

#include "../memory/Memory.h"
#include "../memory/ScatterReadBatch.h"

#include "headers/maingame.h"
#include "../app/debug.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"
#include "headers/tarkovdevquery.h"
#include <cmath>
#include "headers/questManager.h"
#include "headers/loot.h"
#include "headers/wishlist.h"
#include "headers/dogtag.h"
#include "../app/DogTagAPI.h"
#include <chrono>
#include <algorithm>
#include "headers/watchList.h"

std::mutex playerMutex;
Players players;

bool Players::groupIDSet = false;

static glm::vec3 GetBestPlayerBasePosition(const PlayerCache& cachePlayer);

Players::Players()
    : publishedPlayerCache(
        std::make_shared<const PlayerCacheCollection>())
{
}

namespace
{
    static std::int64_t SteadyClockTicks() noexcept
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    static void EnsureTransformCacheShape(PlayerCache& player);

    static bool HasMinimalBonePointers(const PlayerCache& player);

    static bool IsMinimalBoneSlot(int slot);

    static bool IsUsableBonePosition(const glm::vec3& position);

std::string SideToString(EPlayerSide side)
{
    switch (side)
    {
    case EPlayerSide::Usec:   return "Usec";
    case EPlayerSide::Bear:   return "Bear";
    case EPlayerSide::Savage: return "Savage";
    default:                  return "Unknown";
    }
}

int CalculateKD(uint32_t kills, uint32_t deaths)
{
    if (deaths == 0)
    {
        if (kills > 0)
            return kills;   // flawless → KD equals kills
        return 0;            // no combat
    }

    float kd = static_cast<float>(kills) / static_cast<float>(deaths);

    // round to nearest whole number
    return static_cast<int>(std::round(kd));
}

double CalculatePKD(uint32_t kills, uint32_t deaths)
{
    if (deaths == 0)
    {
        if (kills > 0)
            return static_cast<double>(kills);
        return 0.0;            // no combat
    }
    double pkd = static_cast<double>(kills) / static_cast<double>(deaths);
    // round to two decimal places
    return std::round(pkd * 100.0) / 100.0;
}

int ConvertXpToLevel(int xp)
{
    const auto& t = LevelXpThresholds;

    for (int level = 1; level < static_cast<int>(t.size()); ++level)
    {
        if (xp < t[level])
            return level;
    }

    // If XP exceeds the highest value themn max level
    return static_cast<int>(t.size());
}

} // namespace

int Players::getDistance(glm::vec3 point1, glm::vec3 point2)
{
    float dx = point1.x - point2.x;
    float dy = point1.y - point2.y;
    float dz = point1.z - point2.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

std::string Players::voice2Name(std::string voiceName)
{
    if (voiceName.find("BossSanitar") != std::string::npos) {
        return "Sanitar";
    }
    else if (voiceName.find("BossBully") != std::string::npos) {
        return "BossBully";
    }
    else if (voiceName.find("BossGluhar") != std::string::npos) {
        return "Gluhar";
    }
    else if (voiceName.find("SectantPriest") != std::string::npos) {
        return "Priest";
    }
    else if (voiceName.find("SectantWarrior") != std::string::npos) {
        return "Warrior";
    }
    else if (voiceName.find("BossKilla") != std::string::npos) {
        return "Killa";
    }
    else if (voiceName.find("BossTagilla") != std::string::npos) {
        return "Tagilla";
    }
    else if (voiceName.find("Boss_Partizan") != std::string::npos) {
        return "Partizan";
    }
    else if (voiceName.find("BossBigPipe") != std::string::npos) {
        return "BigPipe";
    }
    else if (voiceName.find("BossBirdEye") != std::string::npos) {
        return "BirdEye";
    }
    else if (voiceName.find("BossKnight") != std::string::npos) {
        return "Knight";
    }
    else if (voiceName.find("Arena_Guard_1") != std::string::npos) {
        return "Arena Guard";
    }
    else if (voiceName.find("Arena_Guard_2") != std::string::npos) {
        return "Arena Guard";
    }
    else if (voiceName.find("Boss_Kaban") != std::string::npos) {
        return "Kaban";
    }
    else if (voiceName.find("Boss_Kollontay") != std::string::npos) {
        return "Kollontay";
    }
    else if (voiceName.find("Boss_Sturman") != std::string::npos) {
        return "Sturman";
    }
    else if (voiceName.find("Zombie_Generic") != std::string::npos) {
        return "Zombie";
    }
    else if (voiceName.find("BossZombieTagilla") != std::string::npos) {
        return "ZombieTagilla";
    }
    else if (voiceName.find("Zombie_Fast") != std::string::npos) {
        return "Zombie F";
    }
    else if (voiceName.find("Zombie_Medium") != std::string::npos) {
        return "Zombie M";
    }
    else
        return "Ai";
}

void Players::clearCache()
{
    std::lock_guard<std::mutex> lock(playerMutex);
    this->playerCache.clear();
    this->playerGroups.clear();
    boneResolveCursor = 0;
    nextFullBoneUpdate = {};
    lastBoneRefreshLog = {};
    boneRefreshesSinceLastLog = 0;
    players.groupIDSet = false;
    publishCacheSnapshotLocked();

    LOGS.logInfo("[PLAYER][CACHE] Data cleared");
}

void Players::softRestart()
{
    std::lock_guard<std::mutex> lock(playerMutex);

    players.groupIDSet = false;
    mainGame.localGroupId = "";
    this->playerCache.clear();
    this->playerGroups.clear();
    boneResolveCursor = 0;
    nextFullBoneUpdate = {};
    lastBoneRefreshLog = {};
    boneRefreshesSinceLastLog = 0;
    publishCacheSnapshotLocked();
}

std::vector<PlayerCache>& Players::getCache() {
    return playerCache;
}

PlayerCacheSnapshot Players::getCacheSnapshot() const noexcept
{
    PlayerCacheSnapshot snapshot = publishedPlayerCache.load(
        std::memory_order_acquire);

    if (snapshot)
        return snapshot;

    static const PlayerCacheSnapshot emptySnapshot =
        std::make_shared<const PlayerCacheCollection>();

    return emptySnapshot;
}

PlayerSnapshotTelemetry Players::getSnapshotTelemetry() const noexcept
{
    PlayerSnapshotTelemetry telemetry{};
    const PlayerCacheSnapshot snapshot = getCacheSnapshot();
    const std::int64_t nowTicks = SteadyClockTicks();
    const std::int64_t snapshotTicks =
        publishedSnapshotTicks.load(std::memory_order_acquire);
    const std::int64_t motionTicks =
        publishedMotionTicks.load(std::memory_order_acquire);

    telemetry.snapshotVersion =
        publishedSnapshotVersion.load(std::memory_order_relaxed);
    telemetry.motionVersion =
        publishedMotionVersion.load(std::memory_order_relaxed);
    telemetry.playerCount = snapshot->size();
    telemetry.averageMotionIntervalMs =
        averageMotionIntervalMs.load(std::memory_order_relaxed);

    if (snapshotTicks > 0)
        telemetry.snapshotAgeMs =
            static_cast<double>(nowTicks - snapshotTicks) / 1'000'000.0;

    if (motionTicks > 0)
        telemetry.motionAgeMs =
            static_cast<double>(nowTicks - motionTicks) / 1'000'000.0;

    return telemetry;
}

void Players::publishCacheSnapshotLocked(bool motionUpdated)
{
    const PlayerCacheSnapshot snapshot =
        std::make_shared<const PlayerCacheCollection>(playerCache);
    const std::int64_t nowTicks = SteadyClockTicks();

    publishedPlayerCache.store(snapshot, std::memory_order_release);
    publishedSnapshotTicks.store(nowTicks, std::memory_order_release);
    publishedSnapshotVersion.fetch_add(1, std::memory_order_relaxed);

    if (!motionUpdated)
        return;

    const std::int64_t previousTicks =
        publishedMotionTicks.exchange(nowTicks, std::memory_order_acq_rel);

    if (previousTicks > 0 && nowTicks > previousTicks)
    {
        const double intervalMs =
            static_cast<double>(nowTicks - previousTicks) / 1'000'000.0;
        const double currentAverage =
            averageMotionIntervalMs.load(std::memory_order_relaxed);
        const double nextAverage = currentAverage <= 0.0
            ? intervalMs
            : (currentAverage * 0.88) + (intervalMs * 0.12);

        averageMotionIntervalMs.store(
            nextAverage,
            std::memory_order_relaxed);
    }

    publishedMotionVersion.fetch_add(1, std::memory_order_relaxed);
}

void Players::publishCacheSnapshot(bool motionUpdated)
{
    std::lock_guard<std::mutex> lock(playerMutex);
    publishCacheSnapshotLocked(motionUpdated);
}

void Players::applyGroupEdits(
    const std::vector<std::pair<uint64_t, std::string>>& edits)
{
    if (edits.empty())
        return;

    std::lock_guard<std::mutex> lock(playerMutex);

    for (const auto& [instance, newGroupId] : edits)
    {
        auto player = std::find_if(
            playerCache.begin(),
            playerCache.end(),
            [&](const PlayerCache& candidate)
            {
                return candidate.instance == instance;
            });

        if (player == playerCache.end() ||
            player->isDead ||
            player->hasExfiled)
        {
            continue;
        }

        player->groupId = newGroupId;

        if (player->isLocal || player->instance == mainGame.localPlayerPtr)
        {
            if (newGroupId.empty() || newGroupId == "0")
                mainGame.localGroupId.clear();
            else
                mainGame.localGroupId = newGroupId;
        }

        std::ostringstream message;
        message << "[PLAYERS][GROUP EDIT] "
            << player->name
            << " instance: 0x"
            << std::hex << player->instance
            << " groupId: "
            << (newGroupId.empty() ? "none" : newGroupId);

        LOGS.logInfo(message.str());
    }

    publishCacheSnapshotLocked();
}

std::vector<PlayerGroups>& Players::getGroupCache() {
    return playerGroups;
}

bool Players::getBonePtrs(PlayerCache& player, bool forceResolve)
{
    if (player.isBTR)
        return false;

    if (!Utils::valid_pointer(player.instance))
        return false;

    if (player.boneList.empty())
        return false;

    if (player.bonePtrs.size() != player.boneList.size())
        player.bonePtrs.resize(player.boneList.size(), 0);

    if (player.bonePositions.size() != player.boneList.size())
        player.bonePositions.resize(player.boneList.size(), glm::vec3(0.0f));

    uint64_t resolvedMatrixPtr = player.playerBoneMatrixPtr;

    const bool mustResolveMatrix = forceResolve || !Utils::valid_pointer(resolvedMatrixPtr);

    if (mustResolveMatrix)
    {
        resolvedMatrixPtr = 0;

        try
        {
            const bool isOfflinePlayer =
                player.className == "LocalPlayer" ||
                player.className == "ClientPlayer";

            if (isOfflinePlayer)
            {
                resolvedMatrixPtr = mem.ReadChain(
                    player.instance,
                    {
                        sdk::Player::_playerBody,
                        0x30,
                        0x30,
                        0x10
                    },
                    DmaCacheMode::Uncached
                );
            }
            else
            {
                resolvedMatrixPtr = mem.ReadChain(
                    player.instance,
                    {
                        sdk::ObservedPlayerView::PlayerBody,
                        0x30,
                        0x30,
                        0x10
                    },
                    DmaCacheMode::Uncached
                );
            }
        }
        catch (...)
        {
            resolvedMatrixPtr = 0;
        }
    }

    if (!Utils::valid_pointer(resolvedMatrixPtr))
    {
        player.playerBoneMatrixPtr = 0;
        player.bonePointersNeedResolve = true;

        return false;
    }

    const bool matrixChanged =
        player.playerBoneMatrixPtr != resolvedMatrixPtr;

    player.playerBoneMatrixPtr = resolvedMatrixPtr;

    if (matrixChanged)
    {
        std::fill(
            player.bonePtrs.begin(),
            player.bonePtrs.end(),
            0
        );

        std::fill(
            player.bonePositions.begin(),
            player.bonePositions.end(),
            glm::vec3(0.0f)
        );

        player.boneTransformCache.clear();
    }

    for (size_t i = 0; i < player.boneList.size(); ++i)
    {
        uint64_t resolvedBonePtr = 0;

        try
        {
            resolvedBonePtr = mem.ReadChain(
                player.playerBoneMatrixPtr,
                {
                    0x20 +
                    (static_cast<uint64_t>(player.boneList[i]) * 0x8),
                    0x10
                },
                DmaCacheMode::Uncached
            );
        }
        catch (...)
        {
            resolvedBonePtr = 0;
        }

        const uint64_t oldBonePtr = player.bonePtrs[i];

        if (Utils::valid_pointer(resolvedBonePtr))
        {
            player.bonePtrs[i] = resolvedBonePtr;
        }
        else if (forceResolve)
        {
            // A forced recovery
            player.bonePtrs[i] = 0;
        }

        if (oldBonePtr != player.bonePtrs[i])
        {
            player.bonePositions[i] = glm::vec3(0.0f);
        }
    }

    EnsureTransformCacheShape(player);

    player.invalidBones = false;

    player.bonePointersNeedResolve = !HasMinimalBonePointers(player);

    return std::any_of(
        player.bonePtrs.begin(),
        player.bonePtrs.end(),
        [](uint64_t bonePtr)
        {
            return Utils::valid_pointer(bonePtr);
        }
    );
}

void Players::readDogTagComponent(PlayerCache& player, bool force)
{
    if (!player.equipInited)
        return;

    if (player._slots.empty())
        return;

    if (!player.isPlayer)
        return;

    if (player.hasProfileData)
        return;

    for (auto& slot : player._slots)
    {
        std::string slotName = TrimEFT(slot.name);

        if (slotName != "Dogtag")
            continue;

        uint64_t dogtagItem = mem.Read<uint64_t>(slot.addr + sdk::Slot::ContainedItem);
        if (!Utils::valid_pointer(dogtagItem))
        {
            //std::cout << "[DogTag] Fail: invalid dogtag item ptr\n";
            break;
        }

        uint64_t dogtagComp = mem.Read<uint64_t>(dogtagItem + sdk::BarterOtherOffsets::Dogtag);
        if (!Utils::valid_pointer(dogtagComp))
        {
            //std::cout << "[DogTag] Fail: invalid dogtag component ptr\n";
            break;
        }

        //std::cout << "[DogTag] Read Data:\n";
        //std::cout << "  Nickname: " << player.DT_nickname << "\n";
        //std::cout << "  ProfileID: " << player.DT_profileId << "\n";
        //std::cout << "  AccountID: " << player.DT_accountId << "\n";
        //std::cout << "  Level: " << player.DT_lvl << "\n";
        //std::cout << "  Side: " << player.DT_Side << "\n";

        if (!player.DT_nickname.empty() ||
            player.DT_lvl > 0 ||
            player.DT_Side > 0 ||
            !player.DT_profileId.empty())
        {
            player.hasProfileData = true;

            std::cout << "[DogTag] SUCCESS: valid dogtag data\n";

            if (!player.DT_nickname.empty())
            {
                player.name = player.DT_nickname;
                std::cout << "[DogTag] Name updated from dogtag\n";
            }
        }
        else
        {
            std::cout << "[DogTag] Fail: all fields empty/invalid\n";
        }

        break;
    }
}



namespace
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Milliseconds = std::chrono::milliseconds;

    constexpr int kMaxTransformChain = 512;
    constexpr size_t kMaxBoneScatterBytes = 64 * 1024;
    constexpr size_t kMaxBoneHierarchiesPerScatter = 2;
    constexpr size_t kMaxBonePointerResolvesPerPass = 1;

    constexpr int kMinimalBoneSlots[] =
    {
        static_cast<int>(boneListIndexes::Base),
        static_cast<int>(boneListIndexes::LFoot),
        static_cast<int>(boneListIndexes::RFoot),
    };

    enum class BoneReadKind : uint8_t
    {
        Normal
    };

    struct LiveBoneRead
    {
        uint64_t playerInstance{};
        uint64_t boneTransform{};

        int boneSlot{ -1 };

        BoneTransformCacheEntry cache{};
        TransformAccessReadOnly access{};

        int count{};
        size_t bufOffset{};

        bool needsMetadata{};
        bool needsHierarchy{};
        bool hierarchyQueued{};
        bool metadataDirty{};

        bool matrixQueued{};
        bool indicesQueued{};

        bool hasPosition{};
        glm::vec3 position{};

        BoneReadKind kind{ BoneReadKind::Normal };
    };

    struct BonePlayerSnapshot
    {
        uint64_t instance{};
        float distance{};
        bool isLocal{};

        std::vector<uint64_t> bonePtrs;
        std::vector<BoneTransformCacheEntry> transformCache;
    };

    struct TransformHierarchyKey
    {
        uint64_t transformArray{};
        uint64_t transformIndices{};

        bool operator==(const TransformHierarchyKey&) const = default;
    };

    struct TransformHierarchyKeyHash
    {
        size_t operator()(const TransformHierarchyKey& key) const noexcept
        {
            const size_t first = std::hash<uint64_t>{}(key.transformArray);
            const size_t second = std::hash<uint64_t>{}(key.transformIndices);
            return first ^ (second + 0x9e3779b97f4a7c15ULL + (first << 6) + (first >> 2));
        }
    };

    struct TransformHierarchyRead
    {
        TransformHierarchyKey key{};
        size_t count{};
        size_t bufOffset{};
        bool matrixQueued{};
        bool indicesQueued{};
        std::vector<LiveBoneRead*> bones;
    };

    static bool IsMinimalBoneSlot(int slot)
    {
        return slot ==
            static_cast<int>(boneListIndexes::Base) ||
            slot ==
            static_cast<int>(boneListIndexes::LFoot) ||
            slot ==
            static_cast<int>(boneListIndexes::RFoot);
    }

    static bool IsUsableBonePosition(
        const glm::vec3& position)
    {
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            return false;
        }

        constexpr float epsilon = 0.001f;

        return std::fabs(position.x) >= epsilon ||
            std::fabs(position.y) >= epsilon ||
            std::fabs(position.z) >= epsilon;
    }

    static bool HasValidMinimalBonePose(const PlayerCache& player)
    {
        constexpr float kNearOriginLimit = 2.0f;
        constexpr float kMaxFootDistanceFromBase = 5.0f;
        constexpr float kMaxFootDistanceSq = kMaxFootDistanceFromBase * kMaxFootDistanceFromBase;

        const size_t baseIndex = static_cast<size_t>(boneListIndexes::Base);
        const size_t leftFootIndex = static_cast<size_t>(boneListIndexes::LFoot);
        const size_t rightFootIndex = static_cast<size_t>(boneListIndexes::RFoot);

        if (baseIndex >= player.bonePositions.size() ||
            leftFootIndex >= player.bonePositions.size() ||
            rightFootIndex >= player.bonePositions.size())
        {
            return false;
        }

        const glm::vec3& base = player.bonePositions[baseIndex];
        const glm::vec3& leftFoot = player.bonePositions[leftFootIndex];
        const glm::vec3& rightFoot = player.bonePositions[rightFootIndex];

        const auto isFinite = [](const glm::vec3& value)
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        };

        const auto isNearOrigin = [kNearOriginLimit](const glm::vec3& value)
        {
            return std::fabs(value.x) < kNearOriginLimit &&
                std::fabs(value.y) < kNearOriginLimit &&
                std::fabs(value.z) < kNearOriginLimit;
        };

        if (!isFinite(base) ||
            !isFinite(leftFoot) ||
            !isFinite(rightFoot) ||
            isNearOrigin(base) ||
            isNearOrigin(leftFoot) ||
            isNearOrigin(rightFoot))
        {
            return false;
        }

        const auto distanceSquared = [](const glm::vec3& first,
            const glm::vec3& second)
        {
            const glm::vec3 delta = first - second;
            return (delta.x * delta.x) +
                (delta.y * delta.y) +
                (delta.z * delta.z);
        };

        return distanceSquared(base, leftFoot) <= kMaxFootDistanceSq &&
            distanceSquared(base, rightFoot) <= kMaxFootDistanceSq &&
            distanceSquared(leftFoot, rightFoot) <= kMaxFootDistanceSq;
    }

    static void EnsureTransformCacheShape(
        PlayerCache& player)
    {
        const size_t boneCount = player.bonePtrs.size();

        if (player.boneTransformCache.size() != boneCount)
            player.boneTransformCache.resize(boneCount);

        for (size_t i = 0; i < boneCount; ++i)
        {
            BoneTransformCacheEntry& entry =
                player.boneTransformCache[i];

            const uint64_t currentBonePtr =
                player.bonePtrs[i];

            if (entry.boneTransform != currentBonePtr)
            {
                entry = {};
                entry.boneTransform = currentBonePtr;
            }
        }
    }

    static bool HasMinimalBonePointers(
        const PlayerCache& player)
    {
        for (const int slot : kMinimalBoneSlots)
        {
            if (slot < 0)
                return false;

            const size_t index =
                static_cast<size_t>(slot);

            if (index >= player.bonePtrs.size())
                return false;

            if (!Utils::valid_pointer(player.bonePtrs[index]))
                return false;
        }

        return true;
    }

    static PlayerCache* FindPlayerByInstance(
        std::vector<PlayerCache>& cache,
        uint64_t instance)
    {
        const auto it = std::find_if(
            cache.begin(),
            cache.end(),
            [&](const PlayerCache& player)
            {
                return player.instance == instance;
            }
        );

        return it == cache.end()
            ? nullptr
            : &(*it);
    }

    static bool IsLocalGroupRosterProtectionActive()
    {
        constexpr int kMaxRegisteredPlayersSafe = 512;

        if (!Utils::valid_pointer(mainGame.localPlayerPtr) ||
            mainGame.localGroupId.empty())
        {
            return false;
        }

        const int registeredCount = mainGame.registeredPlayersCount;

        if (registeredCount <= 0 ||
            registeredCount > kMaxRegisteredPlayersSafe)
        {
            return false;
        }

        bool hasValidRegisteredPlayer = false;

        for (int i = 0; i < registeredCount; ++i)
        {
            const uint64_t playerInstance = mainGame.player_buffer[i];

            if (!Utils::valid_pointer(playerInstance))
                continue;

            hasValidRegisteredPlayer = true;

            if (playerInstance == mainGame.localPlayerPtr)
                return false;
        }

        return hasValidRegisteredPlayer;
    }

    static bool IsProtectedLocalGroupMember(const PlayerCache& player, const bool localGroupRosterProtectionActive)
    {
        return localGroupRosterProtectionActive &&
            !player.isLocal &&
            player.instance != mainGame.localPlayerPtr &&
            player.groupId == mainGame.localGroupId;
    }

    static bool HasUsableTransformMetadata(const LiveBoneRead& read)
    {
        return
            read.cache.valid &&
            read.cache.boneTransform == read.boneTransform &&
            Utils::valid_pointer(read.cache.transformArray) &&
            Utils::valid_pointer(read.cache.transformIndices) &&
            read.cache.transformIndex >= 0 &&
            read.cache.transformIndex < kMaxTransformChain;
    }

    bool NeedsTransformMetadataRefresh(const LiveBoneRead& read)
    {
        return !HasUsableTransformMetadata(read);
    }

    static glm::vec3 computeTransformPosition(
        const Matrix34* matrices,
        const int32_t* indices,
        int32_t index,
        int count)
    {
        if (!matrices ||
            !indices ||
            index < 0 ||
            count <= 0 ||
            index >= count)
        {
            return glm::vec3(0.0f);
        }

        __m128 result = *(__m128*)(
            (uint8_t*)matrices +
            sizeof(Matrix34) *
            static_cast<size_t>(index)
            );

        const __m128 mulVec0 =
        {
            -2.0f, 2.0f, -2.0f, 0.0f
        };

        const __m128 mulVec1 =
        {
            2.0f, -2.0f, -2.0f, 0.0f
        };

        const __m128 mulVec2 =
        {
            -2.0f, -2.0f, 2.0f, 0.0f
        };

        int transformIndex = indices[index];
        int safety = 0;

        while (transformIndex >= 0 &&
            safety++ < kMaxTransformChain)
        {
            if (transformIndex >= count)
                break;

            const Matrix34& matrix34 =
                matrices[transformIndex];

            __m128 xxxx = _mm_castsi128_ps(
                _mm_shuffle_epi32(
                    *(__m128i*)(&matrix34.vec1),
                    0x00
                )
            );

            __m128 yyyy = _mm_castsi128_ps(
                _mm_shuffle_epi32(
                    *(__m128i*)(&matrix34.vec1),
                    0x55
                )
            );

            __m128 zwxy = _mm_castsi128_ps(
                _mm_shuffle_epi32(
                    *(__m128i*)(&matrix34.vec1),
                    0x8E
                )
            );

            __m128 wzyw = _mm_castsi128_ps(
                _mm_shuffle_epi32(
                    *(__m128i*)(&matrix34.vec1),
                    0xDB
                )
            );

            __m128 zzzz = _mm_castsi128_ps(
                _mm_shuffle_epi32(
                    *(__m128i*)(&matrix34.vec1),
                    0xAA
                )
            );

            __m128 yxwy = _mm_castsi128_ps(
                _mm_shuffle_epi32(
                    *(__m128i*)(&matrix34.vec1),
                    0x71
                )
            );

            __m128 tmp7 = _mm_mul_ps(
                *(__m128*)(&matrix34.vec2),
                result
            );

            result = _mm_add_ps(
                _mm_add_ps(
                    _mm_add_ps(
                        _mm_mul_ps(
                            _mm_sub_ps(
                                _mm_mul_ps(
                                    _mm_mul_ps(xxxx, mulVec1),
                                    zwxy
                                ),
                                _mm_mul_ps(
                                    _mm_mul_ps(yyyy, mulVec2),
                                    wzyw
                                )
                            ),
                            _mm_castsi128_ps(
                                _mm_shuffle_epi32(
                                    _mm_castps_si128(tmp7),
                                    0xAA
                                )
                            )
                        ),
                        _mm_mul_ps(
                            _mm_sub_ps(
                                _mm_mul_ps(
                                    _mm_mul_ps(zzzz, mulVec2),
                                    wzyw
                                ),
                                _mm_mul_ps(
                                    _mm_mul_ps(xxxx, mulVec0),
                                    yxwy
                                )
                            ),
                            _mm_castsi128_ps(
                                _mm_shuffle_epi32(
                                    _mm_castps_si128(tmp7),
                                    0x55
                                )
                            )
                        )
                    ),
                    _mm_add_ps(
                        _mm_mul_ps(
                            _mm_sub_ps(
                                _mm_mul_ps(
                                    _mm_mul_ps(yyyy, mulVec0),
                                    yxwy
                                ),
                                _mm_mul_ps(
                                    _mm_mul_ps(zzzz, mulVec1),
                                    zwxy
                                )
                            ),
                            _mm_castsi128_ps(
                                _mm_shuffle_epi32(
                                    _mm_castps_si128(tmp7),
                                    0x00
                                )
                            )
                        ),
                        tmp7
                    )
                ),
                *(__m128*)(&matrix34.vec0)
            );

            const int previousTransformIndex =
                transformIndex;

            transformIndex =
                indices[transformIndex];

            if (previousTransformIndex == transformIndex &&
                transformIndex == 0)
            {
                break;
            }
        }

        return glm::vec3(
            result.m128_f32[0],
            result.m128_f32[1],
            result.m128_f32[2]
        );
    }

    static void BatchReadBoneWorldPositions(Memory& memory, std::vector<LiveBoneRead>& reads)
    {
        if (reads.empty())
            return;

        for (LiveBoneRead& read : reads)
        {
            read.needsMetadata = false;
            read.needsHierarchy = false;
            read.hierarchyQueued = false;
            read.metadataDirty = false;
            read.matrixQueued = false;
            read.indicesQueued = false;
            read.hasPosition = false;
            read.count = 0;
        }

        // ---------------------------------------------------------------------
        // Stage 1: refresh TransformAccessReadOnly only when cached metadata
        // is missing or invalid.
        // ---------------------------------------------------------------------
        bool queuedMetadataReads = false;

        {
            ScatterReadBatch scatter(
                memory,
                DmaCacheMode::Uncached,
                "Player bone metadata"
            );

            if (!scatter.Valid())
                return;

            for (LiveBoneRead& read : reads)
            {
                if (!NeedsTransformMetadataRefresh(read))
                    continue;

                read.needsMetadata = true;
                read.metadataDirty = true;
                read.cache = {};
                read.cache.boneTransform = read.boneTransform;
                read.access = {};

                if (!scatter.Add(
                    read.boneTransform +
                    UnityOffsets::TransformInternal_TransformAccessOffset,
                    read.access))
                {
                    read.cache.valid = false;
                    continue;
                }

                queuedMetadataReads = true;
            }

            if (queuedMetadataReads)
            {
                const bool executed = scatter.Execute();

                if (!executed)
                {
                    for (LiveBoneRead& read : reads)
                    {
                        if (read.needsMetadata)
                            read.cache.valid = false;
                    }
                }
            }
        }

        for (LiveBoneRead& read : reads)
        {
            if (!read.needsMetadata)
                continue;

            if (!Utils::valid_pointer(read.access.pTransformData) ||
                read.access.index < 0 ||
                read.access.index >= kMaxTransformChain)
            {
                read.cache.valid = false;
                continue;
            }

            read.cache.transformData =
                read.access.pTransformData;

            read.cache.transformIndex =
                read.access.index;

            read.needsHierarchy = true;
        }

        // ---------------------------------------------------------------------
        // Stage 2: read hierarchy array and index pointers for metadata that
        // was refreshed above.
        // ---------------------------------------------------------------------
        bool queuedHierarchyReads = false;

        {
            ScatterReadBatch scatter(
                memory,
                DmaCacheMode::Uncached,
                "Player bone hierarchy"
            );

            if (!scatter.Valid())
                return;

            for (LiveBoneRead& read : reads)
            {
                if (!read.needsHierarchy)
                    continue;

                const bool matrixQueued =
                    scatter.Add(
                        read.cache.transformData +
                        UnityOffsets::Hierarchy_VerticesOffset,
                        read.cache.transformArray
                    );

                const bool indicesQueued =
                    scatter.Add(
                        read.cache.transformData +
                        UnityOffsets::Hierarchy_IndicesOffset,
                        read.cache.transformIndices
                    );

                if (!matrixQueued || !indicesQueued)
                {
                    read.cache.valid = false;
                    continue;
                }

                read.hierarchyQueued = true;
                queuedHierarchyReads = true;
            }

            if (queuedHierarchyReads)
            {
                const bool executed = scatter.Execute();

                if (!executed)
                {
                    for (LiveBoneRead& read : reads)
                    {
                        if (read.hierarchyQueued)
                            read.cache.valid = false;
                    }
                }
            }
        }

        for (LiveBoneRead& read : reads)
        {
            if (!read.needsHierarchy ||
                !read.hierarchyQueued)
            {
                continue;
            }

            if (!Utils::valid_pointer(read.cache.transformArray) ||
                !Utils::valid_pointer(read.cache.transformIndices))
            {
                read.cache.valid = false;
                continue;
            }

            read.cache.valid = true;
        }

        // ---------------------------------------------------------------------
        // Stage 3: build the list of transform-chain position reads.
        // ---------------------------------------------------------------------
        std::vector<TransformHierarchyRead> hierarchyReads;
        hierarchyReads.reserve(reads.size());

        std::unordered_map<
            TransformHierarchyKey,
            size_t,
            TransformHierarchyKeyHash> hierarchyLookup;
        hierarchyLookup.reserve(reads.size());

        for (LiveBoneRead& read : reads)
        {
            if (!HasUsableTransformMetadata(read))
                continue;

            const size_t matrixCount =
                static_cast<size_t>(
                    read.cache.transformIndex + 1
                    );

            if (matrixCount == 0 ||
                matrixCount > kMaxTransformChain)
            {
                read.cache.valid = false;
                continue;
            }

            read.count = static_cast<int>(matrixCount);

            const TransformHierarchyKey key
            {
                read.cache.transformArray,
                read.cache.transformIndices
            };

            const auto [it, inserted] = hierarchyLookup.try_emplace(
                key,
                hierarchyReads.size());

            if (inserted)
            {
                TransformHierarchyRead hierarchy{};
                hierarchy.key = key;
                hierarchyReads.emplace_back(std::move(hierarchy));
            }

            TransformHierarchyRead& hierarchy = hierarchyReads[it->second];
            hierarchy.count = (std::max)(hierarchy.count, matrixCount);
            hierarchy.bones.emplace_back(&read);
        }

        if (hierarchyReads.empty())
            return;

        thread_local std::vector<Matrix34> matrices;
        thread_local std::vector<int32_t> indices;

        size_t start = 0;

        while (start < hierarchyReads.size())
        {
            size_t end = start;
            size_t totalMatrixElements = 0;
            size_t totalBytes = 0;

            while (end < hierarchyReads.size())
            {
                const size_t matrixCount = hierarchyReads[end].count;
                const size_t hierarchyBytes = matrixCount *
                    (sizeof(Matrix34) + sizeof(int32_t));

                if (hierarchyBytes > kMaxBoneScatterBytes ||
                    end - start >= kMaxBoneHierarchiesPerScatter ||
                    totalBytes + hierarchyBytes > kMaxBoneScatterBytes)
                {
                    break;
                }

                totalMatrixElements += matrixCount;
                totalBytes += hierarchyBytes;
                ++end;
            }

            if (end == start)
            {
                for (LiveBoneRead* read : hierarchyReads[start].bones)
                    read->cache.valid = false;

                ++start;
                continue;
            }

            matrices.resize(totalMatrixElements);
            indices.resize(totalMatrixElements);

            ScatterReadBatch scatter(
                memory,
                DmaCacheMode::Uncached,
                "Player bone positions"
            );

            if (!scatter.Valid())
            {
                for (size_t i = start; i < end; ++i)
                {
                    for (LiveBoneRead* read : hierarchyReads[i].bones)
                        read->cache.valid = false;
                }

                return;
            }

            bool queuedPositionReads = false;
            size_t cursor = 0;

            for (size_t i = start; i < end; ++i)
            {
                TransformHierarchyRead& hierarchy = hierarchyReads[i];
                const size_t matrixCount = hierarchy.count;

                hierarchy.bufOffset = cursor;
                cursor += matrixCount;

                hierarchy.matrixQueued =
                    scatter.AddBytes(
                        hierarchy.key.transformArray,
                        matrices.data() + hierarchy.bufOffset,
                        static_cast<SIZE_T>(
                            sizeof(Matrix34) * matrixCount
                            )
                    );

                hierarchy.indicesQueued =
                    scatter.AddBytes(
                        hierarchy.key.transformIndices,
                        indices.data() + hierarchy.bufOffset,
                        static_cast<SIZE_T>(
                            sizeof(int32_t) * matrixCount
                            )
                    );

                if (!hierarchy.matrixQueued ||
                    !hierarchy.indicesQueued)
                {
                    for (LiveBoneRead* read : hierarchy.bones)
                        read->cache.valid = false;

                    continue;
                }

                queuedPositionReads = true;
            }

            if (!queuedPositionReads)
            {
                start = end;
                continue;
            }

            const bool executed = scatter.Execute();

            if (!executed)
            {
                for (size_t i = start; i < end; ++i)
                {
                    for (LiveBoneRead* read : hierarchyReads[i].bones)
                        read->cache.valid = false;
                }

                start = end;
                continue;
            }

            for (size_t i = start; i < end; ++i)
            {
                TransformHierarchyRead& hierarchy = hierarchyReads[i];

                if (!hierarchy.matrixQueued ||
                    !hierarchy.indicesQueued ||
                    hierarchy.count == 0)
                {
                    continue;
                }

                for (LiveBoneRead* read : hierarchy.bones)
                {
                    const glm::vec3 position =
                        computeTransformPosition(
                            matrices.data() + hierarchy.bufOffset,
                            indices.data() + hierarchy.bufOffset,
                            read->cache.transformIndex,
                            static_cast<int>(hierarchy.count)
                        );

                    if (!IsUsableBonePosition(position))
                    {
                        read->cache.valid = false;
                        continue;
                    }

                    read->position = position;
                    read->hasPosition = true;
                }
            }

            start = end;
        }
    }

    static void ApplyBoneResults(
        std::vector<LiveBoneRead>& reads)
    {
        if (reads.empty())
            return;

        struct AppliedPlayerState
        {
            uint64_t instance = 0;
            uint8_t minimalBonesQueued = 0;
            uint8_t minimalBonesRead = 0;
        };

        constexpr uint8_t kBaseBoneBit = 1u << 0;
        constexpr uint8_t kLeftFootBoneBit = 1u << 1;
        constexpr uint8_t kRightFootBoneBit = 1u << 2;
        constexpr uint8_t kCompleteMinimalBoneMask =
            kBaseBoneBit | kLeftFootBoneBit | kRightFootBoneBit;

        std::vector<AppliedPlayerState> states;
        states.reserve(reads.size());

        auto GetState = [&](uint64_t instance) -> AppliedPlayerState&
            {
                const auto found = std::find_if(
                    states.begin(),
                    states.end(),
                    [&](const AppliedPlayerState& state)
                    {
                        return state.instance == instance;
                    }
                );

                if (found != states.end())
                    return *found;

                states.push_back({ instance });
                return states.back();
            };

        std::lock_guard<std::mutex> lock(playerMutex);

        std::vector<PlayerCache>& cache =
            players.getCache();

        for (const LiveBoneRead& read : reads)
        {
            PlayerCache* player =
                FindPlayerByInstance(
                    cache,
                    read.playerInstance
                );

            if (!player)
                continue;

            if (!Utils::valid_pointer(player->instance) ||
                player->isBTR ||
                player->isDead ||
                player->hasExfiled)
            {
                continue;
            }

            if (read.boneSlot < 0)
                continue;

            const size_t boneIndex =
                static_cast<size_t>(read.boneSlot);

            if (boneIndex >= player->bonePtrs.size() ||
                boneIndex >= player->bonePositions.size())
            {
                continue;
            }

            if (player->bonePtrs[boneIndex] !=
                read.boneTransform)
            {
                continue;
            }

            EnsureTransformCacheShape(*player);

            if (boneIndex < player->boneTransformCache.size())
            {
                player->boneTransformCache[boneIndex] =
                    read.cache;
            }

            AppliedPlayerState& state =
                GetState(player->instance);

            if (IsMinimalBoneSlot(read.boneSlot))
            {
                uint8_t boneBit = 0;

                if (read.boneSlot == static_cast<int>(boneListIndexes::Base))
                    boneBit = kBaseBoneBit;
                else if (read.boneSlot == static_cast<int>(boneListIndexes::LFoot))
                    boneBit = kLeftFootBoneBit;
                else if (read.boneSlot == static_cast<int>(boneListIndexes::RFoot))
                    boneBit = kRightFootBoneBit;

                state.minimalBonesQueued |= boneBit;
                if (read.hasPosition)
                    state.minimalBonesRead |= boneBit;
            }

            if (read.hasPosition)
            {
                player->bonePositions[boneIndex] =
                    read.position;
            }
        }

        for (const AppliedPlayerState& state : states)
        {
            PlayerCache* player =
                FindPlayerByInstance(
                    cache,
                    state.instance
                );

            if (!player)
                continue;

            const bool minimalPointersValid =
                HasMinimalBonePointers(*player);

            const bool queuedCompleteMinimalPose =
                state.minimalBonesQueued == kCompleteMinimalBoneMask;

            const bool readCompleteMinimalPose =
                state.minimalBonesRead == kCompleteMinimalBoneMask;

            // re-resolve bone pointers
            // pointers disappeared or any required position read failed
            if (!minimalPointersValid ||
                !queuedCompleteMinimalPose ||
                !readCompleteMinimalPose)
            {
                player->bonePointersNeedResolve = true;
                continue;
            }

            if (!HasValidMinimalBonePose(*player))
            {
                player->bonePointersNeedResolve = true;
                std::fill(
                    player->bonePtrs.begin(),
                    player->bonePtrs.end(),
                    0ULL
                );
                std::fill(
                    player->bonePositions.begin(),
                    player->bonePositions.end(),
                    glm::vec3(0.0f)
                );
                player->boneTransformCache.clear();
                continue;
            }

            player->bonePointersNeedResolve = false;
            player->invalidBones = false;

            player->location = GetBestPlayerBasePosition(*player);

            if (player->isLocal)
            {
                player->distance = 0;
                mainGame.localLocation = player->location;
            }
            else
            {
                const float dx =
                    player->location.x -
                    mainGame.localLocation.x;

                const float dy =
                    player->location.y -
                    mainGame.localLocation.y;

                const float dz =
                    player->location.z -
                    mainGame.localLocation.z;

                player->distance = static_cast<int>(
                    std::sqrt(
                        (dx * dx) +
                        (dy * dy) +
                        (dz * dz)
                    )
                    );
            }
        }
    }

    enum class AppendResult
    {
        Queued,
        NoBones
    };

    static AppendResult AppendPlayerBoneReads(const BonePlayerSnapshot& player, BoneReadKind kind, bool readFullBoneList, std::vector<LiveBoneRead>& reads)
    {
        bool queuedAny = false;

        auto QueueBone = [&](int slot)
            {
                if (slot < 0)
                    return;

                const size_t boneIndex = static_cast<size_t>(slot);

                if (boneIndex >= player.bonePtrs.size())
                    return;

                const uint64_t bonePtr = player.bonePtrs[boneIndex];

                // One bad optional bone must never stop Base/LFoot/RFoot,
                // or other valid bones, from being read.
                if (!Utils::valid_pointer(bonePtr))
                    return;

                LiveBoneRead read{};

                read.playerInstance = player.instance;
                read.boneTransform = bonePtr;
                read.boneSlot = slot;
                read.kind = kind;

                if (boneIndex < player.transformCache.size())
                {
                    read.cache = player.transformCache[boneIndex];
                }

                reads.emplace_back(std::move(read));
                queuedAny = true;
            };

        if (readFullBoneList)
        {
            // The full list already includes Base, LFoot and RFoot.
            // Do not queue a separate minimal scan as well.
            for (size_t i = 0; i < player.bonePtrs.size(); ++i)
            {
                QueueBone(static_cast<int>(i));
            }
        }
        else
        {
            for (const int slot : kMinimalBoneSlots)
            {
                QueueBone(slot);
            }
        }

        return queuedAny
            ? AppendResult::Queued
            : AppendResult::NoBones;
    }

    static AppendResult AppendFastPlayerBoneReads(
        const BonePlayerSnapshot& player,
        std::vector<LiveBoneRead>& reads)
    {
        const size_t firstRead = reads.size();

        auto QueueBone = [&](int slot)
            {
                if (slot < 0)
                    return;

                for (size_t i = firstRead; i < reads.size(); ++i)
                {
                    if (reads[i].boneSlot == slot)
                        return;
                }

                const size_t boneIndex = static_cast<size_t>(slot);
                if (boneIndex >= player.bonePtrs.size())
                    return;

                const uint64_t bonePtr = player.bonePtrs[boneIndex];
                if (!Utils::valid_pointer(bonePtr))
                    return;

                LiveBoneRead read{};
                read.playerInstance = player.instance;
                read.boneTransform = bonePtr;
                read.boneSlot = slot;
                read.kind = BoneReadKind::Normal;

                if (boneIndex < player.transformCache.size())
                    read.cache = player.transformCache[boneIndex];

                reads.emplace_back(std::move(read));
            };

        // Base drives the player marker and is the only movement bone needed
        // for the local player.
        QueueBone(static_cast<int>(boneListIndexes::Base));
        QueueBone(static_cast<int>(boneListIndexes::LFoot));
        QueueBone(static_cast<int>(boneListIndexes::RFoot));

        if (!player.isLocal)
        {
            if (espGlobals::drawBoxPlayers ||
                espGlobals::drawHeadDot ||
                espGlobals::drawSkeletons)
            {
                QueueBone(static_cast<int>(boneListIndexes::Head));
            }

            if (aimGlobals::aimEnabled)
            {
                QueueBone(static_cast<int>(aimGlobals::aiBone));
                QueueBone(static_cast<int>(aimGlobals::pmcBone));
            }
        }

        return reads.size() > firstRead
            ? AppendResult::Queued
            : AppendResult::NoBones;
    }
}


void Players::boneTask()
{
    bool motionUpdated = false;

    try
    {
        if (!mem.IsDmaOperational())
            return;

        if (!Utils::valid_pointer(mainGame.localPlayerPtr))
            return;

        const auto now = Clock::now();
        const auto fullBoneInterval =
            std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double, std::milli>(
                    (std::max)(1.0, globals::taskPlayersBones)));

        const bool runFullBonePass =
            nextFullBoneUpdate == Clock::time_point{} ||
            now >= nextFullBoneUpdate;

        if (runFullBonePass)
            nextFullBoneUpdate = now + fullBoneInterval;

        constexpr float kNearOriginLimit = 2.0f;
        constexpr float kMaxFootDistanceFromBase = 5.0f;
        constexpr float kMaxBoneDistanceFromBase = 5.0f;
        constexpr float kMaxBoneSeparation = 5.0f;

        constexpr float kMaxFootDistanceSq = kMaxFootDistanceFromBase * kMaxFootDistanceFromBase;

        constexpr float kMaxBoneDistanceSq = kMaxBoneDistanceFromBase * kMaxBoneDistanceFromBase;

        constexpr float kMaxBoneSeparationSq = kMaxBoneSeparation * kMaxBoneSeparation;

        constexpr float kFullBoneUpdateDistanceMargin = 1.0f;

        const float drawPlayerDistance = static_cast<float>(espGlobals::drawPlayerDist);
        const CameraProjectionSnapshot projection = camera.getProjectionSnapshot();

        const auto IsFiniteVector = [](const glm::vec3& value) -> bool
            {
                return std::isfinite(value.x) &&
                    std::isfinite(value.y) &&
                    std::isfinite(value.z);
            };

        const auto IsNearWorldOrigin =
            [&](const glm::vec3& value) -> bool
            {
                return std::fabs(value.x) < kNearOriginLimit &&
                    std::fabs(value.y) < kNearOriginLimit &&
                    std::fabs(value.z) < kNearOriginLimit;
            };

        const auto DistanceSquared =
            [](const glm::vec3& a, const glm::vec3& b) -> float
            {
                const glm::vec3 delta = a - b;

                return
                    (delta.x * delta.x) +
                    (delta.y * delta.y) +
                    (delta.z * delta.z);
            };

        const auto IsStrictlyOnScreen =
            [&](const PlayerCache& player) -> bool
            {
                if (!projection || !projection->valid)
                    return false;

                glm::vec2 screenPosition{};

                if (!Utils::Camera::world_to_screen(GetBestPlayerBasePosition(player), &screenPosition, *projection))
                {
                    return false;
                }

                return
                    screenPosition.x >= 0.0f &&
                    screenPosition.y >= 0.0f &&
                    screenPosition.x <= espGlobals::gameRes.x &&
                    screenPosition.y <= espGlobals::gameRes.y;
            };

        struct PendingBoneScan
        {
            BonePlayerSnapshot snapshot{};
            bool readFullBoneList = false;
        };

        struct PendingBoneResolve
        {
            uint64_t instance{};
            PlayerCache workingCopy{};
        };

        std::vector<PendingBoneResolve> pendingResolves;

        
        if (runFullBonePass)
        {
            std::lock_guard<std::mutex> lock(playerMutex);
            std::vector<PlayerCache>& cache = players.getCache();

            if (!cache.empty())
            {
                const size_t start = boneResolveCursor % cache.size();
                size_t inspected = 0;

                while (inspected < cache.size() &&
                    pendingResolves.size() < kMaxBonePointerResolvesPerPass)
                {
                    const size_t index = (start + inspected) % cache.size();
                    PlayerCache& player = cache[index];
                    ++inspected;

                    if (!Utils::valid_pointer(player.instance) ||
                        player.isBTR ||
                        player.isDead ||
                        player.hasExfiled)
                    {
                        continue;
                    }

                    if (!player.bonePointersNeedResolve && HasMinimalBonePointers(player))
                    {
                        continue;
                    }

                    PendingBoneResolve resolve{};
                    resolve.instance = player.instance;
                    resolve.workingCopy = player;
                    pendingResolves.emplace_back(std::move(resolve));
                }

                boneResolveCursor = (start + (std::max)(size_t{ 1 }, inspected)) % cache.size();
            }
        }

        for (PendingBoneResolve& resolve : pendingResolves)
            getBonePtrs(resolve.workingCopy, true);

        if (!pendingResolves.empty())
        {
            std::lock_guard<std::mutex> lock(playerMutex);
            std::vector<PlayerCache>& cache = players.getCache();

            for (PendingBoneResolve& resolve : pendingResolves)
            {
                PlayerCache* player = FindPlayerByInstance(cache, resolve.instance);

                if (!player ||
                    player->isBTR ||
                    player->isDead ||
                    player->hasExfiled)
                {
                    continue;
                }

                player->playerBoneMatrixPtr = resolve.workingCopy.playerBoneMatrixPtr;
                player->bonePtrs = std::move(resolve.workingCopy.bonePtrs);
                player->bonePositions = std::move(resolve.workingCopy.bonePositions);
                player->boneTransformCache = std::move(resolve.workingCopy.boneTransformCache);
                player->invalidBones = resolve.workingCopy.invalidBones;
                player->bonePointersNeedResolve = resolve.workingCopy.bonePointersNeedResolve;
            }
        }

        std::vector<PendingBoneScan> pendingScans;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache = players.getCache();

            pendingScans.reserve(cache.size());

            for (PlayerCache& player : cache)
            {
                if (!Utils::valid_pointer(player.instance))
                    continue;

                if (player.isBTR ||
                    player.isDead ||
                    player.hasExfiled)
                {
                    continue;
                }

                if (player.bonePtrs.empty())
                    continue;

                const bool hasAnyBonePointer =
                    std::any_of(
                        player.bonePtrs.begin(),
                        player.bonePtrs.end(),
                        [](uint64_t bonePtr)
                        {
                            return Utils::valid_pointer(bonePtr);
                        }
                    );

                if (!hasAnyBonePointer)
                    continue;

                EnsureTransformCacheShape(player);

                PendingBoneScan pending{};

                pending.snapshot.instance = player.instance;
                pending.snapshot.distance = player.distance;
                pending.snapshot.isLocal = player.isLocal;
                pending.snapshot.bonePtrs = player.bonePtrs;
                pending.snapshot.transformCache = player.boneTransformCache;

                // Every player gets Base/LFoot/RFoot. Expand to the full
                // skeleton only while it can actually be rendered on screen.
                pending.readFullBoneList =
                    runFullBonePass &&
                    espGlobals::drawSkeletons &&
                    !player.isLocal &&
                    player.distance > 0.0f &&
                    player.distance <= drawPlayerDistance + kFullBoneUpdateDistanceMargin &&
                    IsStrictlyOnScreen(player);

                pendingScans.emplace_back(std::move(pending));
            }
        }

        if (pendingScans.empty())
            return;

        std::vector<LiveBoneRead> reads;

        std::unordered_set<uint64_t> scannedInstances;
        std::unordered_set<uint64_t> fullBoneScanInstances;

        scannedInstances.reserve(pendingScans.size());
        fullBoneScanInstances.reserve(pendingScans.size());

        for (const PendingBoneScan& pending : pendingScans)
        {
            const size_t readsBefore = reads.size();

            if (pending.readFullBoneList)
            {
                AppendPlayerBoneReads(
                    pending.snapshot,
                    BoneReadKind::Normal,
                    true,
                    reads);
            }
            else
            {
                AppendFastPlayerBoneReads(
                    pending.snapshot,
                    reads);
            }

            // Only validate players for which this task actually submitted reads.
            if (reads.size() == readsBefore)
                continue;

            scannedInstances.insert(pending.snapshot.instance);

            if (pending.readFullBoneList)
                fullBoneScanInstances.insert(pending.snapshot.instance);
        }

        if (reads.empty())
            return;

        BatchReadBoneWorldPositions(mem, reads);

        std::unordered_set<uint64_t> incompleteFullBoneScanInstances;
        incompleteFullBoneScanInstances.reserve(fullBoneScanInstances.size());

        for (const LiveBoneRead& read : reads)
        {
            if (!read.hasPosition &&
                fullBoneScanInstances.contains(read.playerInstance))
            {
                incompleteFullBoneScanInstances.insert(read.playerInstance);
            }
        }

        motionUpdated = std::any_of(
            reads.begin(),
            reads.end(),
            [](const LiveBoneRead& read)
            {
                return read.hasPosition;
            });

        ApplyBoneResults(reads);

        std::string boneRefreshWarning;

        // Validate the newly applied skeleton data
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache = players.getCache();

            for (PlayerCache& player : cache)
            {
                if (!Utils::valid_pointer(player.instance))
                    continue;

                if (player.isBTR ||
                    player.isDead ||
                    player.hasExfiled)
                {
                    continue;
                }

                if (scannedInstances.find(player.instance) ==
                    scannedInstances.end())
                {
                    continue;
                }

                const size_t baseIndex = static_cast<size_t>(boneListIndexes::Base);
                const size_t leftFootIndex = static_cast<size_t>(boneListIndexes::LFoot);
                const size_t rightFootIndex = static_cast<size_t>(boneListIndexes::RFoot);

                bool needsPointerRefresh = false;
                std::string invalidReason;

                if (baseIndex >= player.bonePositions.size() ||
                    leftFootIndex >= player.bonePositions.size() ||
                    rightFootIndex >= player.bonePositions.size())
                {
                    needsPointerRefresh = true;
                    invalidReason = "required bone index outside bonePositions";
                }
                else
                {
                    const glm::vec3& base = player.bonePositions[baseIndex];
                    const glm::vec3& leftFoot = player.bonePositions[leftFootIndex];
                    const glm::vec3& rightFoot = player.bonePositions[rightFootIndex];

                    // Reject NaN, infinity, and 0,0,0-style results.
                    if (!IsFiniteVector(base) || !IsFiniteVector(leftFoot) || !IsFiniteVector(rightFoot))
                    {
                        needsPointerRefresh = true;
                        invalidReason = "non-finite Base/LFoot/RFoot position";
                    }
                    else if (IsNearWorldOrigin(base) || IsNearWorldOrigin(leftFoot) || IsNearWorldOrigin(rightFoot))
                    {
                        needsPointerRefresh = true;
                        invalidReason = "Base/LFoot/RFoot near world origin";
                    }
                    else if (DistanceSquared(base, leftFoot) > kMaxFootDistanceSq)
                    {
                        needsPointerRefresh = true;
                        invalidReason = "LFoot too far from Base";
                    }
                    else if (DistanceSquared(base, rightFoot) >
                        kMaxFootDistanceSq)
                    {
                        needsPointerRefresh = true;
                        invalidReason = "RFoot too far from Base";
                    }
                    else if (DistanceSquared(leftFoot, rightFoot) >
                        kMaxFootDistanceSq)
                    {
                        needsPointerRefresh = true;
                        invalidReason = "LFoot too far from RFoot";
                    }

                    const bool receivedFullBoneScan =
                        fullBoneScanInstances.contains(player.instance);

                    if (!needsPointerRefresh &&
                        receivedFullBoneScan &&
                        incompleteFullBoneScanInstances.contains(player.instance))
                    {
                        needsPointerRefresh = true;
                        invalidReason = "full skeleton read incomplete";
                    }

                    // A completed full scan must produce a compact skeleton:
                    // every pair of bones must remain within five metres.
                    if (!needsPointerRefresh && receivedFullBoneScan)
                    {
                        const size_t boneCount = (std::min)(player.bonePtrs.size(), player.bonePositions.size());

                        for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
                        {
                            if (!Utils::valid_pointer(player.bonePtrs[boneIndex]))
                            {
                                needsPointerRefresh = true;
                                invalidReason = "full skeleton bone pointer missing";
                                break;
                            }

                            if (boneIndex >= player.bonePositions.size())
                                continue;

                            if (boneIndex == baseIndex ||
                                boneIndex == leftFootIndex ||
                                boneIndex == rightFootIndex)
                            {
                                continue;
                            }

                            const glm::vec3& bonePosition =
                                player.bonePositions[boneIndex];

                            if (!IsFiniteVector(bonePosition))
                            {
                                needsPointerRefresh = true;
                                invalidReason = "non-finite full bone position";
                                break;
                            }

                            if (IsNearWorldOrigin(bonePosition))
                            {
                                needsPointerRefresh = true;
                                invalidReason = "full bone near world origin";
                                break;
                            }

                            if (DistanceSquared(base, bonePosition) >
                                kMaxBoneDistanceSq)
                            {
                                needsPointerRefresh = true;
                                invalidReason = "bone more than 5m from Base";
                                break;
                            }
                        }

                        if (!needsPointerRefresh)
                        {
                            for (size_t firstBone = 0;
                                firstBone < boneCount && !needsPointerRefresh;
                                ++firstBone)
                            {
                                for (size_t secondBone = firstBone + 1;
                                    secondBone < boneCount;
                                    ++secondBone)
                                {
                                    if (DistanceSquared(
                                            player.bonePositions[firstBone],
                                            player.bonePositions[secondBone]) >
                                        kMaxBoneSeparationSq)
                                    {
                                        needsPointerRefresh = true;
                                        invalidReason = "bones more than 5m apart";
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }

                if (!needsPointerRefresh)
                    continue;

                if (!player.bonePointersNeedResolve)
                {
                    ++boneRefreshesSinceLastLog;

                    constexpr auto kBoneRefreshLogCooldown =
                        std::chrono::minutes(5);

                    if (lastBoneRefreshLog == Clock::time_point{} ||
                        (now - lastBoneRefreshLog) >= kBoneRefreshLogCooldown)
                    {
                        std::ostringstream warning;

                        warning
                            << "[PLAYERS][BONES] Invalid skeleton; refreshing "
                            << "bone pointers | refreshes="
                            << boneRefreshesSinceLastLog
                            << " | lastReason=" << invalidReason
                            << " | lastPlayer=0x"
                            << std::hex << player.instance;

                        boneRefreshWarning = warning.str();
                        boneRefreshesSinceLastLog = 0;
                        lastBoneRefreshLog = now;
                    }
                }

                // Do not keep reading known-bad transforms
                player.bonePointersNeedResolve = true;

                std::fill(
                    player.bonePtrs.begin(),
                    player.bonePtrs.end(),
                    0ULL
                );

                std::fill(
                    player.bonePositions.begin(),
                    player.bonePositions.end(),
                    glm::vec3(0.0f)
                );

                player.boneTransformCache.clear();
            }
        }

        if (!boneRefreshWarning.empty())
            LOGS.logWarn(boneRefreshWarning);
    }
    catch (const std::exception& e)
    {
        LOGS.logError(
            "[PLAYERS][BONES] Exception: " +
            std::string(e.what())
        );
    }
    catch (...)
    {
        LOGS.logError(
            "[PLAYERS][BONES] Unknown exception"
        );
    }

    publishCacheSnapshot(motionUpdated);
}

void PlayerCache::UpdateBonePositions()
{
    size_t count = std::min(bonePtrs.size(),
        std::min(boneTransforms.size(),
            std::min(boneTransformsData.size(),
                bonePositions.size())));

    for (size_t i = 0; i < count; ++i)
    {
        glm::vec3 newPos = GetTransformPosition((int)i);
        bonePositions[i] = newPos;
    }
}

glm::vec3 PlayerCache::GetTransformPosition(int boneIndex)
{
    // Basic safety
    if (boneIndex < 0 ||
        boneIndex >= static_cast<int>(bonePtrs.size()) ||
        boneIndex >= static_cast<int>(boneTransforms.size()) ||
        boneIndex >= static_cast<int>(boneTransformsData.size()) ||
        boneIndex >= static_cast<int>(pMatriciesBuffers.size()) ||
        boneIndex >= static_cast<int>(pIndicesBuffers.size()))
    {
        std::cout << "Failed transformpoisiton safety check" << std::endl;
        return glm::vec3(0.0f);

    }

    TransformAccessReadOnly pTransformAccessReadOnly = boneTransforms[boneIndex];
    TransformData           transformData = boneTransformsData[boneIndex];

    // Buffers filled in boneTask PASS 3
    Matrix34* matrices = static_cast<Matrix34*>(pMatriciesBuffers[boneIndex]);
    int32_t* indices = static_cast<int32_t*>(pIndicesBuffers[boneIndex]);

    if (!matrices || !indices)
        return glm::vec3(0.0f);

    const int index = pTransformAccessReadOnly.index;
    if (index < 0)
        return glm::vec3(0.0f);

    const int count = index + 1;
    if (count <= 0 || count > kMaxTransformChain)
        return glm::vec3(0.0f);

    return computeTransformPosition(matrices, indices, index, count);
}

constexpr size_t kMaxNewPlayersPerTick = 4;

void Players::playersTask()
{
    try
    {
        if (!mem.IsDmaOperational())
            return;

        if (!mainGame.updatePlayerList())
        {
            return;
        }

        std::vector<uint64_t> registeredPlayers;
        registeredPlayers.reserve(mainGame.registeredPlayersCount);

        for (int i = 0; i < mainGame.registeredPlayersCount; ++i)
        {
            const uint64_t currentPlayer =
                mainGame.player_buffer[i];

            if (Utils::valid_pointer(currentPlayer))
                registeredPlayers.emplace_back(currentPlayer);
        }

        if (registeredPlayers.empty())
            return;

        std::unordered_set<uint64_t> existingInstances;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            existingInstances.reserve(playerCache.size());

            for (const PlayerCache& cachedPlayer : playerCache)
            {
                if (Utils::valid_pointer(cachedPlayer.instance))
                {
                    existingInstances.insert(cachedPlayer.instance);
                }
            }
        }

        std::vector<PlayerCache> pendingNewEntities;
        pendingNewEntities.reserve(
            (std::min)(kMaxNewPlayersPerTick, registeredPlayers.size()));

        // Player construction performs several dependent reads. Limit that
        // work per tick and rotate the start so a temporarily bad entity
        // cannot monopolise DMA or prevent later players being discovered.
        static size_t nextNewPlayerCursor = 0;
        nextNewPlayerCursor %= registeredPlayers.size();

        size_t inspected = 0;
        size_t buildAttempts = 0;

        for (; inspected < registeredPlayers.size() &&
            buildAttempts < kMaxNewPlayersPerTick;
            ++inspected)
        {
            const size_t index =
                (nextNewPlayerCursor + inspected) %
                registeredPlayers.size();
            const uint64_t currentPlayer = registeredPlayers[index];

            if (!existingInstances.insert(currentPlayer).second)
                continue;

            ++buildAttempts;

            const bool isLocal = currentPlayer == mainGame.localPlayerPtr;

            auto builtEntity = buildEntity(currentPlayer, isLocal);

            if (!builtEntity.has_value())
                continue;

            // Log before moving the entity into the pending collection.
            watchListManager.logAddPlayer(*builtEntity);

            pendingNewEntities.emplace_back(
                std::move(*builtEntity)
            );
        }

        nextNewPlayerCursor =
            (nextNewPlayerCursor + (std::max)(size_t{ 1 }, inspected)) %
            registeredPlayers.size();

        std::vector<uint64_t> addedPlayerInstances;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::unordered_set<uint64_t> cachedInstances;
            cachedInstances.reserve(
                playerCache.size() + pendingNewEntities.size()
            );

            for (const PlayerCache& cachedPlayer : playerCache)
            {
                if (Utils::valid_pointer(cachedPlayer.instance))
                    cachedInstances.insert(cachedPlayer.instance);
            }

            for (PlayerCache& entity : pendingNewEntities)
            {
                if (!Utils::valid_pointer(entity.instance) ||
                    !cachedInstances.insert(entity.instance).second)
                {
                    continue;
                }

                addedPlayerInstances.emplace_back(entity.instance);
                playerCache.emplace_back(std::move(entity));
            }

            // BTR
            tryFindBTR();

            for (const uint64_t instance : addedPlayerInstances)
            {
                PlayerCache* player =
                    FindPlayerByInstance(playerCache, instance);

                if (!player)
                    continue;

                if (player->isBTR ||
                    player->isDead ||
                    player->hasExfiled)
                {
                    continue;
                }

                player->bonePointersNeedResolve = true;

                // Initial one-time pointer resolution.
                getBonePtrs(*player, true);
            }
        }

        updateEntity();
        recoverBtrStuckPlayers();
        checkGroupIDs();
        checkExfil();

    }
    catch (const std::exception& e)
    {
        LOGS.logError(
            "[PLAYERS] Exception in playersTask: " +
            std::string(e.what())
        );
    }
    catch (...)
    {
        LOGS.logError(
            "[PLAYERS] Unknown exception in playersTask"
        );
    }

    publishCacheSnapshot();
}

inline bool containsIgnoreCase(const std::string& str, const std::string& search)
{
    auto it = std::search(
        str.begin(), str.end(),
        search.begin(), search.end(),
        [](char ch1, char ch2) {
            return std::tolower(ch1) == std::tolower(ch2);
        }
    );
    return it != str.end();
}

AIRole GetAIRoleInfo(const std::string& voiceLine)
{
    if (containsIgnoreCase(voiceLine, "BossSanitar"))        return { "Sanitar", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossBully"))          return { "Reshala", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossGluhar"))         return { "Gluhar", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "SectantPriest"))      return { "Priest", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "SectantWarrior"))     return { "Cultist", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "BossKilla"))          return { "Killa", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossTagilla"))        return { "Tagilla", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Partizan"))      return { "Partisan", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossBigPipe"))        return { "Big Pipe", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossBirdEye"))        return { "Birdeye", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "BossKnight"))         return { "Knight", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Kaban"))         return { "Kaban", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Kollontay"))     return { "Kollontay", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Boss_Sturman"))       return { "Shturman", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "blackdivision") ||
        containsIgnoreCase(voiceLine, "black_division") ||
        containsIgnoreCase(voiceLine, "black division"))      return { "BlackDiv", PlayerType::AIBoss, true };
    if (containsIgnoreCase(voiceLine, "vsrf"))       return { "VSRF", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "civilian"))       return { "Civilian", PlayerType::AIScav };

    //  Arena guards 
    if (containsIgnoreCase(voiceLine, "Arena_Guard"))        return { "Arena Guard", PlayerType::AIScav };

    //  Zombies 
    if (containsIgnoreCase(voiceLine, "BossZombieTagilla"))  return { "Zombie Tagilla", PlayerType::AIBoss };
    if (containsIgnoreCase(voiceLine, "Zombie"))             return { "Zombie", PlayerType::AIScav };

    //  Faction fallbacks 
    if (containsIgnoreCase(voiceLine, "usec"))               return { "Usec", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "bear"))               return { "Bear", PlayerType::AIRaider };
    if (containsIgnoreCase(voiceLine, "scav"))               return { "Scav", PlayerType::AIScav };

    //  Final fallback 
    return { voiceLine, PlayerType::AIBoss };
}

std::optional<PlayerCache> Players::buildEntity(
    const uint64_t instance,
    bool isLocal)
{
    if (!mem.vHandle)
        return std::nullopt;

    if (!Utils::valid_pointer(instance))
        return std::nullopt;

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

    auto TryReadChain = [&](uint64_t base, std::initializer_list<uint64_t> offsets, uint64_t& out) -> bool
        {
            out = 0;

            if (!Utils::valid_pointer(base))
                return false;

            uint64_t current = base;

            for (const uint64_t offset : offsets)
            {
                uint64_t next = 0;

                if (!Utils::valid_pointer(current))
                    return false;

                if (!TryReadPtr(current + offset, next))
                    return false;

                current = next;
            }

            out = current;
            return Utils::valid_pointer(out);
        };

    auto AddFailure = [](std::string& failed, const char* name)
        {
            if (!failed.empty())
                failed += ", ";

            failed += name;
        };

    auto ValidatePtr = [&](std::string& failed, uint64_t ptr, const char* name)
        {
            if (!Utils::valid_pointer(ptr))
                AddFailure(failed, name);
        };

    auto ValidateAddr = [&](std::string& failed, uint64_t address, const char* name)
        {
            if (!Utils::valid_pointer(address))
                AddFailure(failed, name);
        };

    auto ReadUnityStringSafe = [&](uint64_t stringPtr, int maxLen = 128) -> std::string
        {
            if (!Utils::valid_pointer(stringPtr))
                return {};

            int len = 0;

            if (!TryReadValue(stringPtr + 0x10, len))
                return {};

            if (len <= 0 || len > maxLen)
                return {};

            try
            {
                return mem.readUnicodeString(
                    stringPtr + 0x14,
                    len
                );
            }
            catch (...)
            {
                return {};
            }
        };

    auto LogInitFail = [&](const std::string& reason)
        {
            std::ostringstream ss;

            ss << "[PLAYER][INIT] Failed 0x"
                << std::hex << instance
                << " | " << reason;

            // LOGS.logError(ss.str());
        };

    PlayerCache newEntity{};

    try
    {
        newEntity.className = ReadName(instance, 64, false);
    }
    catch (...)
    {
        LogInitFail("ReadName threw exception");
        return std::nullopt;
    }

    if (newEntity.className.empty())
        return std::nullopt;

    newEntity.instance = instance;
    newEntity.isLocal = isLocal;

    newEntity.equipInited = false;
    newEntity.lastEquipmentUpdate = {};
    newEntity.lastHandsUpdate = {};

    newEntity.playerBoneMatrixPtr = 0;
    newEntity.bonePointersNeedResolve = true;
    newEntity.invalidBones = false;

    const bool isOfflineClass =
        newEntity.className == "LocalPlayer" ||
        newEntity.className == "ClientPlayer";

    // ---------------------------------------------------------------------
    // Local / offline player.
    // ---------------------------------------------------------------------
    if (isOfflineClass)
    {
        if (isLocal)
        {
            newEntity.isLocal = true;
            newEntity.name = "LocalPlayer";
        }
        else
        {
            newEntity.isAi = true;
            newEntity.name = "Ai";
        }

        std::string failed;

        TryReadChain(
            instance,
            {
                sdk::Player::_playerBody,
                0x30,
                0x30,
                0x10
            },
            newEntity.playerBoneMatrixPtr
        );

        newEntity.P_CorpseAddr = instance + sdk::Player::Corpse;

        if (!TryReadPtr(
            instance + sdk::Player::Profile,
            newEntity.P_Profile))
        {
            AddFailure(failed, "Profile");
        }

        if (Utils::valid_pointer(newEntity.P_Profile))
        {
            if (!TryReadPtr(
                newEntity.P_Profile + sdk::Profile::Info,
                newEntity.P_Info))
            {
                AddFailure(failed, "ProfileInfo");
            }
        }

        TryReadPtr(
            instance + sdk::Player::ProceduralWeaponAnimation,
            newEntity.P_PWA
        );

        if (!TryReadPtr(
            instance + sdk::Player::_playerBody,
            newEntity.P_Body))
        {
            AddFailure(failed, "PlayerBody");
        }

        newEntity.P_InventoryControllerAddr = instance + sdk::Player::_inventoryController;

        newEntity.P_HandsControllerAddr = instance + sdk::Player::_handsController;

        if (Utils::valid_pointer(newEntity.P_Info))
        {
            if (!TryReadValue(
                newEntity.P_Info + sdk::PlayerInfo::Side,
                newEntity.playerSide))
            {
                AddFailure(failed, "PlayerSide");
            }
        }

        if (!TryReadPtr(
            instance + sdk::Player::MovementContext,
            newEntity.P_MovementContext))
        {
            AddFailure(failed, "MovementContext");
        }

        if (Utils::valid_pointer(newEntity.P_MovementContext))
        {
            newEntity.P_RotationAddress = newEntity.P_MovementContext + sdk::MovementContext::_rotation;
        }

        uint64_t characterController = 0;
        if (TryReadPtr(
            instance + sdk::Player::CharacterController,
            characterController))
        {
            newEntity.P_VelocityAddress =
                characterController +
                sdk::SimpleCharacterController::Velocity;
        }

        ValidateAddr(
            failed,
            newEntity.P_CorpseAddr,
            "CorpseAddr"
        );

        ValidateAddr(
            failed,
            newEntity.P_InventoryControllerAddr,
            "InventoryControllerAddr"
        );

        ValidateAddr(
            failed,
            newEntity.P_HandsControllerAddr,
            "HandsControllerAddr"
        );

        ValidateAddr(
            failed,
            newEntity.P_RotationAddress,
            "RotationAddress"
        );

        if (!failed.empty())
        {
            LogInitFail(failed);
            return std::nullopt;
        }

        if (newEntity.isLocal)
        {
            const bool isSavage =
                (static_cast<uint32_t>(newEntity.playerSide) &
                    static_cast<uint32_t>(EPlayerSide::Savage)) != 0;

            mainGame.localIsSavage = isSavage;

            newEntity.isPlayer = !isSavage;
            newEntity.isPlayerScav = isSavage;

            mainGame.localplayerProfile =
                newEntity.P_Profile;

            try
            {
                questManager.initQuestManager();
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYER][INIT] questManager.initQuestManager failed"
                );
            }
        }

        return newEntity;
    }

    // ---------------------------------------------------------------------
    // Online observed player.
    // ---------------------------------------------------------------------
    std::string failed;

    // Best-effort only. Bone failure is not a player-init failure
    TryReadChain(
        instance,
        {
            sdk::ObservedPlayerView::PlayerBody,
            0x30,
            0x30,
            0x10
        },
        newEntity.playerBoneMatrixPtr
    );

    if (!TryReadPtr(
        instance +
        sdk::ObservedPlayerView::ObservedPlayerController,
        newEntity.P_ObservedPlayerController))
    {
        AddFailure(failed, "ObservedPlayerController");
    }

    if (Utils::valid_pointer(newEntity.P_ObservedPlayerController))
    {
        if (!TryReadPtr(
            newEntity.P_ObservedPlayerController +
            sdk::ObservedPlayerController::HealthController,
            newEntity.P_ObservedHealthController))
        {
            AddFailure(failed, "HealthController");
        }

        newEntity.P_InventoryControllerAddr = newEntity.P_ObservedPlayerController +  sdk::ObservedPlayerController::InventoryController;

        newEntity.P_HandsControllerAddr =  newEntity.P_ObservedPlayerController +  sdk::ObservedPlayerController::HandsController;

        uint64_t observedMovementController = 0;

        if (!TryReadPtr(
            newEntity.P_ObservedPlayerController +
                sdk::ObservedPlayerController::MovementController,
            observedMovementController))
        {
            AddFailure(failed, "MovementContext");
        }
        else
        {
            if (!TryReadPtr(
                observedMovementController +
                    sdk::ObservedMovementController::
                    ObservedPlayerStateContext,
                newEntity.P_MovementContext))
            {
                AddFailure(failed, "MovementContext");
            }
            else
            {
                newEntity.P_VelocityAddress =
                    newEntity.P_MovementContext +
                    sdk::ObservedPlayerStateContext::Velocity;
            }
        }
    }

    if (Utils::valid_pointer(newEntity.P_ObservedHealthController))
    {
        newEntity.P_CorpseAddr = newEntity.P_ObservedHealthController +  sdk::ObservedHealthController::PlayerCorpse;
    }

    if (Utils::valid_pointer(newEntity.P_MovementContext))
    {
        newEntity.P_RotationAddress = newEntity.P_MovementContext + sdk::ObservedPlayerStateContext::Rotation;
    }

    ValidatePtr(
        failed,
        newEntity.P_ObservedPlayerController,
        "ObservedPlayerController"
    );

    ValidatePtr(
        failed,
        newEntity.P_ObservedHealthController,
        "HealthController"
    );

    ValidateAddr(
        failed,
        newEntity.P_CorpseAddr,
        "CorpseAddr"
    );

    ValidateAddr(
        failed,
        newEntity.P_InventoryControllerAddr,
        "InventoryControllerAddr"
    );

    ValidateAddr(
        failed,
        newEntity.P_HandsControllerAddr,
        "HandsControllerAddr"
    );

    ValidatePtr(
        failed,
        newEntity.P_MovementContext,
        "MovementContext"
    );

    ValidateAddr(
        failed,
        newEntity.P_RotationAddress,
        "RotationAddress"
    );

    if (!failed.empty())
    {
        LogInitFail(failed);
        return std::nullopt;
    }

    if (!TryReadValue(
        instance + sdk::ObservedPlayerView::IsAI,
        newEntity.isAi))
    {
        LogInitFail("IsAI read failed");
        return std::nullopt;
    }

    newEntity.isPlayer = !newEntity.isAi;

    if (!TryReadValue(
        instance + sdk::ObservedPlayerView::Side,
        newEntity.playerSide))
    {
        LogInitFail("Side read failed");
        return std::nullopt;
    }

    newEntity.side = SideToString(newEntity.playerSide);

    const bool isSavage = (static_cast<uint32_t>(newEntity.playerSide) &  static_cast<uint32_t>(EPlayerSide::Savage)) != 0;

    if (isSavage)
    {
        if (newEntity.isAi)
        {
            uint64_t voicePtr = 0;

            TryReadPtr(
                instance + sdk::ObservedPlayerView::Voice,
                voicePtr
            );

            const std::string voice =  ReadUnityStringSafe(voicePtr, 128);

            const AIRole role =  GetAIRoleInfo(voice);

            newEntity.name =
                role.Name.empty()
                ? "Ai"
                : role.Name;

            newEntity.isBoss =
                role.Type == PlayerType::AIBoss;
            newEntity.isBlackDivision = role.IsBlackDivision;

            newEntity.isPlayerScav = false;
            newEntity.isAi = true;
            newEntity.isPlayer = false;
        }
        else
        {
            newEntity.name = "PScav " + std::to_string(mainGame.pmcNumber++);

            newEntity.isPlayerScav = true;
            newEntity.isAi = false;
            newEntity.isPlayer = true;
        }
    }
    else
    {
        newEntity.name = "PMC " +  std::to_string(mainGame.pmcNumber++);

        newEntity.isPlayerScav = false;
        newEntity.isAi = false;
        newEntity.isPlayer = true;
    }

    return newEntity;
}

glm::vec3 GetBestPlayerBasePosition(const PlayerCache& cachePlayer)
{
    auto isGoodVec = [](const glm::vec3& v) -> bool
        {
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
                return false;

            constexpr float eps = 0.001f;
            return std::fabs(v.x) >= eps || std::fabs(v.y) >= eps || std::fabs(v.z) >= eps;
        };

    auto safeBone = [&](boneListIndexes idx) -> glm::vec3
        {
            const int slot = static_cast<int>(idx);
            if (slot < 0 || static_cast<size_t>(slot) >= cachePlayer.bonePositions.size())
                return glm::vec3(0.0f);

            return cachePlayer.bonePositions[static_cast<size_t>(slot)];
        };

    const glm::vec3 base = safeBone(boneListIndexes::Base);
    const glm::vec3 lFoot = safeBone(boneListIndexes::LFoot);
    const glm::vec3 rFoot = safeBone(boneListIndexes::RFoot);

    if (isGoodVec(base))
        return base;

    if (isGoodVec(lFoot) && isGoodVec(rFoot))
    {
        const float footSeparation = glm::distance(lFoot, rFoot);
        if (footSeparation > 0.01f && footSeparation <= 5.5f)
            return (lFoot + rFoot) * 0.5f;
    }

    // As a min we can use feet i guess

    if (isGoodVec(lFoot))
        return lFoot;

    if (isGoodVec(rFoot))
        return rFoot;

    return isGoodVec(cachePlayer.location) ? cachePlayer.location : glm::vec3(0.0f);
}

void Players::tryFindBTR()
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

    std::vector<PlayerCache>& cache = players.getCache();

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

        cachePlayer.isBTR = true;
        cachePlayer.isAi = true;
        cachePlayer.isBoss = false;
        cachePlayer.isBlackDivision = false;
        cachePlayer.isPlayer = false;
        cachePlayer.isPlayerScav = false;

        cachePlayer.colour = coloursGlobals::aiBTR;
        cachePlayer.btrView = btrView;
        cachePlayer.name = "BTR";

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

void Players::recoverBtrStuckPlayers()
{
    using Clock = std::chrono::steady_clock;

    static constexpr float kBtrRadius = 4.0f;
    static constexpr float kBtrRadiusSquared = kBtrRadius * kBtrRadius;
    static constexpr float kStaticRotationEpsilon = 0.75f;
    static constexpr int kStaticRotationTicks = 5;
    static constexpr auto kStuckDuration = std::chrono::milliseconds(600);
    static constexpr auto kRecoveryCooldown = std::chrono::seconds(5);

    const Clock::time_point now = Clock::now();

    auto ResetTracking = [](PlayerCache& player)
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

    std::vector<PlayerCache>& cache = players.getCache();

    if (cache.empty())
        return;

    std::vector<glm::vec3> btrPositions;
    btrPositions.reserve(1);

    for (const PlayerCache& player : cache)
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
        for (PlayerCache& player : cache)
            ResetTracking(player);

        return;
    }

    for (PlayerCache& player : cache)
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

namespace
{
    void ApplyPlayerColour(PlayerCache& player)
    {
        player.colour = { 1, 1, 1, 1 };

        if (player.isDead)
        {
            player.colour = coloursGlobals::playerCorpse;
            return;
        }

        if (player.isAi && !player.isPlayerScav && !player.isPlayer)
            player.colour = coloursGlobals::playerAI;

        if (player.isPlayerScav && !player.isAi && player.isPlayer)
            player.colour = coloursGlobals::playerScav;

        if (player.isBoss)
            player.colour = coloursGlobals::playerBoss;

        if (player.isBlackDivision)
            player.colour = coloursGlobals::playerBlackDiv;

        if (player.isPlayer && !player.isPlayerScav && !player.isAi)
            player.colour = coloursGlobals::playerPMC;

        if (player.isWatched)
            player.colour = coloursGlobals::playerWatched;

        if (player.isFriend)
            player.colour = coloursGlobals::playerFriendly;

        if (!mainGame.localGroupId.empty() &&
            player.groupId == mainGame.localGroupId)
        {
            player.colour = coloursGlobals::playerFriendly;
        }

        if (player.isLocal &&
            Utils::valid_pointer(player.instance) &&
            mainGame.localPlayerPtr == player.instance)
        {
            player.colour = coloursGlobals::playerLocal;
        }
    }

    void ResetAimLineTarget(PlayerCache& player)
    {
        player.aimLineTargetConfirmed = false;
        player.aimLineTargetIsLocal = false;
        player.aimLineTargetLocation = {};
        player.aimLineTargetSince = {};
    }

    void UpdateAimLineTarget(PlayerCache& player, const PlayerCacheCollection& cache, std::chrono::steady_clock::time_point now)
    {
        glm::vec3 targetLocation{};
        bool targetIsLocal = false;

        if (!AimLineTargeting::FindLookedAtTarget(
            player,
            cache,
            mainGame.localLocation,
            mainGame.localGroupId,
            radarGlobals::aimLineTargetAngle,
            targetLocation,
            targetIsLocal))
        {
            ResetAimLineTarget(player);
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

void Players::updateEntity()
{
    if (!mem.vHandle)
        return;

    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    // Corpse is the authoritative, low-cost death state. Read it often
    // enough that a dead player leaves the active display promptly.
    static constexpr Milliseconds kCorpseReadInterval{ 250 };
    static constexpr Milliseconds kHealthReadInterval{ 2000 };
    static constexpr Milliseconds kHandsReadInterval{ 2000 };
    static constexpr Milliseconds kFailedReadRetryInterval{ 2500 };

    const Clock::time_point now = Clock::now();

    struct EntityRead
    {
        uint64_t instance = 0;
        bool isBtr = false;
        bool isLocal = false;
        bool isOfflinePlayer = false;

        uint64_t btrView = 0;
        uint64_t rotationAddress = 0;
        uint64_t corpseAddress = 0;
        uint64_t handsControllerAddress = 0;
        uint64_t proceduralWeaponAnimation = 0;
        uint64_t healthController = 0;
        uint64_t velocityAddress = 0;

        glm::vec3 location{};
        glm::vec3 rotationRaw{};
        glm::vec3 velocity{};
        uint64_t corpseClass = 0;
        uint64_t handsController = 0;
        int healthTag = 0;
        bool isAiming = false;

        bool corpseDue = false;
        bool healthDue = false;
        bool handsDue = false;

        bool locationQueued = false;
        bool rotationQueued = false;
        bool corpseQueued = false;
        bool healthQueued = false;
        bool handsQueued = false;
        bool aimingQueued = false;
        bool velocityQueued = false;
    };

    std::vector<EntityRead> reads;

    {
        std::lock_guard<std::mutex> lock(playerMutex);
        reads.reserve(playerCache.size());

        for (const PlayerCache& player : playerCache)
        {
            if (!Utils::valid_pointer(player.instance))
                continue;

            EntityRead read{};
            read.instance = player.instance;
            read.isBtr = player.isBTR;
            read.isLocal = player.isLocal;
            read.location = player.location;
            read.rotationRaw = player.rotationRAW;
            read.velocity = player.velocity;
            read.corpseClass = player.P_CorpseClass;
            read.handsController = player.P_HandsController;
            read.healthTag = player.healthETAG;
            read.isAiming = player.isAiming;

            if (player.isBTR)
            {
                read.btrView = player.btrView;
                reads.emplace_back(std::move(read));
                continue;
            }

            if (player.isDead || player.hasExfiled)
                continue;

            read.isOfflinePlayer =
                player.className == "LocalPlayer" ||
                player.className == "ClientPlayer";

            read.rotationAddress = player.P_RotationAddress;
            read.corpseAddress = player.P_CorpseAddr;
            read.handsControllerAddress = player.P_HandsControllerAddr;
            read.proceduralWeaponAnimation = player.P_PWA;
            read.healthController = player.P_ObservedHealthController;
            read.velocityAddress = player.P_VelocityAddress;

            read.corpseDue =
                player.nextCorpseRead == Clock::time_point{} ||
                now >= player.nextCorpseRead;

            read.handsDue =
                player.nextHandsControllerRead == Clock::time_point{} ||
                now >= player.nextHandsControllerRead;

            read.healthDue =
                !read.isOfflinePlayer &&
                (player.nextHealthRead == Clock::time_point{} ||
                    now >= player.nextHealthRead);

            if (read.corpseDue)
                read.corpseClass = 0;

            if (read.isOfflinePlayer)
                read.isAiming = false;

            reads.emplace_back(std::move(read));
        }
    }

    bool executed = true;
    bool queuedAnything = false;

    if (!reads.empty())
    {
        ScatterReadBatch updateBatch(mem, DmaCacheMode::Uncached, "Player update");

        if (!updateBatch.Valid())
        {
            LOGS.logError(
                "[PLAYERS][UPDATE] Failed to create scatter handle");
            return;
        }

        for (EntityRead& read : reads)
        {
            if (read.isBtr)
            {
                read.locationQueued = updateBatch.Add(
                    read.btrView + sdk::BTRView::previousPosition,
                    read.location);
                queuedAnything = queuedAnything || read.locationQueued;
                continue;
            }

            read.rotationQueued = updateBatch.AddBytes(
                read.rotationAddress,
                &read.rotationRaw,
                sizeof(glm::vec2));
            queuedAnything = queuedAnything || read.rotationQueued;

            if (aimGlobals::predictionEnabled &&
                !read.isLocal &&
                Utils::valid_pointer(read.velocityAddress))
            {
                read.velocityQueued = updateBatch.Add(
                    read.velocityAddress,
                    read.velocity);
                queuedAnything = queuedAnything || read.velocityQueued;
            }

            if (read.corpseDue)
            {
                read.corpseQueued = updateBatch.Add(
                    read.corpseAddress,
                    read.corpseClass);
                queuedAnything = queuedAnything || read.corpseQueued;
            }

            if (read.handsDue)
            {
                read.handsQueued = updateBatch.Add(
                    read.handsControllerAddress,
                    read.handsController);
                queuedAnything = queuedAnything || read.handsQueued;
            }

            if (read.isOfflinePlayer)
            {
                read.aimingQueued = updateBatch.Add(
                    read.proceduralWeaponAnimation +
                        sdk::ProceduralWeaponAnimation::_isAiming,
                    read.isAiming);
                queuedAnything = queuedAnything || read.aimingQueued;
            }
            else if (read.healthDue)
            {
                read.healthQueued = updateBatch.AddBytes(
                    read.healthController +
                        sdk::ObservedHealthController::HealthStatus,
                    &read.healthTag,
                    sizeof(ETagStatus));
                queuedAnything = queuedAnything || read.healthQueued;
            }
        }

        if (queuedAnything)
            executed = updateBatch.Execute();
    }

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (const EntityRead& read : reads)
        {
            PlayerCache* player =
                FindPlayerByInstance(playerCache, read.instance);

            if (!player)
                continue;

            if (executed)
            {
                if (read.locationQueued)
                    player->location = read.location;

                if (read.rotationQueued)
                    player->rotationRAW = read.rotationRaw;

                if (read.velocityQueued)
                {
                    const float speed = glm::length(read.velocity);
                    const bool velocityValid =
                        std::isfinite(read.velocity.x) &&
                        std::isfinite(read.velocity.y) &&
                        std::isfinite(read.velocity.z) &&
                        std::isfinite(speed) &&
                        speed >= 0.1f && speed <= 15.0f;

                    player->velocity = velocityValid
                        ? read.velocity
                        : glm::vec3{};
                    player->velocityValid = velocityValid;
                    player->lastVelocityUpdate = now;
                }

                if (read.corpseQueued)
                    player->P_CorpseClass = read.corpseClass;

                if (read.handsQueued)
                    player->P_HandsController = read.handsController;

                if (read.healthQueued)
                    player->healthETAG = read.healthTag;

                if (read.aimingQueued)
                    player->isAiming = read.isAiming;
            }

            if (read.corpseDue)
            {
                player->nextCorpseRead =
                    executed && read.corpseQueued
                    ? now + kCorpseReadInterval
                    : now + kFailedReadRetryInterval;
            }

            if (read.handsDue)
            {
                player->nextHandsControllerRead =
                    executed && read.handsQueued
                    ? now + kHandsReadInterval
                    : now + kFailedReadRetryInterval;
            }

            if (read.healthDue)
            {
                player->nextHealthRead =
                    executed && read.healthQueued
                    ? now + kHealthReadInterval
                    : now + kFailedReadRetryInterval;
            }
        }
    }

    if (!executed)
    {
        LOGS.logError(
            "[PLAYERS][UPDATE] Player scatter execute failed");
        return;
    }

    const bool localGroupRosterProtectionActive = IsLocalGroupRosterProtectionActive();

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (PlayerCache& player : playerCache)
        {
            if (player.isBTR)
            {
                player.colour = coloursGlobals::aiBTR;
                player.distance = getDistance(
                    player.location,
                    mainGame.localLocation);
                continue;
            }

            if (IsProtectedLocalGroupMember(
                    player,
                    localGroupRosterProtectionActive))
            {
                player.isDead = false;
                player.hasExfiled = false;
                player.P_CorpseClass = 0;
            }

            if (player.isDead || player.hasExfiled)
            {
                player.distance = getDistance(
                    player.location,
                    mainGame.localLocation);
                ApplyPlayerColour(player);
                continue;
            }

            if (Utils::valid_pointer(player.P_CorpseClass))
            {
                player.isDead = true;
                player.distance = getDistance(
                    player.location,
                    mainGame.localLocation);
                ApplyPlayerColour(player);
                continue;
            }

            if (!Utils::valid_pointer(player.instance))
                continue;

            const glm::vec3 newLocation =
                GetBestPlayerBasePosition(player);

            if (newLocation.x != 0.0f ||
                newLocation.y != 0.0f ||
                newLocation.z != 0.0f)
            {
                player.location = newLocation;
            }

            if (player.isLocal)
                mainGame.localLocation = player.location;

            player.distance = getDistance(
                player.location,
                mainGame.localLocation);

            try
            {
                player.rotation =
                    Utils::Player::Rotation::correctRotation2d(
                        player.rotationRAW);
            }
            catch (...)
            {
                player.rotation = {};
                LOGS.logError(
                    "[PLAYERS][UPDATE] Rotation correction failed");
            }

            if (!Utils::valid_pointer(player.P_HandsController))
            {
                player.itemInHand.clear();
                player.lastHeldItemHandsController = 0;
                player.nextHeldItemRefresh = {};
            }
            else if (player.lastHeldItemHandsController !=
                player.P_HandsController)
            {
                player.lastHeldItemHandsController =
                    player.P_HandsController;
                player.nextHeldItemRefresh = now;
            }

            ApplyPlayerColour(player);

            if (player.isLocal &&
                mainGame.localPlayerPtr == player.instance)
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

        for (PlayerCache& player : playerCache)
        {
            if (!Utils::valid_pointer(player.instance) ||
                player.isLocal ||
                player.isBTR ||
                player.isInBTR ||
                player.isDead ||
                player.hasExfiled ||
                player.isZombie)
            {
                ResetAimLineTarget(player);
                continue;
            }

            UpdateAimLineTarget(player, playerCache, now);
        }
    }
}

void Players::playerMetadataTask()
{
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    static constexpr Milliseconds kHeldItemRefreshInterval{ 3000 };
    static constexpr Milliseconds kPredictionHeldItemRefreshInterval{ 250 };
    static constexpr Milliseconds kFailedReadRetryInterval{ 2500 };

    struct MetadataJob
    {
        uint64_t instance = 0;
        PlayerCache player;
        int profileMode = 0;
        bool updateHands = false;
        bool updateWatchStatus = false;
        bool lookupDogTag = false;
        bool lookupProfile = false;
        bool profileLookupSucceeded = false;
        bool handsSucceeded = false;
    };

    const Clock::time_point now = Clock::now();
    std::vector<MetadataJob> jobs;

    {
        std::lock_guard<std::mutex> lock(playerMutex);
        jobs.reserve(playerCache.size());

        for (PlayerCache& player : playerCache)
        {
            if (!Utils::valid_pointer(player.instance) ||
                player.isBTR ||
                player.isDead ||
                player.hasExfiled)
            {
                continue;
            }

            MetadataJob job{};
            job.instance = player.instance;

            if (Utils::valid_pointer(player.P_HandsController))
            {
                job.updateHands =
                    player.nextHeldItemRefresh == Clock::time_point{} ||
                    now >= player.nextHeldItemRefresh;

                if (job.updateHands)
                {
                    const Milliseconds refreshInterval =
                        player.isLocal &&
                        aimGlobals::predictionEnabled
                        ? kPredictionHeldItemRefreshInterval
                        : kHeldItemRefreshInterval;

                    player.nextHeldItemRefresh =
                        now + refreshInterval;
                }
            }

            job.updateWatchStatus = player.isPlayer || player.isPlayerScav;

            job.lookupDogTag =
                player.isPlayer &&
                !player.profileId.empty() &&
                !player.foundDogTagCache &&
                player.name.contains("PMC") &&
                now - player.lastDogTagLookup >
                    std::chrono::seconds(5);

            if (job.lookupDogTag)
                player.lastDogTagLookup = now;

            const int profileMode = std::clamp(radarGlobals::tarkovDevDataMode, 0,  1);
            const unsigned int profileModeBit = 1u << static_cast<unsigned int>(profileMode);
            job.lookupProfile = player.isPlayer && !player.profileId.empty() &&
                !player.accountId.empty() &&
                radarGlobals::getPlayerStats == TRUE &&
                player.profileDataMode != profileMode && (player.attemptedProfileDataModes & profileModeBit) == 0;

            if (job.lookupProfile)
            {
                job.profileMode = profileMode;
                player.attemptedProfileDataModes |= profileModeBit;
            }

            if (!job.updateHands &&
                !job.updateWatchStatus &&
                !job.lookupDogTag &&
                !job.lookupProfile)
            {
                continue;
            }

            job.player = player;
            jobs.emplace_back(std::move(job));
        }
    }

    for (MetadataJob& job : jobs)
    {
        if (job.updateHands)
        {
            try
            {
                job.handsSucceeded =
                    job.player.observedHandsInfo.update(job.player);
            }
            catch (...)
            {
                job.handsSucceeded = false;
                LOGS.logError(
                    "[PLAYERS][METADATA] Hands update failed");
            }
        }

        if (job.updateWatchStatus)
        {
            try
            {
                watchListManager.UpdateWatchStatus(job.player);
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYERS][METADATA] Watch status update failed");
            }
        }

        if (job.lookupDogTag)
        {
            try
            {
                const auto result =
                    g_dogTagCache.GetByProfileId(job.player.profileId);

                if (result.has_value())
                {
                    if (!result->nickname.empty())
                        job.player.name = result->nickname;

                    job.player.accountId = result->accountId;
                    job.player.foundDogTagCache = true;
                }
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYERS][METADATA] Dogtag cache lookup failed");
            }
        }

        if (job.lookupProfile)
        {
            try
            {
                const auto profile =
                    TarkovDevProfileClient::GetProfileForAccountId(job.player.accountId, job.profileMode);

                if (profile)
                {
                    job.player.profileStats = *profile;
                    job.player.hasProfileData = true;
                    job.player.profileDataMode = job.profileMode;
                    job.player.DT_lvl = ConvertXpToLevel(profile->experience);
                    job.player.kd = CalculateKD(profile->Kills, profile->deathsPMC);
                    job.player.pkd = CalculatePKD(profile->killedPMC, profile->deathsPMC);
                    job.player.hours = profile->hoursPlayed;
                    job.profileLookupSucceeded = true;
                }
            }
            catch (...)
            {
                LOGS.logError(
                    "[PLAYERS][METADATA] Tarkov profile lookup failed");
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        for (const MetadataJob& job : jobs)
        {
            PlayerCache* player =
                FindPlayerByInstance(playerCache, job.instance);

            if (!player)
                continue;

            bool raidEntryNeedsRefresh = false;

            if (job.updateHands)
            {
                if (job.handsSucceeded)
                {
                    player->observedHandsInfo =
                        job.player.observedHandsInfo;
                }
                else
                {
                    player->itemInHand.clear();
                    player->nextHeldItemRefresh =
                        now + kFailedReadRetryInterval;
                }
            }

            if (job.updateWatchStatus)
            {
                player->isWatched = job.player.isWatched;
                player->isFriend = job.player.isFriend;

                if (player->isFriend && !player->isLocal)
                {
                    if (!player->friendGroupOverride)
                    {
                        player->groupIdBeforeFriend = player->groupId;
                        player->friendGroupOverride = true;
                    }

                    player->groupId = mainGame.localGroupId;
                }
                else if (player->friendGroupOverride)
                {
                    player->groupId = std::move(player->groupIdBeforeFriend);
                    player->groupIdBeforeFriend.clear();
                    player->friendGroupOverride = false;
                }
            }

            if (job.lookupDogTag)
            {
                player->name = job.player.name;
                player->accountId = job.player.accountId;
                player->foundDogTagCache =
                    job.player.foundDogTagCache;
                raidEntryNeedsRefresh = player->foundDogTagCache;
            }

            if (job.lookupProfile && job.profileLookupSucceeded)
            {
                player->profileStats = job.player.profileStats;
                player->hasProfileData = true;
                player->profileDataMode = job.player.profileDataMode;
                player->DT_lvl = job.player.DT_lvl;
                player->kd = job.player.kd;
                player->pkd = job.player.pkd;
                player->hours = job.player.hours;
                raidEntryNeedsRefresh = true;
            }

            ApplyPlayerColour(*player);

            
            if (raidEntryNeedsRefresh)
                watchListManager.logAddPlayer(*player);
        }
    }

    publishCacheSnapshot();
}
void Players::checkGroupIDs()
{
    if (groupIDSet)
        return;

    if (!mem.vHandle)
        return;

    if (!mainGame.checkIfRaidStarted())
        return;

    constexpr std::size_t MinimumCacheEntries = 5; // More than 4.
    constexpr float GroupDistanceMeters = 30.0f; // max distance between players

    struct GroupSnapshot
    {
        std::uint64_t instance = 0;
        glm::vec3 worldLocation{};
        bool isLocal = false;
    };

    auto isGroupingTarget = [](const PlayerCache& player) -> bool
        {
            if (player.isBTR)
                return false;

            if (player.isDead || player.hasExfiled)
                return false;

            if (!Utils::valid_pointer(player.instance))
                return false;

            if (!(player.isPlayer || player.isPlayerScav))
                return false;

            return true;
        };

    auto hasValidWorldLocation = [](const glm::vec3& position) -> bool
        {
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z))
            {
                return false;
            }

            return position.x != 0.0f ||
                position.y != 0.0f ||
                position.z != 0.0f;
        };

    std::vector<GroupSnapshot> snapshot;
    int localSnapshotIndex = -1;

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        auto& cache = players.getCache();

        if (cache.size() < MinimumCacheEntries)
            return;

        snapshot.reserve(cache.size());

        for (const PlayerCache& player : cache)
        {
            if (!isGroupingTarget(player))
                continue;

            if (!hasValidWorldLocation(player.location))
                return;

            GroupSnapshot entry{};
            entry.instance = player.instance;
            entry.worldLocation = player.location;
            entry.isLocal =
                player.isLocal &&
                player.instance == mainGame.localPlayerPtr;

            const int snapshotIndex = static_cast<int>(snapshot.size());
            snapshot.emplace_back(entry);

            if (entry.isLocal)
                localSnapshotIndex = snapshotIndex;
        }
    }

    //local player is included
    if (localSnapshotIndex < 0)
        return;

    const int targetCount = static_cast<int>(snapshot.size());

    std::vector<int> parent(targetCount);
    for (int i = 0; i < targetCount; ++i)
        parent[i] = i;

    auto Find = [&](int value) -> int
        {
            while (parent[value] != value)
            {
                parent[value] = parent[parent[value]];
                value = parent[value];
            }

            return value;
        };

    auto Union = [&](int left, int right)
        {
            const int leftRoot = Find(left);
            const int rightRoot = Find(right);

            if (leftRoot != rightRoot)
                parent[rightRoot] = leftRoot;
        };

    for (int a = 0; a < targetCount; ++a)
    {
        for (int b = a + 1; b < targetCount; ++b)
        {
            const float distance = players.getDistance(
                snapshot[a].worldLocation,
                snapshot[b].worldLocation
            );

            if (!std::isfinite(distance))
                continue;

            if (distance <= GroupDistanceMeters)
                Union(a, b);
        }
    }

    std::unordered_map<int, int> componentCounts;
    componentCounts.reserve(snapshot.size());

    for (int i = 0; i < targetCount; ++i)
        ++componentCounts[Find(i)];

    std::unordered_map<int, std::string> componentGroupIds;
    componentGroupIds.reserve(componentCounts.size());

    int nextGroupNumber = 1;
    int groupedComponents = 0;
    int groupedPlayers = 0;

    for (int i = 0; i < targetCount; ++i)
    {
        const int root = Find(i);

        if (componentCounts[root] < 2)
            continue;

        if (componentGroupIds.find(root) != componentGroupIds.end())
            continue;

        componentGroupIds.emplace(
            root,
            std::to_string(nextGroupNumber++)
        );

        ++groupedComponents;
    }

    std::unordered_map<std::uint64_t, std::string> assignments;
    assignments.reserve(snapshot.size());

    std::string resolvedLocalGroupId;

    for (int i = 0; i < targetCount; ++i)
    {
        const int root = Find(i);
        std::string groupId;

        const auto groupIt = componentGroupIds.find(root);
        if (groupIt != componentGroupIds.end())
        {
            groupId = groupIt->second;
            ++groupedPlayers;
        }

        // Solo players
        assignments.emplace(snapshot[i].instance, groupId);

        if (i == localSnapshotIndex)
            resolvedLocalGroupId = groupId;
    }

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        auto& cache = players.getCache();

        // If the player list changed between snapshot and commit, do not finalise a partial/incorrect one-time grouping pass
        std::unordered_map<std::uint64_t, PlayerCache*> currentPlayers;
        currentPlayers.reserve(cache.size());

        for (PlayerCache& player : cache)
        {
            if (Utils::valid_pointer(player.instance))
                currentPlayers.emplace(player.instance, &player);
        }

        for (const GroupSnapshot& entry : snapshot)
        {
            if (currentPlayers.find(entry.instance) == currentPlayers.end())
                return;
        }

        for (const auto& [instance, groupId] : assignments)
        {
            const auto playerIt = currentPlayers.find(instance);
            if (playerIt != currentPlayers.end())
                playerIt->second->groupId = groupId;
        }
    }

    // Empty means local player is solo.
    mainGame.localGroupId = resolvedLocalGroupId;

    // Scan is complete even if every player is solo
    groupIDSet = true;

    std::ostringstream ss;
    ss << "[PLAYERS][GROUP] One-time grouping complete"
        << " | cache=" << snapshot.size()
        << " | groups=" << groupedComponents
        << " | groupedPlayers=" << groupedPlayers
        << " | solos=" << (targetCount - groupedPlayers)
        << " | localGroupId="
        << (mainGame.localGroupId.empty()
            ? "none"
            : mainGame.localGroupId);

    LOGS.logInfo(ss.str());
}

static const std::unordered_set<std::string> skipNames =
{
    "Compass",
    "ArmBand",
    "Eyewear",
    "Pockets"
};

void Players::playerEquipment()
{
    if (!radarGlobals::getPlayerEquip)
        return;

    const QuestPublishedSnapshot questSnapshot =
        GetQuestPublishedSnapshot();
    const std::vector<std::string>& questItemIds =
        questSnapshot->masterItems;

    using Clock = std::chrono::steady_clock;
    using SlotVec =
        std::remove_reference_t<
        decltype(std::declval<PlayerCache&>()._slots)
        >;

    using SlotEntry = typename SlotVec::value_type;

    using PlayerValueT =
        std::remove_reference_t<
        decltype(std::declval<PlayerCache&>().playerValue)
        >;

    static constexpr size_t kMaxEquipmentInitPerPass = 2;
    static constexpr size_t kMaxEquipmentScanPerPass = 1;

    static size_t initRoundRobinCursor = 0;
    static size_t scanRoundRobinCursor = 0;

    struct InitJob
    {
        uint64_t instance = 0;
        uint64_t inventoryControllerAddr = 0;

        uint64_t inventoryController = 0;
        uint64_t inventory = 0;
        uint64_t equipment = 0;
        uint64_t slotsPtr = 0;
    };

    struct InitResult
    {
        uint64_t instance = 0;
        SlotVec slots;
        bool success = false;
    };

    struct ScanJob
    {
        uint64_t instance = 0;
        bool isPlayer = false;
        std::string profileId;
        SlotVec slots;
        Clock::time_point updateTime{};
    };

    struct SlotRead
    {
        size_t jobIndex = 0;
        size_t slotIndex = 0;

        uint64_t containedItem = 0;
        uint64_t itemTemplate = 0;
        MongoID mongoId{};
    };

    struct ScanResult
    {
        uint64_t instance = 0;
        SlotVec slots;
        PlayerValueT playerValue{};
        Clock::time_point updateTime{};

        bool hasProfileUpdate = false;
        std::string profileId;
        std::string accountId;
        std::string nickname;
        int lvl = 0;
    };

    auto findPlayerByInstance = [](
        std::vector<PlayerCache>& cache,
        uint64_t instance) -> PlayerCache*
        {
            for (auto& player : cache)
            {
                if (player.instance == instance)
                    return &player;
            }

            return nullptr;
        };

    auto executeScatter = [&](auto&& queueReads) -> bool
        {
            ScatterReadBatch batch(
                mem,
                DmaCacheMode::Cached,
                "Player equipment"
            );

            if (!batch.Valid())
                return false;

            bool queuedAnything = false;

            try
            {
                queuedAnything = queueReads(batch);
            }
            catch (...)
            {
                return false;
            }

            if (!queuedAnything)
                return true;

            return batch.Execute();
        };

    auto takeInitBatch = [&](
        std::vector<InitJob>& candidates) -> std::vector<InitJob>
        {
            std::vector<InitJob> batch;

            if (candidates.empty())
            {
                initRoundRobinCursor = 0;
                return batch;
            }

            const size_t total = candidates.size();
            const size_t count =
                std::min(kMaxEquipmentInitPerPass, total);

            const size_t start =
                initRoundRobinCursor % total;

            batch.reserve(count);

            for (size_t i = 0; i < count; ++i)
            {
                const size_t index =
                    (start + i) % total;

                batch.emplace_back(
                    std::move(candidates[index])
                );
            }

            initRoundRobinCursor =
                (start + count) % total;

            return batch;
        };

    auto takeScanBatch = [&](
        std::vector<ScanJob>& candidates) -> std::vector<ScanJob>
        {
            std::vector<ScanJob> batch;

            if (candidates.empty())
            {
                scanRoundRobinCursor = 0;
                return batch;
            }

            const size_t total = candidates.size();
            const size_t count =
                std::min(kMaxEquipmentScanPerPass, total);

            const size_t start =
                scanRoundRobinCursor % total;

            batch.reserve(count);

            for (size_t i = 0; i < count; ++i)
            {
                const size_t index =
                    (start + i) % total;

                batch.emplace_back(
                    std::move(candidates[index])
                );
            }

            scanRoundRobinCursor =
                (start + count) % total;

            return batch;
        };

    try
    {
        //Build slot-pointer-cache init candidates
        std::vector<InitJob> initCandidates;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache =
                players.getCache();

            initCandidates.reserve(cache.size());

            for (const PlayerCache& player : cache)
            {
                if (!Utils::valid_pointer(player.instance))
                    continue;

                if (player.isBTR ||
                    player.isDead ||
                    player.hasExfiled)
                {
                    continue;
                }

                if (player.equipInited)
                    continue;

                if (!Utils::valid_pointer(
                    player.P_InventoryControllerAddr))
                {
                    continue;
                }

                InitJob job{};
                job.instance = player.instance;
                job.inventoryControllerAddr =
                    player.P_InventoryControllerAddr;

                initCandidates.emplace_back(std::move(job));
            }
        }

        std::vector<InitJob> initJobs =
            takeInitBatch(initCandidates);

        bool initReadSuccess = true;

        if (!initJobs.empty())
        {
            // InventoryController address -> controller pointer.
            initReadSuccess = executeScatter([&](auto& batch)
                {
                    bool queued = false;

                    for (InitJob& job : initJobs)
                    {
                        if (!Utils::valid_pointer(
                            job.inventoryControllerAddr))
                        {
                            continue;
                        }

                        if (batch.Add(
                            job.inventoryControllerAddr,
                            job.inventoryController))
                        {
                            queued = true;
                        }
                    }

                    return queued;
                });

            // Controller -> Inventory.
            if (initReadSuccess)
            {
                initReadSuccess = executeScatter([&](auto& batch)
                    {
                        bool queued = false;

                        for (InitJob& job : initJobs)
                        {
                            if (!Utils::valid_pointer(
                                job.inventoryController))
                            {
                                continue;
                            }

                            if (batch.Add(
                                job.inventoryController + sdk::InventoryController::Inventory,
                                job.inventory))
                            {
                                queued = true;
                            }
                        }

                        return queued;
                    });
            }

            // Inventory -> Equipment.
            if (initReadSuccess)
            {
                initReadSuccess = executeScatter([&](auto& batch)
                    {
                        bool queued = false;

                        for (InitJob& job : initJobs)
                        {
                            if (!Utils::valid_pointer(job.inventory))
                                continue;

                            if (batch.Add(
                                job.inventory + sdk::Inventory::Equipment,
                                job.equipment))
                            {
                                queued = true;
                            }
                        }

                        return queued;
                    });
            }

            // Equipment -> cached slots array pointer.
            if (initReadSuccess)
            {
                initReadSuccess = executeScatter([&](auto& batch)
                    {
                        bool queued = false;

                        for (InitJob& job : initJobs)
                        {
                            if (!Utils::valid_pointer(job.equipment))
                                continue;

                            if (batch.Add(
                                job.equipment + sdk::InventoryEquipment::_cachedSlots,
                                job.slotsPtr))
                            {
                                queued = true;
                            }
                        }

                        return queued;
                    });
            }

            if (!initReadSuccess)
            {
                LOGS.logError(
                    "[PLAYER][EQUIP] Slot-cache init scatter failed"
                );
            }
        }

        //Build cached slot list for the successful init batch.
        std::vector<InitResult> initResults;
        initResults.reserve(initJobs.size());

        if (initReadSuccess)
        {
            for (const InitJob& job : initJobs)
            {
                if (!Utils::valid_pointer(job.slotsPtr))
                    continue;

                InitResult result{};
                result.instance = job.instance;

                try
                {
                    UnityArray<uint64_t> slotsArray(
                        job.slotsPtr,
                        "Player equipment slots",
                        128);

                    const int slotCount =
                        static_cast<int>(slotsArray.count);

                    if (slotCount < 0 || slotCount > 128)
                        continue;

                    for (const uint64_t slotPtr : slotsArray)
                    {
                        if (!Utils::valid_pointer(slotPtr))
                            continue;

                        const uint64_t namePtr =
                            mem.Read<uint64_t>(
                                slotPtr + sdk::Slot::ID
                            );

                        if (!Utils::valid_pointer(namePtr))
                            continue;

                        const int nameLen =
                            mem.Read<int>(namePtr + 0x10);

                        if (nameLen <= 0 || nameLen > 128)
                            continue;

                        const std::string name =
                            mem.readUnicodeString(
                                namePtr + 0x14,
                                nameLen
                            );

                        if (name.empty())
                            continue;

                        if (skipNames.contains(name))
                            continue;

                        result.slots.push_back({
                            name,
                            slotPtr
                            });
                    }

                    result.success = true;
                    initResults.emplace_back(std::move(result));
                }
                catch (...)
                {
                    LOGS.logError(
                        "[PLAYER][EQUIP] Failed to build slot cache"
                    );
                }
            }
        }

        // Apply slot-pointer cache.
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache =
                players.getCache();

            for (InitResult& result : initResults)
            {
                PlayerCache* player =
                    findPlayerByInstance(
                        cache,
                        result.instance
                    );

                if (!player)
                    continue;

                if (!Utils::valid_pointer(player->instance))
                    continue;

                if (player->isBTR ||
                    player->isDead ||
                    player->hasExfiled)
                {
                    continue;
                }

                if (player->equipInited)
                    continue;

                if (!result.success)
                    continue;

                player->_slots = std::move(result.slots);
                player->equipInited = true;

                // Forces the first contents scan immediately
                player->lastEquipmentUpdate = {};
            }
        }

        // Stage 3: Build normal contents-update candidates
        // Each cached player is due every 5 seconds
        std::vector<ScanJob> scanCandidates;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache =
                players.getCache();

            scanCandidates.reserve(cache.size());

            const Clock::time_point now = Clock::now();

            for (const PlayerCache& player : cache)
            {
                if (!Utils::valid_pointer(player.instance))
                    continue;

                if (player.isBTR ||
                    player.isDead ||
                    player.hasExfiled)
                {
                    continue;
                }

                if (!player.equipInited)
                    continue;

                if (player._slots.empty())
                    continue;

                if (now - player.lastEquipmentUpdate <
                    player.equipmentUpdateInterval)
                {
                    continue;
                }

                ScanJob job{};
                job.instance = player.instance;
                job.isPlayer = player.isPlayer;
                job.profileId = player.profileId;
                job.slots = player._slots;
                job.updateTime = now;

                scanCandidates.emplace_back(std::move(job));
            }
        }

        std::vector<ScanJob> scanJobs =
            takeScanBatch(scanCandidates);

        if (scanJobs.empty())
            return;

        //Flatten all slots from up to five players
        std::vector<SlotRead> slotReads;

        for (size_t jobIndex = 0;
            jobIndex < scanJobs.size();
            ++jobIndex)
        {
            const ScanJob& job =
                scanJobs[jobIndex];

            for (size_t slotIndex = 0;
                slotIndex < job.slots.size();
                ++slotIndex)
            {
                const SlotEntry& slot =
                    job.slots[slotIndex];

                if (job.isPlayer &&
                    slot.name == "Scabbard")
                {
                    continue;
                }

                if (!Utils::valid_pointer(slot.addr))
                    continue;

                SlotRead read{};
                read.jobIndex = jobIndex;
                read.slotIndex = slotIndex;

                slotReads.emplace_back(std::move(read));
            }
        }

        //Read slot contents -> template -> Mongo item ID
        bool scanReadSuccess = true;

        if (!slotReads.empty())
        {
            scanReadSuccess = executeScatter([&](auto& batch)
                {
                    bool queued = false;

                    for (SlotRead& read : slotReads)
                    {
                        const ScanJob& job =
                            scanJobs[read.jobIndex];

                        const SlotEntry& slot =
                            job.slots[read.slotIndex];

                        if (!Utils::valid_pointer(slot.addr))
                            continue;

                        if (batch.Add(
                            slot.addr +
                            sdk::Slot::ContainedItem,
                            read.containedItem))
                        {
                            queued = true;
                        }
                    }

                    return queued;
                });

            if (scanReadSuccess)
            {
                scanReadSuccess = executeScatter([&](auto& batch)
                    {
                        bool queued = false;

                        for (SlotRead& read : slotReads)
                        {
                            if (!Utils::valid_pointer(
                                read.containedItem))
                            {
                                continue;
                            }

                            if (batch.Add(
                                read.containedItem +
                                sdk::LootItem::Template,
                                read.itemTemplate))
                            {
                                queued = true;
                            }
                        }

                        return queued;
                    });
            }

            if (scanReadSuccess)
            {
                scanReadSuccess = executeScatter([&](auto& batch)
                    {
                        bool queued = false;

                        for (SlotRead& read : slotReads)
                        {
                            if (!Utils::valid_pointer(
                                read.itemTemplate))
                            {
                                continue;
                            }

                            if (batch.Add(
                                read.itemTemplate +
                                sdk::ItemTemplate::_id,
                                read.mongoId))
                            {
                                queued = true;
                            }
                        }

                        return queued;
                    });
            }
        }

        if (!scanReadSuccess)
        {
            LOGS.logError(
                "[PLAYER][EQUIP] Slot-content scatter failed"
            );

            return;
        }

        //create result copies outside the player-cache lock
        std::vector<ScanResult> scanResults;
        scanResults.reserve(scanJobs.size());

        for (ScanJob& job : scanJobs)
        {
            ScanResult result{};

            result.instance = job.instance;
            result.updateTime = job.updateTime;
            result.slots = std::move(job.slots);
            result.playerValue = 0;

            scanResults.emplace_back(std::move(result));
        }

        for (SlotRead& read : slotReads)
        {
            if (read.jobIndex >= scanJobs.size() ||
                read.jobIndex >= scanResults.size())
            {
                continue;
            }

            ScanJob& job =
                scanJobs[read.jobIndex];

            ScanResult& result =
                scanResults[read.jobIndex];

            if (read.slotIndex >= result.slots.size())
                continue;

            SlotEntry& slot =
                result.slots[read.slotIndex];

            slot.wanted = false;
            slot.price = 0;
            slot.equipName.clear();

            if (!Utils::valid_pointer(read.containedItem))
                continue;

            // Dogtag profile ID lookup.
            if (job.isPlayer &&
                job.profileId.empty() &&
                !result.hasProfileUpdate)
            {
                const std::string className =
                    ReadName(read.containedItem);

                if (className == "BarterOther")
                {
                    const uint64_t dogtag =
                        mem.Read<uint64_t>(
                            read.containedItem +
                            sdk::BarterOtherOffsets::Dogtag
                        );

                    if (!Utils::valid_pointer(dogtag))
                    {
                        LOGS.logError(
                            "[DOGTAG] Pointer to dogtag failed"
                        );
                    }
                    else
                    {
                        const uint64_t profileIdPtr =
                            dogtag +
                            sdk::DogtagComponent::ProfileId;

                        if (!Utils::valid_pointer(profileIdPtr))
                        {
                            LOGS.logError(
                                "[DOGTAG] Pointer to profile string failed"
                            );
                        }
                        else
                        {
                            const std::string readString =
                                mem.readUnityStringField(
                                    profileIdPtr,
                                    256
                                );

                            if (!readString.empty())
                            {
                                result.hasProfileUpdate = true;
                                result.profileId = readString;

                                if (g_DogTagAPI.hasApiKey())
                                {
                                    const auto apiResult =
                                        g_DogTagAPI.getByProfile(
                                            result.profileId
                                        );

                                    if (apiResult)
                                    {
                                        if (!apiResult->accountId.empty())
                                        {
                                            result.accountId =
                                                apiResult->accountId;
                                        }

                                        if (!apiResult->nickname.empty())
                                        {
                                            result.nickname =
                                                apiResult->nickname;
                                        }

                                        if (apiResult->lvl > 0)
                                        {
                                            result.lvl = apiResult->lvl;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            const std::string id =
                TrimEFT(read.mongoId.ReadString(mem));

            if (id.empty())
                continue;

            for (const auto& marketItem : marketList)
            {
                if (marketItem.bsgid != id)
                    continue;

                slot.equipName =
                    marketItem.shortName;

                slot.price =
                    marketItem.marketPrice == 0
                    ? marketItem.traderPrice
                    : marketItem.marketPrice;

                break;
            }

            for (const auto& filter : lootFilters)
            {
                if (!filter.active)
                    continue;

                bool found = false;

                for (const auto& filterItem :
                    filter.lootItems)
                {
                    if (id != filterItem.bsgid)
                        continue;

                    slot.wanted = true;
                    found = true;
                    break;
                }

                if (found)
                    break;
            }

            if (!slot.wanted)
            {
                for (const auto& questId : questItemIds)
                {
                    if (questId == id)
                    {
                        slot.wanted = true;
                        break;
                    }
                }
            }

            if (!slot.wanted)
            {
                for (const auto& wishlistItem :
                    wishListData)
                {
                    if (wishlistItem.bsgId == id)
                    {
                        slot.wanted = true;
                        break;
                    }
                }
            }

            if (!slot.wanted &&
                lootGlobals::enableValueLoot &&
                slot.price >= lootGlobals::valueLootFromEquip)
            {
                slot.wanted = true;
            }
        }

        //Calculate value for each scanned player
        for (ScanResult& result : scanResults)
        {
            result.playerValue = 0;

            for (const SlotEntry& slot : result.slots)
            {
                const std::string slotName =
                    TrimEFT(slot.name);

                if (slotName == "SecuredContainer" ||
                    slotName == "Dogtag" ||
                    slotName == "Scabbard")
                {
                    continue;
                }

                if (slot.price <= 0)
                    continue;

                result.playerValue +=
                    static_cast<PlayerValueT>(
                        slot.price
                        );
            }
        }

        //Apply updated contents and next 5-second scan time
        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache =
                players.getCache();

            for (ScanResult& result : scanResults)
            {
                PlayerCache* player =
                    findPlayerByInstance(
                        cache,
                        result.instance
                    );

                if (!player)
                    continue;

                if (!Utils::valid_pointer(player->instance))
                    continue;

                if (player->isBTR ||
                    player->isDead ||
                    player->hasExfiled)
                {
                    continue;
                }

                player->_slots = std::move(result.slots);
                player->playerValue = result.playerValue;

                
                player->lastEquipmentUpdate = result.updateTime;

                if (result.hasProfileUpdate &&
                    player->profileId.empty())
                {
                    player->profileId =
                        result.profileId;

                    if (!result.accountId.empty())
                    {
                        player->accountId =
                            result.accountId;
                    }

                    if (!result.nickname.empty())
                    {
                        player->name =
                            result.nickname;
                    }

                    if (result.lvl > 0)
                    {
                        player->DT_lvl = result.lvl;
                    }

                    //update watchlist raid list pid
                    watchListManager.logUpdatePlayerPID(*player);
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError(
            "[PLAYER][EQUIP] Exception: ",
            e.what()
        );
    }
    catch (...)
    {
        LOGS.logError(
            "[PLAYER][EQUIP] Unknown exception."
        );
    }

    publishCacheSnapshot();
}

std::string Players::heldItemName(PlayerCache& player)
{
    try
    {
        std::string ItemHand = player.itemInHand;
        
        auto now = std::chrono::steady_clock::now();
        if (now - player.lastHandsUpdate < player.handsUpdateInterval)
            return ItemHand;

        player.lastHandsUpdate = now;


        if (!Utils::valid_pointer(player.P_MovementContext))
        {
            //std::cout << "[ITEMINHAND] observedhands memory read 1\n";
            return ItemHand;
        }

        uint64_t observedHands = mem.Read<uint64_t>(player.P_MovementContext + sdk::ObservedMovementState::ObservedPlayerHands);
        if (!Utils::valid_pointer(observedHands))
        {
            //std::cout << "[ITEMINHAND] observedhands memory read 2\n";
            return ItemHand;
        }

        uint64_t itemBase = mem.Read<uint64_t>(observedHands + sdk::ObservedPlayerHands::Item);
        if (!Utils::valid_pointer(itemBase))
        {
            ItemHand = "--";
            return ItemHand;
        }

        if (itemBase != player._lastObservedHands)
        {
            player._lastObservedHands = itemBase;

            // query template for item
            ItemHand = this->ReadNameFromHandsItem(itemBase);
            
        }
        if (ItemHand == "--ERR--")
            player._lastObservedHands = NULL;

        return ItemHand;


    }
    catch (...)
    {
        return "--EX--";
    }

    

}

std::string Players::ReadNameFromHandsItem(uint64_t itemBase)
{

    uint64_t itemTemp = 0x0;

    if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItem::Template,itemTemp))
		return "--ERR--";

    auto mongoId = mem.Read<MongoID>(itemTemp + sdk::ItemTemplate::_id);
    auto itemId = TrimEFT(mongoId.ReadString(mem, 64));
    
    if (itemId.empty())
        return "--ERR2--";

    for (auto& ml : marketList)
    {
        if (ml.bsgid != itemId.c_str())
            continue;

        return ml.shortName;
    }

}

static inline bool TryReadAmmoTemplateFromRound(uint64_t roundPtr, uint64_t& ammoTemplate)
{
    ammoTemplate = 0;

    if (!Utils::valid_pointer(roundPtr))
        return false;

    if (!mem.TryRead<uint64_t>(roundPtr + sdk::LootItem::Template, ammoTemplate))
        return false;

    return Utils::valid_pointer(ammoTemplate);
}

static inline bool CountLoadedChamberArray(
    uint64_t chambersPtr,
    uint64_t& firstRound,
    int& currentAmmoCount,
    int& maxAmmoCount
)
{
    if (!Utils::valid_pointer(chambersPtr))
        return false;

    UnityArray<Chamber> chambers(
        chambersPtr,
        "Weapon chambers",
        64);

    if (chambers.count <= 0)
        return false;

    maxAmmoCount += chambers.count;

    for (int i = 0; i < chambers.count; ++i)
    {
        Chamber chamber = chambers[i];

        if (!chamber.HasBullet(true))
            continue;

        ++currentAmmoCount;

        // Keep first valid round for ammo type
        if (!Utils::valid_pointer(firstRound))
        {
            const uint64_t chamberPtr = static_cast<uint64_t>(chamber);

            if (!Utils::valid_pointer(chamberPtr))
                continue;

            uint64_t containedItem = 0;

            if (mem.TryRead<uint64_t>(
                chamberPtr + sdk::Slot::ContainedItem,
                containedItem) &&
                Utils::valid_pointer(containedItem))
            {
                firstRound = containedItem;
            }
        }
    }

    return true;
}


static inline bool TryGetAmmoTemplateFromWeapon(
    uint64_t itemBase,
    uint64_t& ammoTemplate,
    int& chamberCount,
    int& magazineCount
)
{
    ammoTemplate = 0;

    int currentAmmoCount = 0;
    int maxAmmoCount = 0;

    uint64_t firstRound = 0;

    // ----------------------------------------------------
    // 1. Weapon chamber path
    // Count chamber ammo, but DO NOT return here.
    // Normal guns still need the magazine counted after this.
    // ----------------------------------------------------
    uint64_t chambersPtr = 0;

    if (mem.TryRead<uint64_t>(itemBase + sdk::LootItemWeapon::Chambers, chambersPtr) &&
        Utils::valid_pointer(chambersPtr))
    {
        CountLoadedChamberArray(
            chambersPtr,
            firstRound,
            currentAmmoCount,
            maxAmmoCount
        );
    }

    // ----------------------------------------------------
    // 2. Magazine path
    // ----------------------------------------------------
    uint64_t magSlot = 0;
    uint64_t magItemPtr = 0;

    if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItemWeapon::magSlotCache, magSlot) ||
        !Utils::valid_pointer(magSlot))
    {
        return false;
    }

    if (!mem.TryRead<uint64_t>(magSlot + sdk::Slot::ContainedItem, magItemPtr) ||
        !Utils::valid_pointer(magItemPtr))
    {
        return false;
    }

    // ----------------------------------------------------
    // 3. Magazine chambers path
    // Revolvers, etc.
    // ----------------------------------------------------
    uint64_t magChambersPtr = 0;

    if (mem.TryRead<uint64_t>(magItemPtr + sdk::LootItemMod::Slots, magChambersPtr) &&
        Utils::valid_pointer(magChambersPtr))
    {
        UnityArray<Chamber> magChambers(
            magChambersPtr,
            "Magazine chambers",
            64);

        if (magChambers.count > 0)
        {
            CountLoadedChamberArray(
                magChambersPtr,
                firstRound,
                currentAmmoCount,
                maxAmmoCount
            );

            chamberCount = currentAmmoCount;
            magazineCount = maxAmmoCount;

            return TryReadAmmoTemplateFromRound(firstRound, ammoTemplate);
        }
    }

    // ----------------------------------------------------
    // 4. Regular magazine stack path
    // ----------------------------------------------------
    uint64_t cartridges = 0;
    uint64_t magStackPtr = 0;

    if (!mem.TryRead<uint64_t>(magItemPtr + 0xA8, cartridges) ||
        !Utils::valid_pointer(cartridges))
    {
        return false;
    }

    if (!mem.TryRead<uint64_t>(cartridges + sdk::StackSlot::items, magStackPtr) ||
        !Utils::valid_pointer(magStackPtr))
    {
        return false;
    }

    int magMaxCount = 0;

    if (!mem.TryRead<int>(cartridges + sdk::StackSlot::MaxCount, magMaxCount))
        magMaxCount = 0;

    if (magMaxCount < 0)
        magMaxCount = 0;

    maxAmmoCount += magMaxCount;

    UnityList<uint64_t> magStack =
        UnityList<uint64_t>::Create(
            magStackPtr,
            DmaCacheMode::Cached,
            512);

    if (magStack.count() > 0)
    {
        for (const auto& stack : magStack)
        {
            if (!Utils::valid_pointer(stack))
                continue;

            int stackNumber = 0;

            if (!mem.TryRead<int>(stack + 0x24, stackNumber))
                continue;

            if (stackNumber < 0)
                continue;

            currentAmmoCount += stackNumber;

            // If no chamber round was found, use the first mag round for ammo type
            if (!Utils::valid_pointer(firstRound))
                firstRound = stack;
        }
    }

    chamberCount = currentAmmoCount;
    magazineCount = maxAmmoCount;

    return TryReadAmmoTemplateFromRound(firstRound, ammoTemplate);
}

static void AccumulateWeaponVelocityModifiers(
    uint64_t itemBase,
    float& velocityModifier,
    std::unordered_set<uint64_t>& visitedItems,
    size_t depth,
    size_t& itemCount)
{
    constexpr size_t kMaximumAttachmentDepth = 8;
    constexpr size_t kMaximumAttachmentItems = 64;

    if (!Utils::valid_pointer(itemBase) ||
        depth >= kMaximumAttachmentDepth ||
        itemCount >= kMaximumAttachmentItems ||
        !visitedItems.insert(itemBase).second)
    {
        return;
    }

    ++itemCount;

    uint64_t slotsPointer = 0;
    if (!mem.TryRead<uint64_t>(
        itemBase + sdk::LootItemMod::Slots,
        slotsPointer) ||
        !Utils::valid_pointer(slotsPointer))
    {
        return;
    }

    try
    {
        UnityArray<uint64_t> slots(
            slotsPointer,
            "Weapon velocity modifier slots",
            100);

        for (const uint64_t slot : slots)
        {
            if (!Utils::valid_pointer(slot) ||
                itemCount >= kMaximumAttachmentItems)
            {
                continue;
            }

            uint64_t containedItem = 0;
            uint64_t itemTemplate = 0;
            float attachmentModifier = 0.0f;

            if (!mem.TryRead<uint64_t>(
                slot + sdk::Slot::ContainedItem,
                containedItem) ||
                !Utils::valid_pointer(containedItem) ||
                !mem.TryRead<uint64_t>(
                    containedItem + sdk::LootItem::Template,
                    itemTemplate) ||
                !Utils::valid_pointer(itemTemplate))
            {
                continue;
            }

            if (mem.TryRead<float>(
                itemTemplate + sdk::ModTemplate::Velocity,
                attachmentModifier) &&
                std::isfinite(attachmentModifier) &&
                std::fabs(attachmentModifier) <= 100.0f)
            {
                velocityModifier += attachmentModifier;
            }

            AccumulateWeaponVelocityModifiers(
                containedItem,
                velocityModifier,
                visitedItems,
                depth + 1,
                itemCount);
        }
    }
    catch (...)
    {
        // A malformed attachment branch should not invalidate the ammo data.
    }
}

static bool TryReadWeaponVelocityModifier(
    uint64_t weaponItem,
    uint64_t weaponTemplate,
    float& output)
{
    output = 0.0f;

    if (!Utils::valid_pointer(weaponItem) ||
        !Utils::valid_pointer(weaponTemplate) ||
        !mem.TryRead<float>(
            weaponTemplate + sdk::WeaponTemplate::Velocity,
            output) ||
        !std::isfinite(output) ||
        std::fabs(output) > 100.0f)
    {
        return false;
    }

    std::unordered_set<uint64_t> visitedItems;
    visitedItems.reserve(32);
    size_t attachmentItemCount = 0;

    AccumulateWeaponVelocityModifiers(
        weaponItem,
        output,
        visitedItems,
        0,
        attachmentItemCount);

    const float velocityFactor = 1.0f + output / 100.0f;
    return
        std::isfinite(velocityFactor) &&
        velocityFactor > 0.0f &&
        velocityFactor < 2.0f;
}

static bool TryReadWeaponBallistics(
    uint64_t ammoTemplate,
    float velocityModifier,
    BallisticsInfo& output)
{
    output = {};

    if (!Utils::valid_pointer(ammoTemplate) ||
        !std::isfinite(velocityModifier))
    {
        return false;
    }

    float initialSpeed = 0.0f;
    float ballisticCoefficient = 0.0f;
    float bulletMass = 0.0f;
    float bulletDiameter = 0.0f;

    const Memory::ScatterReadRequest requests[] =
    {
        {
            ammoTemplate + sdk::AmmoTemplate::InitialSpeed,
            &initialSpeed,
            sizeof(initialSpeed)
        },
        {
            ammoTemplate + sdk::AmmoTemplate::BallisticCoefficient,
            &ballisticCoefficient,
            sizeof(ballisticCoefficient)
        },
        {
            ammoTemplate + sdk::AmmoTemplate::BulletMassGrams,
            &bulletMass,
            sizeof(bulletMass)
        },
        {
            ammoTemplate + sdk::AmmoTemplate::BulletDiameterMillimeters,
            &bulletDiameter,
            sizeof(bulletDiameter)
        }
    };

    if (!mem.ReadScatter(
        requests,
        std::size(requests),
        DmaCacheMode::Cached,
        "Weapon ballistics"))
    {
        return false;
    }

    const float velocityFactor = 1.0f + velocityModifier / 100.0f;
    if (!std::isfinite(velocityFactor) ||
        velocityFactor <= 0.0f ||
        velocityFactor >= 2.0f)
    {
        return false;
    }

    output.bulletSpeed = initialSpeed * velocityFactor;
    output.bulletMassGrams = bulletMass;
    output.bulletDiameterMillimeters = bulletDiameter;
    output.ballisticCoefficient = ballisticCoefficient;

    return output.IsValid();
}

inline bool HandsInfo::update(const PlayerCache& playerCache)
{
    if (playerCache.isDead || playerCache.hasExfiled)
    {
        reset();
        cachedItem = 0;
        cachedIsWeapon = false;
        return false;
    }

    if (!Utils::valid_pointer(playerCache.P_HandsController))
    {
        reset();
        cachedItem = 0;
        cachedIsWeapon = false;
        return false;
    }

    uint64_t itemBase = 0;

    if (playerCache.isLocal ||
        playerCache.className.find("LocalPlayer") != std::string::npos ||
        playerCache.className.find("ClientPlayer") != std::string::npos)
    {
        itemBase = mem.Read<uint64_t>(
            playerCache.P_HandsController + sdk::ItemHandsController::Item
        );
    }
    else
    {
        itemBase = mem.Read<uint64_t>(
            playerCache.P_HandsController + sdk::ObservedPlayerHands::Item
        );
    }

    if (!Utils::valid_pointer(itemBase) || itemName == "Unknown")
    {
        reset();
        cachedItem = 0;
        cachedIsWeapon = false;
        return false;
    }

    bool isWeapon = cachedIsWeapon;

    // Only refresh item identity when the held item pointer changes
    bool itemChanged = (itemBase != cachedItem);

    if (itemChanged)
    {
        itemName.clear();
        ammoName.clear();

        chamberCount = 0;
        magazineCount = 0;

        cachedIsWeapon = false;
        cachedItemTemplate = 0;
        loadedAmmoTemplate = 0;
        weaponVersion = -1;
        ballistics = {};
        nextBallisticsRefresh = {};
        weaponVelocityModifier = 0.0f;
        weaponVelocityModifierValid = false;
        nextVelocityModifierRefresh = {};
        isWeapon = false;

        uint64_t itemTemp = 0;

        if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItem::Template, itemTemp) ||
            !Utils::valid_pointer(itemTemp))
        {
            itemName = "Unknown";
            cachedItem = itemBase;
            return true;
        }

        cachedItemTemplate = itemTemp;

        MongoID mongoId{};

        if (!mem.TryRead<MongoID>(itemTemp + sdk::ItemTemplate::_id, mongoId))
        {
            itemName = "Unknown";
            cachedItem = itemBase;
            return true;
        }

        std::string itemId = TrimEFT(mongoId.ReadString(mem, 64));

        std::string itemMarketName;

        if (!itemId.empty())
        {
            for (const auto& ml : marketList)
            {
                if (ml.bsgid != itemId)
                    continue;

                itemMarketName = ml.shortName;

                //Check if we have a weapon category
                const bool hasWeaponCategory =
                    std::find(ml.bsgCategory.begin(), ml.bsgCategory.end(), "Weapon") != ml.bsgCategory.end();

                if (hasWeaponCategory)
                {
                    isWeapon = true;
                    cachedIsWeapon = true;
                }

                break;
            }
        }

        if (!itemMarketName.empty())
        {
            itemName = itemMarketName;
        }
        else
        {
            uint64_t itemNamePointer = 0;

            if (mem.TryRead<uint64_t>(itemTemp + sdk::ItemTemplate::ShortName, itemNamePointer) &&
                Utils::valid_pointer(itemNamePointer))
            {
                std::string shortNameMem = TrimEFT(
                    mem.readUnityString(itemNamePointer, 32)
                );

                if (!shortNameMem.empty())
                    itemName = shortNameMem;
                else
                    itemName = "Unknown";
            }
            else
            {
                itemName = "Unknown";
            }

            if (itemName.find("nsv_utes") != std::string::npos)
            {
                itemName = "NSV Utyos";
            }
            else if (itemName.find("ags30_30") != std::string::npos)
            {
                itemName = "AGS-30";
                ammoName = "VOG-30";
            }
            else if (itemName.find("izhmash_rpk16") != std::string::npos)
            {
                itemName = "RPK-16";
            }
        }

        cachedItem = itemBase;
    }

    // Use cached weapon state after item identity refresh
    isWeapon = cachedIsWeapon;

    int currentWeaponVersion = weaponVersion;
    const bool versionRead = isWeapon &&
        mem.TryRead<int>(
            itemBase + sdk::LootItem::Version,
            currentWeaponVersion);
    const bool weaponVersionChanged =
        itemChanged ||
        (versionRead && currentWeaponVersion != weaponVersion);
    const auto ballisticsNow = std::chrono::steady_clock::now();
    const bool ballisticsRequested =
        playerCache.isLocal && aimGlobals::predictionEnabled;
    const bool ballisticsRetryDue =
        ballisticsRequested &&
        !ballistics.IsValid() &&
        (nextBallisticsRefresh ==
            std::chrono::steady_clock::time_point{} ||
            ballisticsNow >= nextBallisticsRefresh);
    const bool velocityModifierRefreshDue =
        ballisticsRequested &&
        (nextVelocityModifierRefresh ==
            std::chrono::steady_clock::time_point{} ||
            ballisticsNow >= nextVelocityModifierRefresh);

    if (isWeapon &&
        (itemChanged ||
            (playerCache.isLocal &&
                (weaponVersionChanged ||
                    ballisticsRetryDue ||
                    velocityModifierRefreshDue))))
    {
        uint64_t ammoTemplate = 0;

        int newChamberCount = chamberCount;
        int newMagazineCount = magazineCount;

        const bool gotAmmoTemplate = TryGetAmmoTemplateFromWeapon(
            itemBase,
            ammoTemplate,
            newChamberCount,
            newMagazineCount
        );

        
        chamberCount = newChamberCount;
        magazineCount = newMagazineCount;
        ammoName.clear();

        if (versionRead)
            weaponVersion = currentWeaponVersion;

        if (ballisticsRequested)
        {
            nextBallisticsRefresh =
                ballisticsNow + std::chrono::seconds(3);
            const bool ammoChanged =
                ammoTemplate != loadedAmmoTemplate;
            bool velocityModifierRefreshed = false;

            if (velocityModifierRefreshDue)
            {
                float refreshedModifier = 0.0f;

                if (TryReadWeaponVelocityModifier(
                    itemBase,
                    cachedItemTemplate,
                    refreshedModifier))
                {
                    weaponVelocityModifier = refreshedModifier;
                    weaponVelocityModifierValid = true;
                    velocityModifierRefreshed = true;
                    nextVelocityModifierRefresh =
                        ballisticsNow + std::chrono::seconds(10);
                }
                else
                {
                    nextVelocityModifierRefresh =
                        ballisticsNow + std::chrono::seconds(3);
                }
            }

            if (!Utils::valid_pointer(ammoTemplate) ||
                !weaponVelocityModifierValid)
            {
                loadedAmmoTemplate = 0;
                ballistics = {};
            }
            else if (ammoChanged ||
                velocityModifierRefreshed ||
                !ballistics.IsValid())
            {
                BallisticsInfo refreshedBallistics{};

                if (TryReadWeaponBallistics(
                    ammoTemplate,
                    weaponVelocityModifier,
                    refreshedBallistics))
                {
                    ballistics = refreshedBallistics;
                    loadedAmmoTemplate = ammoTemplate;
                }
                else
                {
                    loadedAmmoTemplate = 0;
                    ballistics = {};
                }
            }
        }

        
        if (gotAmmoTemplate || Utils::valid_pointer(ammoTemplate))
        { 

            MongoID ammoMongoId{};

            if (mem.TryRead<MongoID>(ammoTemplate + sdk::ItemTemplate::_id, ammoMongoId))
            {
                std::string ammoId = TrimEFT(ammoMongoId.ReadString(mem, 64));

                if (ammoId.empty())
                    return true;

                for (const auto& ml : marketList)
                {
                    if (ml.bsgid != ammoId)
                        continue;

                    ammoName = ml.shortName;
                    break;
                }
            }
        }
    }


    if (!isWeapon)
    {
        chamberCount = 0;
        magazineCount = 0;
        ammoName = "";
        loadedAmmoTemplate = 0;
        weaponVersion = -1;
        ballistics = {};
        nextBallisticsRefresh = {};
        weaponVelocityModifier = 0.0f;
        weaponVelocityModifierValid = false;
        nextVelocityModifierRefresh = {};
    }

    return true;
}

void Players::checkExfil()
{

    if (!mem.vHandle)
        return;

    std::lock_guard<std::mutex> lock(playerMutex);

    std::vector<PlayerCache>& cache = players.getCache();

    if (cache.empty())
        return;

    constexpr int MAX_REGISTERED_PLAYERS_SAFE = 512;

    const int registeredCount = mainGame.registeredPlayersCount;

    if (registeredCount <= 0 || registeredCount > MAX_REGISTERED_PLAYERS_SAFE)
    {
        LOGS.logError("[PLAYERS][EXFIL] Invalid registeredPlayersCount, skipping exfil check");
        return;
    }

    std::unordered_set<uint64_t> alivePlayers;
    alivePlayers.reserve(static_cast<size_t>(registeredCount));

    for (int i = 0; i < registeredCount; i++)
    {
        const uint64_t playerInstance = mainGame.player_buffer[i];

        if (!Utils::valid_pointer(playerInstance))
            continue;

        alivePlayers.insert(playerInstance);
    }

    if (alivePlayers.empty())
    {
        LOGS.logError("[PLAYERS][EXFIL] No valid registered players found, skipping exfil check");
        return;
    }

    const bool localGroupRosterProtectionActive = IsLocalGroupRosterProtectionActive();

    for (auto& cachedPlayer : cache)
    {
        if (cachedPlayer.isBTR)
            continue;

        if (IsProtectedLocalGroupMember(
                cachedPlayer,
                localGroupRosterProtectionActive))
        {
            cachedPlayer.isDead = false;
            cachedPlayer.hasExfiled = false;
            cachedPlayer.P_CorpseClass = 0;
        }

        if (cachedPlayer.isDead)
            continue;

        if (!Utils::valid_pointer(cachedPlayer.instance))
            continue;

        const bool stillRegistered =
            alivePlayers.find(cachedPlayer.instance) != alivePlayers.end();

        if (stillRegistered)
        {
            if (cachedPlayer.hasExfiled)
            {
                cachedPlayer.hasExfiled = false;
                LOGS.logWarn(
                    "[PLAYERS][EXFIL] Restored player after roster recovery: " +
                    cachedPlayer.name);
            }

            continue;
        }

        if (IsProtectedLocalGroupMember(cachedPlayer, localGroupRosterProtectionActive))
        {
            continue;
        }

        if (cachedPlayer.hasExfiled)
            continue;

        cachedPlayer.hasExfiled = true;

        if (cachedPlayer.isLocal)
        {
            LOGS.logInfo("[PLAYERS][EXFIL] Local player no longer registered");
        }
    }
}

uint64_t Players::getPlayerBoneMatrixPtr(const uint64_t instance)
{
    if (mainGame.onlineRaid)
        return mem.ReadChain(instance, { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 });
    else
        return mem.ReadChain(instance, { sdk::Player::_playerBody, 0x30, 0x30, 0x10 });
}

uint64_t Players::getPlayerHealthControllerPtr(const uint64_t instance)
{
    if (mainGame.onlineRaid)
    {
        return mem.ReadChain(instance, { sdk::ObservedPlayerView::ObservedPlayerController, sdk::ObservedPlayerController::HealthController });
    }
    else
        return instance; // offline corpse ptr in eft.player
}
