#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerLookup.h"
#include "PlayerClassifier.h"
#include "PlayerMemoryAccess.h"
#include "PlayerPosition.h"

#include "../../../UI/debug.h"
#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../MainGame.h"
#include "../../Unity/UnityContainers.h"
#include "../../Unity/UnityOffsets.h"
#include "../../Unity/Transform.h"
#include "../../../Core/Utilities.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <immintrin.h>
#include <optional>
#include <unordered_map>

namespace
{
    constexpr uint64_t kMinimumUnityAllocation = 0x1'0000'0000ULL;

    bool IsPlausibleUnityPointer(uint64_t pointer)
    {
        return pointer >= kMinimumUnityAllocation && Utils::valid_pointer(pointer);
    }

    void EnsureTransformCacheShape(Player& player);
    bool HasAnyBonePointers(const Player& player);
    bool HasAnchorBonePointer(const Player& player);
    bool HasMinimalBonePointers(const Player& player);
    bool HasFullBonePointers(const Player& player);
    bool HasRequiredBonePointers(const Player& player);
    bool IsMinimalBoneSlot(int slot);
    bool IsUsableBonePosition(const glm::vec3& position);
}

bool RegisteredPlayers::getBonePtrs(Player& player, bool waitForDma)
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

    uint64_t resolvedMatrixPtr = 0;
    if (!PlayerClassifier::get(player).tryResolveBoneMatrix(player, resolvedMatrixPtr) || !IsPlausibleUnityPointer(resolvedMatrixPtr))
    {
        player.minimalBonePointersReady = HasMinimalBonePointers(player);
        player.fullBonePointersReady = HasFullBonePointers(player);
        player.bonePointersNeedResolve = player.isZombie
            ? !HasAnyBonePointers(player)
            : !HasAnchorBonePointer(player);
        player.nextBonePointerResolve = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        return false;
    }

    const bool matrixChanged = player.playerBoneMatrixPtr != resolvedMatrixPtr;
    std::vector<uint64_t> firstLinks(player.boneList.size(), 0);
    std::vector<Memory::ScatterReadRequest> firstRequests;
    firstRequests.reserve(player.boneList.size());

    for (size_t index = 0; index < player.boneList.size(); ++index)
    {
        firstRequests.push_back({
            resolvedMatrixPtr + 0x20 + static_cast<uint64_t>(player.boneList[index]) * 0x8,
            &firstLinks[index],
            sizeof(firstLinks[index])
            });
    }

    std::vector<DWORD> firstBytes(firstRequests.size(), 0);
    const auto firstResult = mem.TryReadScatter(firstRequests.data(), firstRequests.size(), DmaCacheMode::Uncached, "Player bone pointers", firstBytes);
    if (firstResult == Memory::TryScatterReadResult::Failed)
    {
        player.nextBonePointerResolve = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        return false;
    }

    std::vector<uint64_t> resolvedBonePtrs(player.boneList.size(), 0);
    std::vector<size_t> secondIndexes;
    std::vector<Memory::ScatterReadRequest> secondRequests;
    secondIndexes.reserve(player.boneList.size());
    secondRequests.reserve(player.boneList.size());

    for (size_t index = 0; index < firstLinks.size(); ++index)
    {
        if (firstBytes[index] != sizeof(uint64_t) || !IsPlausibleUnityPointer(firstLinks[index]))
            continue;

        secondIndexes.emplace_back(index);
        secondRequests.push_back({ firstLinks[index] + 0x10, &resolvedBonePtrs[index], sizeof(resolvedBonePtrs[index]) });
    }

    if (secondRequests.empty())
    {
        player.nextBonePointerResolve = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        return false;
    }

    std::vector<DWORD> secondBytes(secondRequests.size(), 0);
    const auto secondResult = mem.TryReadScatter(secondRequests.data(), secondRequests.size(), DmaCacheMode::Uncached, "Player bone transforms", secondBytes);
    if (secondResult == Memory::TryScatterReadResult::Failed)
    {
        player.nextBonePointerResolve = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        return false;
    }

    std::vector<uint64_t> candidatePointers = matrixChanged ? std::vector<uint64_t>(player.boneList.size(), 0) : player.bonePtrs;
    for (size_t requestIndex = 0; requestIndex < secondIndexes.size(); ++requestIndex)
    {
        if (secondBytes[requestIndex] != sizeof(uint64_t))
            continue;

        const size_t boneIndex = secondIndexes[requestIndex];
        candidatePointers[boneIndex] = IsPlausibleUnityPointer(resolvedBonePtrs[boneIndex]) ? resolvedBonePtrs[boneIndex] : 0;
    }

    Player candidate = player;
    candidate.bonePtrs = candidatePointers;
    const bool minimalReady = HasMinimalBonePointers(candidate);
    const bool anyReady = HasAnyBonePointers(candidate);

    const bool anchorReady = HasAnchorBonePointer(candidate);
    if ((!player.isZombie && !anchorReady) || (player.isZombie && !anyReady))
    {
        player.nextBonePointerResolve = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        return false;
    }

    player.playerBoneMatrixPtr = resolvedMatrixPtr;
    for (size_t index = 0; index < candidatePointers.size(); ++index)
    {
        if (player.bonePtrs[index] != candidatePointers[index])
            player.bonePositions[index] = glm::vec3(0.0f);
    }

    player.bonePtrs = std::move(candidatePointers);
    EnsureTransformCacheShape(player);
    player.minimalBonePointersReady = minimalReady;
    player.fullBonePointersReady = HasFullBonePointers(player);
    player.bonePointersNeedResolve = player.isZombie ? !anyReady : !anchorReady;
    player.invalidBones = false;
    player.nextBonePointerResolve = player.fullBonePointersReady ? std::chrono::steady_clock::time_point{} : std::chrono::steady_clock::now() + std::chrono::milliseconds(250);

    if (player.isLocal)
        mem.ReportDmaHealthSuccess(DmaHealthSource::LocalPlayerPose);

    return anyReady;
}

