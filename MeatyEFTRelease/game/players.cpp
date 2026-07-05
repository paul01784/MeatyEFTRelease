#include "../app/includes.h"
#include "headers/players.h"
#include "../app/globals.h"

#include "../memory/Memory.h"

#include "headers/maingame.h"
#include "../app/debug.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"
#include "headers/tarkovdevquery.h"
#include <cmath>
#include "headers/questManager.h"
#include "../app/market.h"
#include "headers/loot.h"
#include "headers/wishlist.h"
#include "headers/dogtag.h"
#include "../app/DogTagAPI.h"
#include <chrono>
#include <algorithm>

std::mutex playerMutex;
Players players;

bool Players::groupIDSet = false;

static glm::vec3 GetBestPlayerBasePosition(const PlayerCache& cachePlayer);

namespace
{
    static void EnsureTransformCacheShape(PlayerCache& player);

    static bool HasMinimalBonePointers(const PlayerCache& player);

    static bool IsMinimalBoneSlot(int slot);

    static bool IsUsableBonePosition(const glm::vec3& position);
}

static std::string CleanString(std::string str)
{
    const size_t nullPos = str.find('\0');
    if (nullPos != std::string::npos)
        str.erase(nullPos);

    while (!str.empty() && (str.back() == ' ' || str.back() == '\r' || str.back() == '\n' || str.back() == '\t'))
        str.pop_back();

    return str;
}

static std::string ReadString(uint64_t fieldAddr)
{
    if (!Utils::valid_pointer(fieldAddr))
        return "";

    uint64_t namePtr = mem.Read<uint64_t>(fieldAddr);
    if (!Utils::valid_pointer(namePtr))
        return "";

    int len = mem.Read<int>(static_cast<SIZE_T>(namePtr) + 0x10);
    if (len <= 0 || len > 256)
        return "";

    return CleanString(mem.readUnicodeString(namePtr + 0x14, len));
}

inline bool isValidBoneVector(const glm::vec3& v)
{
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
        return false;

    if (std::abs(v.x) < 0.001f &&
        std::abs(v.y) < 0.001f &&
        std::abs(v.z) < 0.001f)
        return false;

    constexpr float LIMIT = 100000.f;
    if (std::abs(v.x) > LIMIT || std::abs(v.y) > LIMIT || std::abs(v.z) > LIMIT)
        return false;

    return true;
}



inline std::string SideToString(EPlayerSide side)
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

static int ConvertXpToLevel(int xp)
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
    players.groupIDSet = false;

    LOGS.logInfo("[PLAYER][CACHE] Data cleared");
}

void Players::softRestart()
{
    std::lock_guard<std::mutex> lock(playerMutex);

    players.groupIDSet = false;
    mainGame.localGroupId = "";
    this->playerCache.clear();
    this->playerGroups.clear();
}

std::vector<PlayerCache>& Players::getCache() {
    return playerCache;
}

