#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerLookup.h"
#include "PlayerClassifier.h"
#include "PlayerPosition.h"

#include "../../../Web/MeatyAPI/DogTagAPI.h"
#include "../../../UI/debug.h"
#include "../../../UI/globals.h"
#include "../../Features/Visibility/AtlasVisibility.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "DogTagCache.h"
#include "../MainGame.h"
#include "../../Unity/UnityContainers.h"
#include "../../Unity/UnityOffsets.h"
#include "../../../Core/Utilities.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <immintrin.h>
#include <unordered_map>

namespace
{
    void EnsureTransformCacheShape(Player& player);
    bool HasMinimalBonePointers(const Player& player);
    bool IsMinimalBoneSlot(int slot);
    bool IsUsableBonePosition(const glm::vec3& position);
}

bool RegisteredPlayers::getBonePtrs(Player& player, bool forceResolve)
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
            PlayerClassifier::get(player).tryResolveBoneMatrix(player, resolvedMatrixPtr);
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

    const bool matrixChanged = player.playerBoneMatrixPtr != resolvedMatrixPtr;

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

void RegisteredPlayers::readDogTagComponent(Player& player, bool force)
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

    static bool HasValidMinimalBonePose(const Player& player)
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
        Player& player)
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
        const Player& player)
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

        std::vector<Player>& cache =
            registeredPlayers.getCache();

        for (const LiveBoneRead& read : reads)
        {
            Player* player =
                PlayerLookup::findByInstance(
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
            Player* player =
                PlayerLookup::findByInstance(
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

            player->location = PlayerPosition::getBestBasePosition(*player);

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

        if (atlasVisibilityGlobals::enabled)
        {
            QueueBone(static_cast<int>(boneListIndexes::Head));

            if (!player.isLocal)
            {
                QueueBone(static_cast<int>(boneListIndexes::Neck));
                QueueBone(static_cast<int>(boneListIndexes::Pelvis));
            }
        }

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


void RegisteredPlayers::boneTask()
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

        const float drawPlayerDistance = static_cast<float>(espGlobals::getMaximumPlayerDrawDistance());
        const bool closestFireportBoneEnabled =
            aimGlobals::aimEnabled &&
            aimGlobals::aimReference == AimReference::Fireport &&
            aimGlobals::aimClosestBoneToFireport;
        const float fullSkeletonDistance = closestFireportBoneEnabled
            ? (std::max)(drawPlayerDistance, static_cast<float>(aimGlobals::aimDistance))
            : drawPlayerDistance;
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

        const auto IsInsideSkeletonRefreshBounds =
            [&](const Player& player) -> bool
            {
                if (!projection || !projection->valid)
                    return false;

                glm::vec2 screenPosition{};

                if (!Utils::Camera::world_to_screen(PlayerPosition::getBestBasePosition(player), &screenPosition, *projection))
                {
                    return false;
                }

                constexpr float kScreenBoundsExtension = 0.12f;

                const float horizontalMargin =
                    espGlobals::gameRes.x * kScreenBoundsExtension;
                const float verticalMargin =
                    espGlobals::gameRes.y * kScreenBoundsExtension;

                return
                    screenPosition.x >= -horizontalMargin &&
                    screenPosition.y >= -verticalMargin &&
                    screenPosition.x <= espGlobals::gameRes.x + horizontalMargin &&
                    screenPosition.y <= espGlobals::gameRes.y + verticalMargin;
            };

        struct PendingBoneScan
        {
            BonePlayerSnapshot snapshot{};
            bool readFullBoneList = false;
        };

        struct PendingBoneResolve
        {
            uint64_t instance{};
            Player workingCopy{};
        };

        std::vector<PendingBoneResolve> pendingResolves;

        
        if (runFullBonePass)
        {
            std::lock_guard<std::mutex> lock(playerMutex);
            std::vector<Player>& cache = registeredPlayers.getCache();

            if (!cache.empty())
            {
                const size_t start = boneResolveCursor % cache.size();
                size_t inspected = 0;

                while (inspected < cache.size() &&
                    pendingResolves.size() < kMaxBonePointerResolvesPerPass)
                {
                    const size_t index = (start + inspected) % cache.size();
                    Player& player = cache[index];
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
            std::vector<Player>& cache = registeredPlayers.getCache();

            for (PendingBoneResolve& resolve : pendingResolves)
            {
                Player* player = PlayerLookup::findByInstance(cache, resolve.instance);

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

            std::vector<Player>& cache = registeredPlayers.getCache();

            pendingScans.reserve(cache.size());

            for (Player& player : cache)
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
                // skeleton within a small boundary beyond the viewport.
                pending.readFullBoneList =
                    runFullBonePass &&
                    (espGlobals::drawSkeletons || closestFireportBoneEnabled) &&
                    !player.isLocal &&
                    player.distance > 0.0f &&
                    player.distance <= fullSkeletonDistance + kFullBoneUpdateDistanceMargin &&
                    IsInsideSkeletonRefreshBounds(player);

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

            std::vector<Player>& cache = registeredPlayers.getCache();

            for (Player& player : cache)
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

uint64_t RegisteredPlayers::getPlayerBoneMatrixPtr(const uint64_t instance)
{
    if (mainGame.onlineRaid)
        return mem.ReadChain(
            instance,
            { sdk::ObservedPlayerView::PlayerBody, 0x30, 0x30, 0x10 },
            DmaCacheMode::Uncached);
    else
        return mem.ReadChain(
            instance,
            { sdk::Player::_playerBody, 0x30, 0x30, 0x10 },
            DmaCacheMode::Uncached);
}

uint64_t RegisteredPlayers::getPlayerHealthControllerPtr(const uint64_t instance)
{
    if (mainGame.onlineRaid)
    {
        return mem.ReadChain(instance, { sdk::ObservedPlayerView::ObservedPlayerController, sdk::ObservedPlayerController::HealthController });
    }
    else
        return instance; // offline corpse ptr in eft.player
}