namespace
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Milliseconds = std::chrono::milliseconds;

    constexpr int kMaxTransformChain = 512;
    constexpr size_t kMaxBoneScatterBytes = 64 * 1024;
    constexpr size_t kMaxBoneHierarchiesPerScatter = 12;
    constexpr size_t kMaxBonePointerResolvesPerPass = 4;

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
        uint64_t cacheEntryId{};
        uint64_t boneTransform{};

        int boneSlot{ -1 };

        BoneTransformCacheEntry cache{};
        TransformAccessReadOnly access{};

        int count{};
        size_t bufOffset{};

        bool needsMetadata{};
        bool metadataCompleted{};
        bool needsHierarchy{};
        bool hierarchyQueued{};
        bool hierarchyMetadataCompleted{};
        bool metadataDirty{};

        bool matrixQueued{};
        bool indicesQueued{};


        bool sampleCompleted{};
        bool hasPosition{};
        bool fullPose{};
        glm::vec3 position{};

        BoneReadKind kind{ BoneReadKind::Normal };
    };

    struct BonePlayerSnapshot
    {
        uint64_t instance{};
        uint64_t cacheEntryId{};
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
        bool readComplete{};
        std::vector<LiveBoneRead*> bones;
    };

    static bool ReadExactScatter(
        Memory& memory,
        std::span<const Memory::ScatterReadRequest> requests,
        std::vector<uint8_t>& completed,
        std::string_view label,
        bool waitForDma)
    {
        completed.assign(requests.size(), 0);
        bool allComplete = true;

        size_t start = 0;

        while (start < requests.size())
        {
            size_t end = start;
            size_t chunkBytes = 0;

            while (end < requests.size() &&
                end - start < Memory::MaxDmaScatterRequests)
            {
                const size_t requestBytes = requests[end].size;

                if (requestBytes == 0 ||
                    requestBytes > Memory::MaxDmaTransferBytes ||
                    chunkBytes + requestBytes > Memory::MaxDmaTransferBytes)
                {
                    break;
                }

                chunkBytes += requestBytes;
                ++end;
            }

            if (end == start)
            {
                allComplete = false;
                ++start;
                continue;
            }

            const size_t requestCount = end - start;
            std::vector<DWORD> bytesRead(requestCount, 0);
            const auto result = memory.TryReadScatter(
                requests.data() + start,
                requestCount,
                DmaCacheMode::Uncached,
                label,
                bytesRead
            );

            if (result == Memory::TryScatterReadResult::Failed)
            {
                allComplete = false;
                start = end;
                continue;
            }

            for (size_t localIndex = 0;
                localIndex < requestCount;
                ++localIndex)
            {
                const size_t requestIndex = start + localIndex;
                const DWORD expectedBytes = static_cast<DWORD>(
                    requests[requestIndex].size
                    );

                if (bytesRead[localIndex] == expectedBytes)
                    completed[requestIndex] = 1;
                else
                    allComplete = false;
            }

            start = end;
        }

        return allComplete;
    }

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

        constexpr float kMinimumWorldDistanceSquared = 0.01f;
        return glm::dot(position, position) >= kMinimumWorldDistanceSquared;
    }

    static bool IsPlausibleBoneWorldPosition(
        const glm::vec3& position)
    {
        constexpr float kMaxAbsoluteWorldCoordinate = 100000.0f;

        return std::isfinite(position.x) &&
            std::isfinite(position.y) &&
            std::isfinite(position.z) &&
            std::fabs(position.x) <= kMaxAbsoluteWorldCoordinate &&
            std::fabs(position.y) <= kMaxAbsoluteWorldCoordinate &&
            std::fabs(position.z) <= kMaxAbsoluteWorldCoordinate;
    }

    static float DistanceSquared(
        const glm::vec3& first,
        const glm::vec3& second)
    {
        const glm::vec3 delta = first - second;
        return glm::dot(delta, delta);
    }

    static bool AreBonesNear(
        const glm::vec3& first,
        const glm::vec3& second)
    {
        constexpr float kMaximumSkeletonSpan = 6.0f;
        return DistanceSquared(first, second) <=
            kMaximumSkeletonSpan * kMaximumSkeletonSpan;
    }

    static bool HasUsableAnchor(const Player& player)
    {
        return IsUsableBonePosition(PlayerPosition::getBestBasePosition(player));
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

    static bool HasAnyBonePointers(const Player& player)
    {
        return std::any_of(player.bonePtrs.begin(), player.bonePtrs.end(), [](uint64_t bonePtr)
            {
                return Utils::valid_pointer(bonePtr);
            });
    }

    static bool HasAnchorBonePointer(const Player& player)
    {
        return std::any_of(
            std::begin(kMinimalBoneSlots),
            std::end(kMinimalBoneSlots),
            [&](int slot)
            {
                return slot >= 0 &&
                    static_cast<size_t>(slot) < player.bonePtrs.size() &&
                    Utils::valid_pointer(player.bonePtrs[static_cast<size_t>(slot)]);
            });
    }

    static bool HasFullBonePointers(const Player& player)
    {
        return !player.bonePtrs.empty() && std::all_of(player.bonePtrs.begin(), player.bonePtrs.end(), [](uint64_t bonePtr)
            {
                return Utils::valid_pointer(bonePtr);
            });
    }

    static bool HasRequiredBonePointers(const Player& player)
    {
        return player.isZombie ? HasAnyBonePointers(player) : HasAnchorBonePointer(player);
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

    static bool BatchReadBoneWorldPositions(Memory& memory, std::vector<LiveBoneRead>& reads, bool waitForDma)
    {
        if (reads.empty())
            return true;

        bool dmaComplete = true;

        for (LiveBoneRead& read : reads)
        {
            read.needsMetadata = false;
            read.metadataCompleted = false;
            read.needsHierarchy = false;
            read.hierarchyQueued = false;
            read.hierarchyMetadataCompleted = false;
            read.metadataDirty = false;
            read.matrixQueued = false;
            read.indicesQueued = false;
            read.sampleCompleted = false;
            read.hasPosition = false;
            read.count = 0;
        }

        {
            std::vector<Memory::ScatterReadRequest> metadataRequests;
            std::vector<LiveBoneRead*> metadataReads;
            metadataRequests.reserve(reads.size());
            metadataReads.reserve(reads.size());

            for (LiveBoneRead& read : reads)
            {
                if (HasUsableTransformMetadata(read))
                    continue;

                read.needsMetadata = true;
                read.metadataDirty = true;
                read.cache = {};
                read.cache.boneTransform = read.boneTransform;
                read.access = {};

                const uint64_t accessAddress =
                    read.boneTransform +
                    UnityOffsets::TransformInternal_TransformAccessOffset;

                if (!Memory::IsValidPointer(accessAddress))
                {
                    read.cache.valid = false;
                    continue;
                }

                metadataRequests.push_back(
                    { accessAddress, &read.access, sizeof(read.access) }
                );
                metadataReads.emplace_back(&read);
            }

            std::vector<uint8_t> completed;
            dmaComplete &= ReadExactScatter(
                memory,
                metadataRequests,
                completed,
                "Player bone metadata",
                waitForDma
            );

            for (size_t i = 0; i < metadataReads.size(); ++i)
            {
                if (completed[i])
                    metadataReads[i]->metadataCompleted = true;
                else
                    metadataReads[i]->cache.valid = false;
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
                read.sampleCompleted = read.metadataCompleted;
                continue;
            }

            read.cache.transformData =
                read.access.pTransformData;

            read.cache.transformIndex =
                read.access.index;

            read.needsHierarchy = true;
        }

        {
            std::vector<Memory::ScatterReadRequest> hierarchyRequests;
            std::vector<LiveBoneRead*> hierarchyReads;
            hierarchyRequests.reserve(reads.size() * 2);
            hierarchyReads.reserve(reads.size());

            for (LiveBoneRead& read : reads)
            {
                if (!read.needsHierarchy)
                    continue;

                const uint64_t matrixAddress =
                    read.cache.transformData +
                    UnityOffsets::Hierarchy_VerticesOffset;
                const uint64_t indicesAddress =
                    read.cache.transformData +
                    UnityOffsets::Hierarchy_IndicesOffset;

                if (!Memory::IsValidPointer(matrixAddress) ||
                    !Memory::IsValidPointer(indicesAddress))
                {
                    read.cache.valid = false;
                    continue;
                }

                hierarchyRequests.push_back(
                    {
                        matrixAddress,
                        &read.cache.transformArray,
                        sizeof(read.cache.transformArray)
                    }
                );
                hierarchyRequests.push_back(
                    {
                        indicesAddress,
                        &read.cache.transformIndices,
                        sizeof(read.cache.transformIndices)
                    }
                );
                hierarchyReads.emplace_back(&read);
                read.hierarchyQueued = true;
            }

            std::vector<uint8_t> completed;
            dmaComplete &= ReadExactScatter(
                memory,
                hierarchyRequests,
                completed,
                "Player bone hierarchy",
                waitForDma
            );

            for (size_t i = 0; i < hierarchyReads.size(); ++i)
            {
                const size_t requestIndex = i * 2;

                if (!completed[requestIndex] ||
                    !completed[requestIndex + 1])
                {
                    hierarchyReads[i]->cache.valid = false;
                }
                else
                {
                    hierarchyReads[i]->hierarchyMetadataCompleted = true;
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
                read.sampleCompleted = read.hierarchyMetadataCompleted;
                continue;
            }

            read.cache.valid = true;
        }

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
            return dmaComplete;

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

            constexpr size_t kRequestsPerHierarchy = 2;
            constexpr size_t kMaxPositionRequests =
                kMaxBoneHierarchiesPerScatter * kRequestsPerHierarchy;

            std::array<Memory::ScatterReadRequest, kMaxPositionRequests>
                positionRequests{};
            size_t positionRequestCount = 0;
            size_t cursor = 0;

            for (size_t i = start; i < end; ++i)
            {
                TransformHierarchyRead& hierarchy = hierarchyReads[i];
                const size_t matrixCount = hierarchy.count;

                hierarchy.bufOffset = cursor;
                cursor += matrixCount;

                hierarchy.readComplete = false;

                positionRequests[positionRequestCount++] =
                {
                    hierarchy.key.transformArray,
                    matrices.data() + hierarchy.bufOffset,
                    sizeof(Matrix34) * matrixCount
                };
                positionRequests[positionRequestCount++] =
                {
                    hierarchy.key.transformIndices,
                    indices.data() + hierarchy.bufOffset,
                    sizeof(int32_t) * matrixCount
                };
            }

            std::vector<uint8_t> completed;
            dmaComplete &= ReadExactScatter(
                memory,
                std::span<const Memory::ScatterReadRequest>(
                    positionRequests.data(),
                    positionRequestCount
                ),
                completed,
                "Player bone positions",
                waitForDma
            );

            for (size_t i = start; i < end; ++i)
            {
                TransformHierarchyRead& hierarchy = hierarchyReads[i];
                const size_t requestIndex =
                    (i - start) * kRequestsPerHierarchy;

                hierarchy.readComplete =
                    completed[requestIndex] &&
                    completed[requestIndex + 1];
            }

            for (size_t i = start; i < end; ++i)
            {
                TransformHierarchyRead& hierarchy = hierarchyReads[i];

                if (!hierarchy.readComplete ||
                    hierarchy.count == 0)
                {
                    continue;
                }

                for (LiveBoneRead* read : hierarchy.bones)
                {
                    read->sampleCompleted = true;

                    const glm::vec3 position =
                        computeTransformPosition(
                            matrices.data() + hierarchy.bufOffset,
                            indices.data() + hierarchy.bufOffset,
                            read->cache.transformIndex,
                            static_cast<int>(hierarchy.count)
                        );

                    if (!IsUsableBonePosition(position) ||
                        !IsPlausibleBoneWorldPosition(position))
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

        return dmaComplete;
    }

    static void ApplyBoneResults(std::vector<LiveBoneRead>& reads)
    {
        if (reads.empty())
            return;

        struct PendingPose
        {
            uint64_t instance{};
            uint64_t cacheEntryId{};
            bool fullRequested = false;
            std::vector<glm::vec3> positions;
            std::vector<BoneTransformCacheEntry> transformCache;
            std::vector<uint8_t> successfulSlots;
        };

        std::vector<PendingPose> poses;
        poses.reserve(reads.size());

        auto getPose = [&](Player& player) -> PendingPose&
            {
                const auto found = std::find_if(poses.begin(), poses.end(), [&](const PendingPose& pose)
                    {
                        return pose.instance == player.instance && pose.cacheEntryId == player.cacheEntryId;
                    });

                if (found != poses.end())
                    return *found;

                PendingPose pose{};
                pose.instance = player.instance;
                pose.cacheEntryId = player.cacheEntryId;
                pose.positions.resize(player.bonePositions.size(), glm::vec3(0.0f));
                pose.transformCache = player.boneTransformCache;
                pose.successfulSlots.resize(player.bonePositions.size(), 0);
                poses.emplace_back(std::move(pose));
                return poses.back();
            };

        std::lock_guard<std::mutex> lock(playerMutex);

        std::vector<Player>& cache =
            registeredPlayers.getCache();

        for (const LiveBoneRead& read : reads)
        {
            Player* player = PlayerLookup::findByInstance(cache, read.playerInstance);

            if (!player || player->cacheEntryId != read.cacheEntryId)
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

            if (!read.cache.valid &&
                boneIndex < player->boneTransformCache.size())
            {
                player->boneTransformCache[boneIndex] = {};
                player->boneTransformCache[boneIndex].boneTransform =
                    read.boneTransform;
            }

            if (!read.sampleCompleted)
                continue;

            PendingPose& pose = getPose(*player);
            pose.fullRequested = pose.fullRequested || read.fullPose;

            if (!read.hasPosition ||
                !IsPlausibleBoneWorldPosition(read.position))
                continue;

            pose.positions[boneIndex] = read.position;
            pose.successfulSlots[boneIndex] = 1;

            if (boneIndex < pose.transformCache.size())
                pose.transformCache[boneIndex] = read.cache;
        }

        for (PendingPose& pose : poses)
        {
            Player* player = PlayerLookup::findByInstance(cache, pose.instance);

            if (!player || player->cacheEntryId != pose.cacheEntryId)
                continue;

            player->minimalBonePointersReady = HasMinimalBonePointers(*player);
            player->fullBonePointersReady = HasFullBonePointers(*player);
            player->bonePointersNeedResolve = player->isZombie
                ? !HasAnyBonePointers(*player)
                : !HasAnchorBonePointer(*player);

            const size_t baseIndex = static_cast<size_t>(boneListIndexes::Base);
            const size_t leftFootIndex = static_cast<size_t>(boneListIndexes::LFoot);
            const size_t rightFootIndex = static_cast<size_t>(boneListIndexes::RFoot);

            const bool baseRead = baseIndex < pose.successfulSlots.size() && pose.successfulSlots[baseIndex];
            const bool leftFootRead = leftFootIndex < pose.successfulSlots.size() && pose.successfulSlots[leftFootIndex];
            const bool rightFootRead = rightFootIndex < pose.successfulSlots.size() && pose.successfulSlots[rightFootIndex];

            if (!baseRead && (!leftFootRead || !rightFootRead))
                continue;

            if (baseRead)
                player->bonePositions[baseIndex] = pose.positions[baseIndex];

            if (leftFootRead)
                player->bonePositions[leftFootIndex] = pose.positions[leftFootIndex];
            if (rightFootRead)
                player->bonePositions[rightFootIndex] = pose.positions[rightFootIndex];

            for (const int slot : kMinimalBoneSlots)
            {
                const size_t index = static_cast<size_t>(slot);
                if (index < player->bonePositions.size() &&
                    IsUsableBonePosition(player->bonePositions[index]) &&
                    index < player->boneTransformCache.size())
                {
                    player->boneTransformCache[index] = pose.transformCache[index];
                }
            }

            const glm::vec3 anchor = PlayerPosition::getBestBasePosition(*player);
            const bool anchorValid = IsUsableBonePosition(anchor);
            size_t optionalBonesAccepted = 0;

            if (anchorValid && pose.fullRequested)
            {
                for (size_t index = 0; index < pose.positions.size(); ++index)
                {
                    if (IsMinimalBoneSlot(static_cast<int>(index)) ||
                        !pose.successfulSlots[index] ||
                        !AreBonesNear(anchor, pose.positions[index]))
                    {
                        continue;
                    }

                    player->bonePositions[index] = pose.positions[index];
                    if (index < player->boneTransformCache.size())
                        player->boneTransformCache[index] = pose.transformCache[index];
                    ++optionalBonesAccepted;
                }
            }

            if (!anchorValid)
                continue;

            player->minimalBonePoseValid = true;
            if (pose.fullRequested && optionalBonesAccepted > 0)
                player->fullBonePoseValid = true;
            player->invalidBones = false;
            player->consecutiveBadBoneSamples = 0;

            player->location = anchor;

            if (pose.successfulSlots[boneListIndexes::Base])
            {
                const auto sampledAt = std::chrono::steady_clock::now();
                if (player->worldVelocityTracker.update(pose.positions[boneListIndexes::Base], sampledAt))
                {
                    player->worldVelocity = player->worldVelocityTracker.value();
                    player->lastWorldVelocityUpdate = sampledAt;
                }
                player->worldVelocityValid = player->worldVelocityTracker.valid();
            }

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
                read.cacheEntryId = player.cacheEntryId;
                read.boneTransform = bonePtr;
                read.boneSlot = slot;
                read.kind = kind;
                read.fullPose = readFullBoneList;

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


void RegisteredPlayers::boneTask()
{
    bool motionUpdated = false;

    try
    {
        if (!mem.IsDmaOperational())
            return;

        motionUpdated = updateBtrMotion();

        const auto now = Clock::now();
        constexpr auto kMaximumBoneRefreshGap = std::chrono::milliseconds(35);

        if (nextGuaranteedBoneRefresh == Clock::time_point{})
            nextGuaranteedBoneRefresh = now + kMaximumBoneRefreshGap;

        const bool guaranteeFreshRead = now >= nextGuaranteedBoneRefresh;
        std::optional<ScopedDmaPriority> guaranteedPriority;
        if (guaranteeFreshRead)
        {
            guaranteedPriority.emplace(DmaPriority::High);
            nextGuaranteedBoneRefresh = now + kMaximumBoneRefreshGap;
        }

        const auto fullBoneInterval =
            std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double, std::milli>(
                    (std::max)(1.0, globals::taskPlayersBones)));

        const bool runFullBonePass =
            nextFullBoneUpdate == Clock::time_point{} ||
            now >= nextFullBoneUpdate;

        if (runFullBonePass)
            nextFullBoneUpdate = now + fullBoneInterval;

        constexpr float kFullBoneScanEdgeBufferPixels = 200.0f;
        const CameraManagerSnapshot projection = cameraManagerTest.snapshot();

        const auto IsFullBoneScanEligible = [&](const Player& player)
            {
                if (!projection || !projection->valid)
                    return false;

                glm::vec2 screenPosition{};
                const auto projectsWithinScanArea = [&](const glm::vec3& position)
                    {
                        return CameraManager::worldToScreen(*projection, position, screenPosition,
                            espGlobals::gameRes.x, espGlobals::gameRes.y, kFullBoneScanEdgeBufferPixels);
                    };

                if (projectsWithinScanArea(PlayerPosition::getBestBasePosition(player)))
                    return true;

                constexpr std::array<boneListIndexes, 4> kUpperBodyScanAnchors =
                {
                    boneListIndexes::Pelvis,
                    boneListIndexes::Spine,
                    boneListIndexes::Neck,
                    boneListIndexes::Head
                };

                for (const boneListIndexes bone : kUpperBodyScanAnchors)
                {
                    const size_t index = static_cast<size_t>(bone);
                    if (index < player.bonePositions.size() &&
                        IsUsableBonePosition(player.bonePositions[index]) &&
                        projectsWithinScanArea(player.bonePositions[index]))
                    {
                        return true;
                    }
                }

                return false;
            };

        struct PendingBoneScan
        {
            BonePlayerSnapshot snapshot{};
            bool readFullBoneList = false;
        };

        struct PendingBoneResolve
        {
            uint64_t instance{};
            uint64_t cacheEntryId{};
            Player workingCopy{};
            bool resolved = false;
        };

        std::vector<PendingBoneResolve> pendingResolves;

        
        if (runFullBonePass)
        {
            std::lock_guard<std::mutex> lock(playerMutex);
            std::vector<Player>& cache = registeredPlayers.getCache();

            if (!cache.empty())
            {
                const auto localPlayer = std::find_if(cache.begin(), cache.end(), [](const Player& player)
                    {
                        return player.isLocal && !player.isBTR && !player.isDead && !player.hasExfiled &&
                            (player.bonePointersNeedResolve || !HasRequiredBonePointers(player)) &&
                            (player.nextBonePointerResolve == Clock::time_point{} || Clock::now() >= player.nextBonePointerResolve);
                    });

                if (localPlayer != cache.end())
                {
                    PendingBoneResolve resolve{};
                    resolve.instance = localPlayer->instance;
                    resolve.cacheEntryId = localPlayer->cacheEntryId;
                    resolve.workingCopy = *localPlayer;
                    pendingResolves.emplace_back(std::move(resolve));
                }

                for (const Player& player : cache)
                {
                    if (!player.btrExitBoneRefreshPending || player.isLocal || player.isBTR || player.isDead || player.hasExfiled ||
                        !Utils::valid_pointer(player.instance))
                        continue;

                    PendingBoneResolve resolve{};
                    resolve.instance = player.instance;
                    resolve.cacheEntryId = player.cacheEntryId;
                    resolve.workingCopy = player;
                    pendingResolves.emplace_back(std::move(resolve));
                }

                const size_t maximumPendingResolves = pendingResolves.size() + kMaxBonePointerResolvesPerPass;
                const size_t start = boneResolveCursor % cache.size();
                size_t inspected = 0;

                while (inspected < cache.size() &&
                    pendingResolves.size() < maximumPendingResolves)
                {
                    const size_t index = (start + inspected) % cache.size();
                    Player& player = cache[index];
                    ++inspected;

                    if (!Utils::valid_pointer(player.instance) ||
                        player.isBTR ||
                        player.isLocal ||
                        player.btrExitBoneRefreshPending ||
                        player.isDead ||
                        player.hasExfiled)
                    {
                        continue;
                    }

                    const bool fullPointersRequired = runFullBonePass && IsFullBoneScanEligible(player);

                    if (HasRequiredBonePointers(player) && (!fullPointersRequired || HasFullBonePointers(player)))
                    {
                        continue;
                    }

                    if (player.nextBonePointerResolve != Clock::time_point{} && now < player.nextBonePointerResolve)
                        continue;

                    PendingBoneResolve resolve{};
                    resolve.instance = player.instance;
                    resolve.cacheEntryId = player.cacheEntryId;
                    resolve.workingCopy = player;
                    pendingResolves.emplace_back(std::move(resolve));
                }

                boneResolveCursor = (start + (std::max)(size_t{ 1 }, inspected)) % cache.size();
            }
        }

        for (PendingBoneResolve& resolve : pendingResolves)
            resolve.resolved = getBonePtrs(resolve.workingCopy, false);

        if (!pendingResolves.empty())
        {
            std::lock_guard<std::mutex> lock(playerMutex);
            std::vector<Player>& cache = registeredPlayers.getCache();

            for (PendingBoneResolve& resolve : pendingResolves)
            {
                Player* player = PlayerLookup::findByInstance(cache, resolve.instance);

                if (!player || player->cacheEntryId != resolve.cacheEntryId ||
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
                player->minimalBonePointersReady = resolve.workingCopy.minimalBonePointersReady;
                player->fullBonePointersReady = resolve.workingCopy.fullBonePointersReady;
                player->nextBonePointerResolve = resolve.workingCopy.nextBonePointerResolve;
                if (resolve.resolved)
                    player->btrExitBoneRefreshPending = false;
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
                pending.snapshot.cacheEntryId = player.cacheEntryId;
                pending.snapshot.distance = player.distance;
                pending.snapshot.isLocal = player.isLocal;
                pending.snapshot.bonePtrs = player.bonePtrs;
                pending.snapshot.transformCache = player.boneTransformCache;

                pending.readFullBoneList =
                    runFullBonePass &&
                    IsFullBoneScanEligible(player) &&
                    HasAnyBonePointers(player);

                pendingScans.emplace_back(std::move(pending));
            }
        }

        std::stable_sort(pendingScans.begin(), pendingScans.end(), [](const PendingBoneScan& first, const PendingBoneScan& second)
            {
                return first.snapshot.distance < second.snapshot.distance;
            });

        if (pendingScans.empty())
        {
            publishCacheSnapshot(motionUpdated);
            return;
        }

        std::vector<LiveBoneRead> priorityReads;
        std::vector<LiveBoneRead> backgroundReads;

        for (const PendingBoneScan& pending : pendingScans)
        {
            std::vector<LiveBoneRead>& reads = pending.snapshot.isLocal || pending.readFullBoneList
                ? priorityReads
                : backgroundReads;

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
                AppendPlayerBoneReads(
                    pending.snapshot,
                    BoneReadKind::Normal,
                    false,
                    reads);
            }
        }

        bool priorityDmaComplete = true;
        bool backgroundDmaComplete = true;

        if (!priorityReads.empty())
        {
            priorityDmaComplete = BatchReadBoneWorldPositions(mem, priorityReads, true);

            motionUpdated = motionUpdated || std::any_of(
                priorityReads.begin(),
                priorityReads.end(),
                [](const LiveBoneRead& read)
                {
                    return read.hasPosition;
                });

            ApplyBoneResults(priorityReads);
        }

        if (!backgroundReads.empty())
        {
            backgroundDmaComplete = BatchReadBoneWorldPositions(mem, backgroundReads, guaranteeFreshRead);

            motionUpdated = motionUpdated || std::any_of(
                backgroundReads.begin(),
                backgroundReads.end(),
                [](const LiveBoneRead& read)
                {
                    return read.hasPosition;
                });

            ApplyBoneResults(backgroundReads);
        }

        if (priorityDmaComplete && backgroundDmaComplete && pendingResolves.empty())
            nextGuaranteedBoneRefresh = Clock::now() + kMaximumBoneRefreshGap;

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