std::vector<PlayerCache> Players::getCacheSnapshot()
{
    std::lock_guard<std::mutex> lock(playerMutex);
    return playerCache;
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
                    }
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
                    }
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
                }
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
    constexpr size_t kMaxCombinedMatrixElements = 12288;

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

    struct ScatterGuard
    {
        Memory& mem;
        VMMDLL_SCATTER_HANDLE handle{ nullptr };

        explicit ScatterGuard(Memory& memory)
            : mem(memory),
            handle(memory.CreateScatterHandle())
        {
        }

        ~ScatterGuard()
        {
            if (!handle)
                return;

            try
            {
                mem.CloseScatterHandle(handle);
            }
            catch (...)
            {
            }
        }
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

    static bool HasUsableTransformMetadata(
        const LiveBoneRead& read)
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
            ScatterGuard scatter(memory);

            if (!scatter.handle)
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

                if (!memory.AddScatterReadRequest(
                    scatter.handle,
                    read.boneTransform +
                    UnityOffsets::TransformInternal_TransformAccessOffset,
                    &read.access,
                    sizeof(TransformAccessReadOnly)))
                {
                    read.cache.valid = false;
                    continue;
                }

                queuedMetadataReads = true;
            }

            if (queuedMetadataReads)
            {
                const bool executed =
                    memory.ExecuteReadScatter(scatter.handle);

                // Your ExecuteReadScatter consumes/clears the handle.
                scatter.handle = nullptr;

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
            ScatterGuard scatter(memory);

            if (!scatter.handle)
                return;

            for (LiveBoneRead& read : reads)
            {
                if (!read.needsHierarchy)
                    continue;

                const bool matrixQueued =
                    memory.AddScatterReadRequest(
                        scatter.handle,
                        read.cache.transformData +
                        UnityOffsets::Hierarchy_VerticesOffset,
                        &read.cache.transformArray,
                        sizeof(uint64_t)
                    );

                const bool indicesQueued =
                    memory.AddScatterReadRequest(
                        scatter.handle,
                        read.cache.transformData +
                        UnityOffsets::Hierarchy_IndicesOffset,
                        &read.cache.transformIndices,
                        sizeof(uint64_t)
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
                const bool executed =
                    memory.ExecuteReadScatter(scatter.handle);

                scatter.handle = nullptr;

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
        std::vector<LiveBoneRead*> positionReads;
        positionReads.reserve(reads.size());

        for (LiveBoneRead& read : reads)
        {
            if (!HasUsableTransformMetadata(read))
                continue;

            const size_t matrixCount =
                static_cast<size_t>(
                    read.cache.transformIndex + 1
                    );

            if (matrixCount == 0 ||
                matrixCount > kMaxTransformChain ||
                matrixCount > kMaxCombinedMatrixElements)
            {
                read.cache.valid = false;
                continue;
            }

            read.count = static_cast<int>(matrixCount);
            positionReads.emplace_back(&read);
        }

        if (positionReads.empty())
            return;

        thread_local std::vector<Matrix34> matrices;
        thread_local std::vector<int32_t> indices;

        size_t start = 0;

        // Split only if all required transform-chain matrices cannot fit in one
        // scatter batch. This does not skip any player.
        while (start < positionReads.size())
        {
            size_t end = start;
            size_t totalMatrixElements = 0;

            while (end < positionReads.size())
            {
                const size_t matrixCount =
                    static_cast<size_t>(
                        positionReads[end]->count
                        );

                if (totalMatrixElements + matrixCount >
                    kMaxCombinedMatrixElements)
                {
                    break;
                }

                totalMatrixElements += matrixCount;
                ++end;
            }

            if (end == start)
            {
                positionReads[start]->cache.valid = false;
                ++start;
                continue;
            }

            matrices.resize(totalMatrixElements);
            indices.resize(totalMatrixElements);

            ScatterGuard scatter(memory);

            if (!scatter.handle)
            {
                for (size_t i = start; i < end; ++i)
                    positionReads[i]->cache.valid = false;

                return;
            }

            bool queuedPositionReads = false;
            size_t cursor = 0;

            for (size_t i = start; i < end; ++i)
            {
                LiveBoneRead& read = *positionReads[i];

                const size_t matrixCount =
                    static_cast<size_t>(read.count);

                read.bufOffset = cursor;
                cursor += matrixCount;

                read.matrixQueued =
                    memory.AddScatterReadRequest(
                        scatter.handle,
                        read.cache.transformArray,
                        matrices.data() + read.bufOffset,
                        static_cast<SIZE_T>(
                            sizeof(Matrix34) * matrixCount
                            )
                    );

                read.indicesQueued =
                    memory.AddScatterReadRequest(
                        scatter.handle,
                        read.cache.transformIndices,
                        indices.data() + read.bufOffset,
                        static_cast<SIZE_T>(
                            sizeof(int32_t) * matrixCount
                            )
                    );

                if (!read.matrixQueued ||
                    !read.indicesQueued)
                {
                    read.cache.valid = false;
                    continue;
                }

                queuedPositionReads = true;
            }

            if (!queuedPositionReads)
            {
                start = end;
                continue;
            }

            const bool executed =
                memory.ExecuteReadScatter(scatter.handle);

            scatter.handle = nullptr;

            if (!executed)
            {
                for (size_t i = start; i < end; ++i)
                    positionReads[i]->cache.valid = false;

                start = end;
                continue;
            }

            for (size_t i = start; i < end; ++i)
            {
                LiveBoneRead& read = *positionReads[i];

                if (!read.matrixQueued ||
                    !read.indicesQueued ||
                    read.count <= 0)
                {
                    continue;
                }

                const glm::vec3 position =
                    computeTransformPosition(
                        matrices.data() + read.bufOffset,
                        indices.data() + read.bufOffset,
                        read.cache.transformIndex,
                        read.count
                    );

                if (!IsUsableBonePosition(position))
                {
                    read.cache.valid = false;
                    continue;
                }

                read.position = position;
                read.hasPosition = true;
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
            bool hadMinimalBoneRead = false;
            bool gotMinimalBonePosition = false;
        };

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
                state.hadMinimalBoneRead = true;

                if (read.hasPosition)
                    state.gotMinimalBonePosition = true;
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

            // re-resolve bone pointers
            // pointers disappeared or all read positions were invalid
            if (!minimalPointersValid ||
                (state.hadMinimalBoneRead &&
                    !state.gotMinimalBonePosition))
            {
                player->bonePointersNeedResolve = true;
                continue;
            }

            if (!state.gotMinimalBonePosition)
                continue;

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
}


void Players::boneTask()
{
    try
    {
        if (!mem.IsDmaOperational())
            return;

        if (!Utils::valid_pointer(mainGame.localPlayerPtr))
            return;

        const float drawPlayerDistance =
            static_cast<float>(espGlobals::drawPlayerDist);

        struct PendingBoneScan
        {
            BonePlayerSnapshot snapshot{};
            bool readFullBoneList = false;
        };

        std::vector<PendingBoneScan> pendingScans;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            std::vector<PlayerCache>& cache =
                players.getCache();

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

                const bool minimalPointersMissing = !HasMinimalBonePointers(player);

                // setup after adding a player
                // or recovery after all Base/LFoot/RFoot positions failed
                if (player.bonePointersNeedResolve ||
                    minimalPointersMissing)
                {
                    getBonePtrs(player, true);
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

                // Initial distance is 0 until Base/LFoot/RFoot is done
                pending.readFullBoneList = !player.isLocal && player.distance > 0 && player.distance <= drawPlayerDistance;

                pendingScans.emplace_back(std::move(pending));
            }
        }

        if (pendingScans.empty())
            return;

        std::vector<LiveBoneRead> reads;

        for (const PendingBoneScan& pending : pendingScans)
        {
            AppendPlayerBoneReads(
                pending.snapshot,
                BoneReadKind::Normal,
                pending.readFullBoneList,
                reads
            );
        }

        if (reads.empty())
            return;

        BatchReadBoneWorldPositions(mem, reads);

        ApplyBoneResults(reads);
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
        pendingNewEntities.reserve(registeredPlayers.size());

        for (const uint64_t currentPlayer : registeredPlayers)
        {
            if (existingInstances.contains(currentPlayer))
                continue;

            const bool isLocal =
                currentPlayer == mainGame.localPlayerPtr;

            auto builtEntity =
                buildEntity(currentPlayer, isLocal);

            if (!builtEntity.has_value())
                continue;

            pendingNewEntities.emplace_back(
                std::move(*builtEntity)
            );
        }

        std::vector<uint64_t> addedPlayerInstances;

        {
            std::lock_guard<std::mutex> lock(playerMutex);

            for (PlayerCache& entity : pendingNewEntities)
            {
                const auto found = std::find_if(
                    playerCache.begin(),
                    playerCache.end(),
                    [&](const PlayerCache& cachedPlayer)
                    {
                        return cachedPlayer.instance ==
                            entity.instance;
                    }
                );

                if (found != playerCache.end())
                    continue;

                std::ostringstream ss;

                ss << "[PLAYERS][INIT] Adding player : 0x"
                    << std::hex << entity.instance
                    << " className : " << entity.className
                    << " name : " << entity.name;

                LOGS.logInfo(ss.str());

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
    if (containsIgnoreCase(voiceLine, "black_division"))       return { "Black Division", PlayerType::AIRaider };
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
        newEntity.className = ReadName(instance, 64);
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

        if (!TryReadChain(
            newEntity.P_ObservedPlayerController,
            {
                sdk::ObservedPlayerController::MovementController,
                sdk::ObservedMovementController::
                ObservedPlayerStateContext
            },
            newEntity.P_MovementContext))
        {
            AddFailure(failed, "MovementContext");
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

void Players::updateEntity()
{
    if (!mem.vHandle)
        return;

    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    static constexpr Milliseconds kCorpseReadInterval{ 1000 };
    static constexpr Milliseconds kHealthReadInterval{ 2000 };
    static constexpr Milliseconds kHandsReadInterval{ 2000 };
    static constexpr Milliseconds kHeldItemRefreshInterval{ 3000 };
    static constexpr Milliseconds kFailedReadRetryInterval{ 2500 };

    const Clock::time_point now = Clock::now();

    struct ScheduledRead
    {
        Clock::time_point* nextRead = nullptr;
        Milliseconds interval{};
    };

    std::vector<ScheduledRead> scheduledReads;

    // Fast scatter
    {
        std::lock_guard<std::mutex> lock(playerMutex);

        std::vector<PlayerCache>& cache =
            players.getCache();

        if (cache.empty())
            return;

        auto updateHandle = mem.CreateScatterHandle();

        if (!updateHandle)
        {
            LOGS.logError(
                "[PLAYERS][UPDATE] Failed to create scatter handle"
            );
            return;
        }

        bool queuedAnything = false;

        for (PlayerCache& cachePlayer : cache)
        {
            if (!Utils::valid_pointer(cachePlayer.instance))
                continue;

            if (cachePlayer.isBTR)
            {
                if (mem.AddScatterReadRequest(
                    updateHandle,
                    cachePlayer.btrView +
                    sdk::BTRView::previousPosition,
                    &cachePlayer.location,
                    sizeof(glm::vec3)))
                {
                    queuedAnything = true;
                }

                continue;
            }

            if (cachePlayer.isDead || cachePlayer.hasExfiled)
            {
                continue;
            }

            const bool isOfflinePlayer =
                cachePlayer.className == "LocalPlayer" ||
                cachePlayer.className == "ClientPlayer";

            if (mem.AddScatterReadRequest(
                updateHandle,
                cachePlayer.P_RotationAddress,
                &cachePlayer.rotationRAW,
                sizeof(glm::vec2)))
            {
                queuedAnything = true;
            }

            const bool corpseDue = cachePlayer.nextCorpseRead == Clock::time_point{} || now >= cachePlayer.nextCorpseRead;

            if (corpseDue)
            {
                cachePlayer.P_CorpseClass = 0;

                if (mem.AddScatterReadRequest(
                    updateHandle,
                    cachePlayer.P_CorpseAddr,
                    &cachePlayer.P_CorpseClass,
                    sizeof(uint64_t)))
                {
                    queuedAnything = true;

                    scheduledReads.push_back({
                        &cachePlayer.nextCorpseRead,
                        kCorpseReadInterval
                        });
                }
                else
                {
                    cachePlayer.nextCorpseRead = now + kFailedReadRetryInterval;
                }
            }

            const bool handsDue = cachePlayer.nextHandsControllerRead == Clock::time_point{} || now >= cachePlayer.nextHandsControllerRead;

            if (handsDue)
            {
                if (mem.AddScatterReadRequest(
                    updateHandle,
                    cachePlayer.P_HandsControllerAddr,
                    &cachePlayer.P_HandsController,
                    sizeof(uint64_t)))
                {
                    queuedAnything = true;

                    scheduledReads.push_back({
                        &cachePlayer.nextHandsControllerRead,
                        kHandsReadInterval
                        });
                }
                else
                {
                    cachePlayer.nextHandsControllerRead = now + kFailedReadRetryInterval;
                }
            }

            if (isOfflinePlayer)
            {
                cachePlayer.isAiming = false;

                if (mem.AddScatterReadRequest(
                    updateHandle,
                    cachePlayer.P_PWA +
                    sdk::ProceduralWeaponAnimation::_isAiming,
                    &cachePlayer.isAiming,
                    sizeof(bool)))
                {
                    queuedAnything = true;
                }
            }
            else
            {
                const bool healthDue = cachePlayer.nextHealthRead == Clock::time_point{} || now >= cachePlayer.nextHealthRead;

                if (healthDue)
                {
                    if (mem.AddScatterReadRequest(
                        updateHandle,
                        cachePlayer.P_ObservedHealthController +
                        sdk::ObservedHealthController::HealthStatus,
                        &cachePlayer.healthETAG,
                        sizeof(ETagStatus)))
                    {
                        queuedAnything = true;

                        scheduledReads.push_back({
                            &cachePlayer.nextHealthRead,
                            kHealthReadInterval
                            });
                    }
                    else
                    {
                        cachePlayer.nextHealthRead = now + kFailedReadRetryInterval;
                    }
                }
            }
        }

        if (queuedAnything)
        {
            const bool executed = mem.ExecuteReadScatter(updateHandle);

            updateHandle = {};

            if (!executed)
            {
                for (const ScheduledRead& read : scheduledReads)
                {
                    if (read.nextRead)
                    {
                        *read.nextRead = now + kFailedReadRetryInterval;
                    }
                }

                LOGS.logError(
                    "[PLAYERS][UPDATE] Player scatter execute failed"
                );

                return;
            }

            for (const ScheduledRead& read : scheduledReads)
            {
                if (read.nextRead)
                    *read.nextRead = now + read.interval;
            }
        }
        else
        {
            mem.CloseScatterHandle(updateHandle);
            updateHandle = {};
        }
    }

    auto ApplyPlayerColour = [&](PlayerCache& cachePlayer)
        {
            cachePlayer.colour = { 1, 1, 1, 1 };

            if (cachePlayer.isDead)
            {
                cachePlayer.colour =
                    coloursGlobals::playerCorpse;
                return;
            }

            if (cachePlayer.isAi &&
                !cachePlayer.isPlayerScav &&
                !cachePlayer.isPlayer)
            {
                cachePlayer.colour =
                    coloursGlobals::playerAI;
            }

            if (cachePlayer.isPlayerScav &&
                !cachePlayer.isAi &&
                cachePlayer.isPlayer)
            {
                cachePlayer.colour =
                    coloursGlobals::playerScav;
            }

            if (cachePlayer.isBoss)
            {
                cachePlayer.colour =
                    coloursGlobals::playerBoss;
            }

            if (cachePlayer.isPlayer &&
                !cachePlayer.isPlayerScav &&
                !cachePlayer.isAi)
            {
                cachePlayer.colour =
                    coloursGlobals::playerPMC;
            }

            if (cachePlayer.isWatched)
            {
                cachePlayer.colour =
                    coloursGlobals::playerWatched;
            }

            if (!mainGame.localGroupId.empty() &&
                cachePlayer.groupId ==
                mainGame.localGroupId)
            {
                cachePlayer.colour =
                    coloursGlobals::playerFriendly;
            }

            if (cachePlayer.isLocal &&
                Utils::valid_pointer(cachePlayer.instance) &&
                mainGame.localPlayerPtr ==
                cachePlayer.instance)
            {
                cachePlayer.colour =
                    coloursGlobals::playerLocal;
            }
        };

    // Apply updates
    {
        std::lock_guard<std::mutex> lock(playerMutex);

        std::vector<PlayerCache>& cache =
            players.getCache();

        for (PlayerCache& cachePlayer : cache)
        {
            if (cachePlayer.isBTR)
            {
                cachePlayer.colour = coloursGlobals::aiBTR;

                cachePlayer.distance = getDistance(
                    cachePlayer.location,
                    mainGame.localLocation
                );

                continue;
            }

            if (cachePlayer.isDead ||
                cachePlayer.hasExfiled)
            {
                cachePlayer.distance = getDistance(
                    cachePlayer.location,
                    mainGame.localLocation
                );

                ApplyPlayerColour(cachePlayer);
                continue;
            }

            if (Utils::valid_pointer(
                cachePlayer.P_CorpseClass))
            {
                cachePlayer.isDead = true;
                cachePlayer.instance = 0;

                cachePlayer.distance = getDistance(
                    cachePlayer.location,
                    mainGame.localLocation
                );

                ApplyPlayerColour(cachePlayer);
                continue;
            }

            if (!Utils::valid_pointer(cachePlayer.instance))
                continue;

            // The bone task updates Base/LFoot/RFoot
            const glm::vec3 newLocation = GetBestPlayerBasePosition(cachePlayer);

            if (newLocation.x != 0.0f ||
                newLocation.y != 0.0f ||
                newLocation.z != 0.0f)
            {
                cachePlayer.location = newLocation;
            }

            if (cachePlayer.isLocal)
                mainGame.localLocation =
                cachePlayer.location;

            cachePlayer.distance = getDistance(
                cachePlayer.location,
                mainGame.localLocation
            );

            // Raw rotation was read in the scatter phase.
            try
            {
                cachePlayer.rotation =
                    Utils::Player::Rotation::
                    correctRotation2d(
                        cachePlayer.rotationRAW
                    );
            }
            catch (...)
            {
                cachePlayer.rotation = {};

                LOGS.logError(
                    "[PLAYERS][UPDATE] Rotation correction failed"
                );
            }

            // Initial update as soon as hands appear, update every # seconds
            try
            {
                if (!Utils::valid_pointer(
                    cachePlayer.P_HandsController))
                {
                    cachePlayer.itemInHand.clear();

                    cachePlayer.lastHeldItemHandsController = 0;
                    cachePlayer.nextHeldItemRefresh = {};
                }
                else
                {
                    const bool handsChanged =
                        cachePlayer.lastHeldItemHandsController !=
                        cachePlayer.P_HandsController;

                    if (handsChanged)
                    {
                        cachePlayer.lastHeldItemHandsController =
                            cachePlayer.P_HandsController;

                        cachePlayer.nextHeldItemRefresh = now;
                    }

                    const bool heldItemDue =
                        cachePlayer.nextHeldItemRefresh ==
                        Clock::time_point{} ||
                        now >= cachePlayer.nextHeldItemRefresh;

                    if (heldItemDue)
                    {
                        cachePlayer.observedHandsInfo.update(
                            cachePlayer
                        );

                        cachePlayer.nextHeldItemRefresh =
                            now + kHeldItemRefreshInterval;
                    }
                }
            }
            catch (...)
            {
                cachePlayer.itemInHand.clear();

                cachePlayer.nextHeldItemRefresh = now + kFailedReadRetryInterval;

                LOGS.logError(
                    "[PLAYERS][UPDATE] heldItemName failed"
                );
            }

            if (cachePlayer.isPlayer &&
                !cachePlayer.profileId.empty())
            {
                const auto profileNow = Clock::now();

                if (!cachePlayer.foundDogTagCache &&
                    cachePlayer.name.contains("PMC") &&
                    profileNow -
                    cachePlayer.lastDogTagLookup >
                    std::chrono::seconds(5))
                {
                    cachePlayer.lastDogTagLookup = profileNow;

                    try
                    {
                        auto result =
                            g_dogTagCache.GetByProfileId(
                                cachePlayer.profileId
                            );

                        if (result.has_value())
                        {
                            if (!result->nickname.empty())
                            {
                                cachePlayer.name = result->nickname;
                            }

                            cachePlayer.accountId = result->accountId;

                            cachePlayer.foundDogTagCache = true;
                        }
                    }
                    catch (...)
                    {
                        LOGS.logError(
                            "[PLAYERS][UPDATE] Dogtag cache lookup failed"
                        );
                    }
                }

                if (!cachePlayer.hasProfileData &&
                    !cachePlayer.accountId.empty() &&
                    radarGlobals::getPlayerStats == TRUE &&
                    !cachePlayer.triedprofileonce)
                {
                    cachePlayer.triedprofileonce = true;

                    try
                    {
                        auto profile =
                            TarkovDevProfileClient::
                            GetProfileForAccountId(
                                cachePlayer.accountId
                            );

                        if (profile)
                        {
                            cachePlayer.profileStats = *profile;
                            cachePlayer.hasProfileData = true;

                            cachePlayer.DT_lvl =
                                ConvertXpToLevel(
                                    profile->experience
                                );

                            cachePlayer.kd =
                                CalculateKD(
                                    profile->Kills,
                                    profile->deathsPMC
                                );

                            cachePlayer.pkd =
                                CalculatePKD(
                                    profile->killedPMC,
                                    profile->deathsPMC
                                );

                            cachePlayer.hours =
                                profile->hoursPlayed;
                        }
                    }
                    catch (...)
                    {
                        LOGS.logError(
                            "[PLAYERS][UPDATE] Tarkov profile lookup failed"
                        );
                    }
                }
            }

            ApplyPlayerColour(cachePlayer);

            if (cachePlayer.isLocal &&
                Utils::valid_pointer(cachePlayer.instance) &&
                mainGame.localPlayerPtr ==
                cachePlayer.instance)
            {
                mainGame.localLocation =
                    cachePlayer.location;

                mainGame.localRotation =
                    cachePlayer.rotation;

                mainGame.localGroupId =
                    cachePlayer.groupId;

                mainGame.localPlayerHands =
                    cachePlayer.P_HandsController;

                mainGame.localIsScoped =
                    cachePlayer.isAiming;

                mainGame.localPlayerPWA =
                    cachePlayer.P_PWA;

                cachePlayer.colour =
                    coloursGlobals::playerLocal;
            }
        }
    }
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

    // Connected proximity grouping:
    // A <-> B within 30m and B <-> C within 30m makes A/B/C one group.
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
            "proximity_" + std::to_string(nextGroupNumber++)
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

        // Solo players deliberately receive an empty ID.
        assignments.emplace(snapshot[i].instance, groupId);

        if (i == localSnapshotIndex)
            resolvedLocalGroupId = groupId;
    }

    {
        std::lock_guard<std::mutex> lock(playerMutex);

        auto& cache = players.getCache();

        // If the player list changed between snapshot and commit, do not
        // finalise a partial/incorrect one-time grouping pass.
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

    // Scan is complete even if every player is solo.
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
    static constexpr size_t kMaxEquipmentScanPerPass = 2;

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
            auto handle = mem.CreateScatterHandle();

            if (!handle)
                return false;

            bool queuedAnything = false;

            try
            {
                queuedAnything = queueReads(handle);
            }
            catch (...)
            {
                mem.CloseScatterHandle(handle);
                return false;
            }

            if (!queuedAnything)
            {
                mem.CloseScatterHandle(handle);
                return true;
            }

            return mem.ExecuteReadScatter(handle);
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
            initReadSuccess = executeScatter([&](auto handle)
                {
                    bool queued = false;

                    for (InitJob& job : initJobs)
                    {
                        if (!Utils::valid_pointer(
                            job.inventoryControllerAddr))
                        {
                            continue;
                        }

                        if (mem.AddScatterReadRequest(
                            handle,
                            job.inventoryControllerAddr,
                            &job.inventoryController,
                            sizeof(job.inventoryController)))
                        {
                            queued = true;
                        }
                    }

                    return queued;
                });

            // Controller -> Inventory.
            if (initReadSuccess)
            {
                initReadSuccess = executeScatter([&](auto handle)
                    {
                        bool queued = false;

                        for (InitJob& job : initJobs)
                        {
                            if (!Utils::valid_pointer(
                                job.inventoryController))
                            {
                                continue;
                            }

                            if (mem.AddScatterReadRequest(
                                handle,
                                job.inventoryController + sdk::InventoryController::Inventory,
                                &job.inventory,
                                sizeof(job.inventory)))
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
                initReadSuccess = executeScatter([&](auto handle)
                    {
                        bool queued = false;

                        for (InitJob& job : initJobs)
                        {
                            if (!Utils::valid_pointer(job.inventory))
                                continue;

                            if (mem.AddScatterReadRequest(
                                handle,
                                job.inventory + sdk::Inventory::Equipment,
                                &job.equipment,
                                sizeof(job.equipment)))
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
                initReadSuccess = executeScatter([&](auto handle)
                    {
                        bool queued = false;

                        for (InitJob& job : initJobs)
                        {
                            if (!Utils::valid_pointer(job.equipment))
                                continue;

                            if (mem.AddScatterReadRequest(
                                handle,
                                job.equipment + sdk::InventoryEquipment::_cachedSlots,
                                &job.slotsPtr,
                                sizeof(job.slotsPtr)))
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
                    UnityArray<uint64_t> slotsArray(job.slotsPtr);

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
            scanReadSuccess = executeScatter([&](auto handle)
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

                        if (mem.AddScatterReadRequest(
                            handle,
                            slot.addr +
                            sdk::Slot::ContainedItem,
                            &read.containedItem,
                            sizeof(read.containedItem)))
                        {
                            queued = true;
                        }
                    }

                    return queued;
                });

            if (scanReadSuccess)
            {
                scanReadSuccess = executeScatter([&](auto handle)
                    {
                        bool queued = false;

                        for (SlotRead& read : slotReads)
                        {
                            if (!Utils::valid_pointer(
                                read.containedItem))
                            {
                                continue;
                            }

                            if (mem.AddScatterReadRequest(
                                handle,
                                read.containedItem +
                                sdk::LootItem::Template,
                                &read.itemTemplate,
                                sizeof(read.itemTemplate)))
                            {
                                queued = true;
                            }
                        }

                        return queued;
                    });
            }

            if (scanReadSuccess)
            {
                scanReadSuccess = executeScatter([&](auto handle)
                    {
                        bool queued = false;

                        for (SlotRead& read : slotReads)
                        {
                            if (!Utils::valid_pointer(
                                read.itemTemplate))
                            {
                                continue;
                            }

                            if (mem.AddScatterReadRequest(
                                handle,
                                read.itemTemplate +
                                sdk::ItemTemplate::_id,
                                &read.mongoId,
                                sizeof(read.mongoId)))
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
                                ReadString(profileIdPtr);

                            if (!readString.empty())
                            {
                                result.hasProfileUpdate = true;
                                result.profileId = readString;

                                LOGS.logInfo(
                                    "[PLAYER] Set PID to Player : ",
                                    result.profileId
                                );

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
                for (const auto& questId : masterItems)
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
                slot.price > lootGlobals::valueLootFrom)
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

    UnityArray<Chamber> chambers(chambersPtr);

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
        UnityArray<Chamber> magChambers(magChambersPtr);

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

    UnityList<uint64_t> magStack(magStackPtr);

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
        isWeapon = false;

        uint64_t itemTemp = 0;

        if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItem::Template, itemTemp) ||
            !Utils::valid_pointer(itemTemp))
        {
            itemName = "Unknown";
            cachedItem = itemBase;
            return true;
        }

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

    if (isWeapon && (playerCache.isLocal || itemChanged))
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

        
        if (newMagazineCount > 0 || newChamberCount > 0)
        {
            chamberCount = newChamberCount;
            magazineCount = newMagazineCount;
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

    for (auto& cachedPlayer : cache)
    {
        if (cachedPlayer.isBTR)
            continue;

        if (cachedPlayer.isDead || cachedPlayer.hasExfiled)
            continue;

        if (!Utils::valid_pointer(cachedPlayer.instance))
            continue;

        const bool stillRegistered =
            alivePlayers.find(cachedPlayer.instance) != alivePlayers.end();

        if (!stillRegistered)
        {
            cachedPlayer.hasExfiled = true;
            cachedPlayer.instance = 0x0; // avoid unnecessary future reads

            if (cachedPlayer.isLocal)
            {
                LOGS.logInfo("[PLAYERS][EXFIL] Local player no longer registered");
            }
            else
            {
                std::ostringstream ss;
                ss << "[PLAYERS][EXFIL] Player exfiled/removed: "
                    << cachedPlayer.name
                    << " old instance: 0x"
                    << std::hex << cachedPlayer.instance;

                LOGS.logInfo(ss.str());
            }
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