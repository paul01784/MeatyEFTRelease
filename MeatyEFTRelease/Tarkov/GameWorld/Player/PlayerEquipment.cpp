#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "../../../Web/MeatyAPI/DogTagAPI.h"
#include "../../../UI/debug.h"
#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "DogTagCache.h"
#include "PlayerMemoryAccess.h"
#include "../MainGame.h"
#include "../QuestManager.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"
#include "../../Unity/UnityContainers.h"
#include "../../Unity/UnityOffsets.h"
#include "../../../Core/Utilities.h"
#include "WatchList.h"
#include "../Loot/WishList.h"
#include "../Loot/Loot.h"

#include <algorithm>
#include <chrono>
#include <type_traits>
#include <unordered_set>

static const std::unordered_set<std::string> skipNames =
{
    "Compass",
    "ArmBand",
    "Eyewear",
    "Pockets"
};

void RegisteredPlayers::playerEquipment()
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
        decltype(std::declval<Player&>()._slots)
        >;

    using SlotEntry = typename SlotVec::value_type;

    using PlayerValueT =
        std::remove_reference_t<
        decltype(std::declval<Player&>().playerValue)
        >;

    static constexpr size_t kMaxEquipmentInitPerPass = 1;
    static constexpr size_t kMaxEquipmentScanPerPass = 5;

    static size_t initRoundRobinCursor = 0;
    static size_t scanRoundRobinCursor = 0;
    static Clock::time_point nextFailureLog{};

    const auto logFailure = [&](const char* message)
        {
            const Clock::time_point now = Clock::now();
            if (now < nextFailureLog)
                return;

            nextFailureLog = now + std::chrono::seconds(30);
            LOGS.logError(message);
        };

    struct InitJob
    {
        uint64_t instance = 0;
        uint64_t cacheEntryId = 0;
        uint64_t inventoryControllerAddr = 0;
        bool isPlayer = false;
        bool requiresDogTagSlot = false;
        bool requiresSecuredContainerSlot = false;

        uint64_t inventoryController = 0;
        uint64_t inventory = 0;
        uint64_t equipment = 0;
        uint64_t slotsPtr = 0;
    };

    struct InitResult
    {
        uint64_t instance = 0;
        uint64_t cacheEntryId = 0;
        SlotVec slots;
        bool requiresDogTagSlot = false;
        bool requiresSecuredContainerSlot = false;
        bool success = false;
    };

    struct ScanJob
    {
        uint64_t instance = 0;
        uint64_t cacheEntryId = 0;
        bool isPlayer = false;
        bool isPmc = false;
        bool dogTagApiLookupAttempted = false;
        bool dogTagDataComplete = false;
        std::string dogTagProfileId;
        std::string accountId;
        std::string nickname;
        int lvl = 0;
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
        uint64_t cacheEntryId = 0;
        bool isPmc = false;
        SlotVec slots;
        PlayerValueT playerValue{};
        Clock::time_point updateTime{};

        bool hasProfileUpdate = false;
        bool hasDogTagDataUpdate = false;
        bool dogTagApiLookupAttempted = false;
        bool dogTagDataComplete = false;
        std::string dogTagProfileId;
        std::string accountId;
        std::string nickname;
        int lvl = 0;
    };

    auto executeScatter = [&](auto&& queueReads) -> bool
        {
            ScatterReadBatch batch(
                mem,
                DmaCacheMode::Uncached,
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
            const size_t playerCount = static_cast<size_t>(std::count_if(candidates.begin(), candidates.end(), [](const InitJob& job)
                {
                    return job.isPlayer;
                }));
            const size_t eligibleCount = playerCount > 0 ? playerCount : total;
            const size_t count =
                std::min(kMaxEquipmentInitPerPass, eligibleCount);

            const size_t start =
                initRoundRobinCursor % eligibleCount;

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
                (start + count) % eligibleCount;

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
        const PlayerSnapshot initSnapshot = getCacheSnapshot();
        initCandidates.reserve(initSnapshot->size());

        for (const Player& player : *initSnapshot)
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
            job.cacheEntryId = player.cacheEntryId;
            job.inventoryControllerAddr =
                player.P_InventoryControllerAddr;
            job.isPlayer = player.isPlayer;
            job.requiresDogTagSlot = player.isPlayer && !player.isPlayerScav && !player.isAi && !player.isLocal;
            job.requiresSecuredContainerSlot = player.isPlayer && !player.isPlayerScav && !player.isAi;

            initCandidates.emplace_back(std::move(job));
        }

        std::stable_sort(initCandidates.begin(), initCandidates.end(), [](const InitJob& first, const InitJob& second)
            {
                return first.isPlayer && !second.isPlayer;
            });

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
                logFailure("[PLAYER][EQUIP] Slot-cache init scatter failed");
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
                result.cacheEntryId = job.cacheEntryId;
                result.requiresDogTagSlot = job.requiresDogTagSlot;
                result.requiresSecuredContainerSlot = job.requiresSecuredContainerSlot;

                int slotCount = 0;
                ScatterReadBatch slotCountBatch(mem, DmaCacheMode::Uncached, "Player equipment slot count");
                if (!slotCountBatch.Add(job.slotsPtr + OffsetsArray::CountOffset, slotCount) ||
                    !slotCountBatch.Execute() || slotCount < 0 || slotCount > 128)
                    continue;

                std::vector<uint64_t> slotPointers(static_cast<size_t>(slotCount), 0);
                if (!slotPointers.empty())
                {
                    ScatterReadBatch slotsBatch(mem, DmaCacheMode::Uncached, "Player equipment slots");
                    if (!slotsBatch.AddBytes(job.slotsPtr + OffsetsArray::ArrBaseOffset, slotPointers.data(), slotPointers.size() * sizeof(uint64_t)) ||
                        !slotsBatch.Execute())
                    {
                        continue;
                    }
                }

                struct SlotNameRead
                {
                    uint64_t slot = 0;
                    uint64_t namePointer = 0;
                    int length = 0;
                    std::vector<wchar_t> characters;
                };

                std::vector<SlotNameRead> nameReads;
                nameReads.reserve(slotPointers.size());
                for (uint64_t slot : slotPointers)
                {
                    if (Utils::valid_pointer(slot))
                        nameReads.push_back({ .slot = slot });
                }

                bool namesComplete = executeScatter([&](auto& batch)
                    {
                        bool queued = false;
                        for (SlotNameRead& read : nameReads)
                            queued |= batch.Add(read.slot + sdk::Slot::ID, read.namePointer);
                        return queued;
                    });

                if (!namesComplete)
                    continue;

                namesComplete = executeScatter([&](auto& batch)
                    {
                        bool queued = false;
                        for (SlotNameRead& read : nameReads)
                        {
                            if (Utils::valid_pointer(read.namePointer))
                                queued |= batch.Add(read.namePointer + 0x10, read.length);
                        }
                        return queued;
                    });

                if (!namesComplete)
                    continue;

                ScatterReadBatch stringBatch(mem, DmaCacheMode::Uncached, "Player equipment slot names");
                bool queuedStrings = false;
                for (SlotNameRead& read : nameReads)
                {
                    if (read.length <= 0 || read.length > 128)
                        continue;

                    read.characters.assign(static_cast<size_t>(read.length), L'\0');
                    queuedStrings |= stringBatch.AddBytes(read.namePointer + 0x14, read.characters.data(), read.characters.size() * sizeof(wchar_t));
                }

                if (queuedStrings && !stringBatch.Execute())
                    continue;

                for (const SlotNameRead& read : nameReads)
                {
                    if (read.characters.empty())
                        continue;

                    const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, read.characters.data(), read.length, nullptr, 0, nullptr, nullptr);
                    if (utf8Length <= 0)
                        continue;

                    std::string name(static_cast<size_t>(utf8Length), '\0');
                    WideCharToMultiByte(CP_UTF8, 0, read.characters.data(), read.length, name.data(), utf8Length, nullptr, nullptr);

                    if (!name.empty() && !skipNames.contains(name))
                        result.slots.push_back({ name, read.slot });
                }

                const bool hasDogTagSlot = std::any_of(result.slots.begin(), result.slots.end(), [](const SlotEntry& slot)
                    {
                        return TrimEFT(slot.name) == "Dogtag";
                    });
                const bool hasSecuredContainerSlot = std::any_of(result.slots.begin(), result.slots.end(), [](const SlotEntry& slot)
                    {
                        return TrimEFT(slot.name) == "SecuredContainer";
                    });
                const bool requiredSlotsReady =
                    (!result.requiresDogTagSlot || hasDogTagSlot) &&
                    (!result.requiresSecuredContainerSlot || hasSecuredContainerSlot);

                if (!result.slots.empty() && requiredSlotsReady)
                {
                    result.success = true;
                    initResults.emplace_back(std::move(result));
                }
            }
        }

        // Queue slot-pointer cache updates for the live player owner.
        // The equipment worker never waits for, or mutates, the live cache.
        for (InitResult& result : initResults)
        {
            if (!result.success)
                continue;

            const uint64_t instance = result.instance;
            const uint64_t cacheEntryId = result.cacheEntryId;
            queuePlayerEdit(instance, cacheEntryId, [slots = std::move(result.slots)](Player& player) mutable
                {
                    if (player.isBTR || player.isDead || player.hasExfiled || player.equipInited)
                        return;

                    player._slots = std::move(slots);
                    player.equipInited = true;
                    player.lastEquipmentUpdate = {};
                });
        }

        // Stage 3: Build contents-update candidates. Initialized equipment is due
        // every 10 seconds; players without an equipment dogtag ID retry every pass.
        std::vector<ScanJob> scanCandidates;
        const PlayerSnapshot scanSnapshot = getCacheSnapshot();
        scanCandidates.reserve(scanSnapshot->size());
        const Clock::time_point now = Clock::now();

        for (const Player& player : *scanSnapshot)
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

            const bool hasDogTagSlot = std::any_of(player._slots.begin(), player._slots.end(), [](const SlotEntry& slot)
                {
                    return TrimEFT(slot.name) == "Dogtag";
                });
            const bool dogTagReadPending = player.isPlayer && hasDogTagSlot && !player.equipmentDogTagDataComplete;
            const bool isPmc = player.isPlayer && !player.isPlayerScav && !player.isAi;
            const bool containerReadPending = isPmc && std::any_of(player._slots.begin(), player._slots.end(), [](const SlotEntry& slot)
                {
                    return TrimEFT(slot.name) == "SecuredContainer" && slot.equipName.empty();
                });
            const bool emptySlotConfirmationPending = std::any_of(player._slots.begin(), player._slots.end(), [](const SlotEntry& slot)
                {
                    return slot.consecutiveEmptyReads > 0;
                });
            const bool equipmentUpdateDue = now - player.lastEquipmentUpdate >= player.equipmentUpdateInterval;

            if (!dogTagReadPending && !containerReadPending && !emptySlotConfirmationPending && !equipmentUpdateDue)
            {
                continue;
            }

            ScanJob job{};
            job.instance = player.instance;
            job.cacheEntryId = player.cacheEntryId;
            job.isPlayer = player.isPlayer;
            job.isPmc = isPmc;
            job.dogTagApiLookupAttempted = player.dogTagApiLookupAttempted;
            job.dogTagDataComplete = player.equipmentDogTagDataComplete;
            job.dogTagProfileId = player.equipmentDogTagProfileId;
            job.accountId = player.equipmentDogTagAccountId;
            job.nickname = player.equipmentDogTagNickname;
            job.lvl = player.equipmentDogTagLevel;
            job.slots = player._slots;
            job.updateTime = now;

            scanCandidates.emplace_back(std::move(job));
        }

        std::stable_sort(scanCandidates.begin(), scanCandidates.end(), [](const ScanJob& first, const ScanJob& second)
            {
                const auto needsDogTagDetails = [](const ScanJob& job)
                    {
                        return job.isPlayer && !job.dogTagDataComplete &&
                            std::any_of(job.slots.begin(), job.slots.end(), [](const SlotEntry& slot)
                                {
                                    return TrimEFT(slot.name) == "Dogtag";
                                });
                    };
                const bool firstNeedsDetails = needsDogTagDetails(first);
                const bool secondNeedsDetails = needsDogTagDetails(second);
                if (firstNeedsDetails != secondNeedsDetails)
                    return firstNeedsDetails;
                return first.isPlayer && !second.isPlayer;
            });

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
                const std::string slotName = TrimEFT(slot.name);

                if (slotName == "Scabbard")
                {
                    continue;
                }

                // These two slot identities are static for a live player. Keep
                // retrying until their initial details are complete, then leave
                // them alone during normal ten-second equipment refreshes.
                if ((slotName == "SecuredContainer" && !slot.equipName.empty()) ||
                    (slotName == "Dogtag" && job.dogTagDataComplete))
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
            logFailure("[PLAYER][EQUIP] Slot-content scatter failed; retaining last good equipment and retrying");

            for (const ScanJob& job : scanJobs)
            {
                queuePlayerEdit(job.instance, job.cacheEntryId, [](Player& player)
                    {
                        // A failed periodic read is not evidence that equipment
                        // disappeared. Preserve the initialized slot cache and
                        // make the player immediately eligible for another pass.
                        player.lastEquipmentUpdate = {};
                    });
            }

            return;
        }

        //create result copies outside the player-cache lock
        std::vector<ScanResult> scanResults;
        scanResults.reserve(scanJobs.size());

        for (ScanJob& job : scanJobs)
        {
            ScanResult result{};

            result.instance = job.instance;
            result.cacheEntryId = job.cacheEntryId;
            result.isPmc = job.isPmc;
            result.dogTagApiLookupAttempted = job.dogTagApiLookupAttempted;
            result.dogTagDataComplete = job.dogTagDataComplete;
            result.dogTagProfileId = job.dogTagProfileId;
            result.accountId = job.accountId;
            result.nickname = job.nickname;
            result.lvl = job.lvl;
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

            if (!Utils::valid_pointer(read.containedItem))
            {
                const bool hasLastGoodItem = !slot.equipName.empty() || slot.price > 0 || slot.wanted;
                if (hasLastGoodItem && slot.consecutiveEmptyReads < 1)
                {
                    ++slot.consecutiveEmptyReads;
                    continue;
                }

                slot.wanted = false;
                slot.price = 0;
                slot.equipName.clear();
                slot.consecutiveEmptyReads = 0;
                continue;
            }

            slot.consecutiveEmptyReads = 0;

            // Live-player dogtag data is read from the known equipment slot.
            if (job.isPlayer && !result.dogTagDataComplete && TrimEFT(slot.name) == "Dogtag")
            {
                uint64_t dogtag = 0;
                PlayerMemoryAccess::tryReadPointer(read.containedItem + sdk::BarterOtherOffsets::Dogtag, dogtag, DmaCacheMode::Uncached);

                if (Utils::valid_pointer(dogtag))
                {
                    const std::string profileId = TrimEFT(mem.readUnityStringField(
                        dogtag + sdk::DogtagComponent::ProfileId, 256, DmaCacheMode::Uncached));
                    const std::string accountId = TrimEFT(mem.readUnityStringField(
                        dogtag + sdk::DogtagComponent::AccountId, 256, DmaCacheMode::Uncached));
                    const std::string nickname = TrimEFT(mem.readUnityStringField(
                        dogtag + sdk::DogtagComponent::Nickname, 256, DmaCacheMode::Uncached));
                    const int level = mem.Read<int>(dogtag + sdk::DogtagComponent::Level);

                    if (!profileId.empty())
                    {
                        result.hasProfileUpdate = true;
                        result.dogTagProfileId = profileId;
                    }

                    if (!accountId.empty())
                        result.accountId = accountId;
                    if (!nickname.empty())
                        result.nickname = nickname;
                    if (level > 0)
                        result.lvl = level;

                    result.hasDogTagDataUpdate = !accountId.empty() || !nickname.empty() || level > 0;
                    result.dogTagDataComplete = !result.dogTagProfileId.empty() && !result.accountId.empty() &&
                        !result.nickname.empty() && result.lvl > 0;
                }
            }

            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            const std::string id =
                TrimEFT(PlayerMemoryAccess::readString(read.mongoId._stringId));

            if (id.empty())
                continue;

            slot.wanted = false;
            slot.price = 0;
            slot.equipName.clear();

            for (const auto& marketItem : marketList)
            {
                if (marketItem.bsgid != id)
                    continue;

                slot.equipName =
                    marketItem.shortName;

                slot.price = static_cast<int>(GetLootSelectedPrice(
                    marketItem.marketPrice,
                    marketItem.traderPrice));

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

        for (size_t index = 0; index < scanResults.size(); ++index)
        {
            ScanResult& result = scanResults[index];
            const ScanJob& job = scanJobs[index];

            if (!job.isPlayer || result.dogTagProfileId.empty())
                continue;

            auto apiResult = getCachedDogTagApiResult(result.dogTagProfileId);

            if (apiResult)
            {
                result.dogTagApiLookupAttempted = true;
            }
            else if (!result.dogTagApiLookupAttempted && g_DogTagAPI.hasApiKey())
            {
                result.dogTagApiLookupAttempted = true;

                if (tryBeginDogTagApiRequest(result.dogTagProfileId))
                {
                    const auto fetchedResult = g_DogTagAPI.getByProfile(result.dogTagProfileId);

                    if (fetchedResult)
                    {
                        DogTagApiResult cachedResult{};
                        cachedResult.accountId = fetchedResult->accountId;
                        cachedResult.nickname = fetchedResult->nickname;
                        cachedResult.level = fetchedResult->lvl;
                        cacheDogTagApiResult(result.dogTagProfileId, cachedResult);
                        apiResult = std::move(cachedResult);
                    }
                }
            }

            if (!apiResult)
                continue;

            result.hasDogTagDataUpdate = true;
            if (!apiResult->accountId.empty())
                result.accountId = apiResult->accountId;
            if (!apiResult->nickname.empty())
                result.nickname = apiResult->nickname;
            if (apiResult->level > 0)
                result.lvl = apiResult->level;
            result.dogTagDataComplete = !result.dogTagProfileId.empty() && !result.accountId.empty() &&
                !result.nickname.empty() && result.lvl > 0;
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

        // Queue updated contents for the live player owner.
        {
            for (ScanResult& result : scanResults)
            {
                if (result.hasProfileUpdate || result.hasDogTagDataUpdate)
                {
                    const auto source = std::find_if(scanSnapshot->begin(), scanSnapshot->end(), [&](const Player& player)
                    {
                            return player.instance == result.instance;
                        });

                    if (source != scanSnapshot->end())
                    {
                        Player updatedPlayer = *source;
                        if (!result.dogTagProfileId.empty())
                        {
                            updatedPlayer.equipmentDogTagProfileId = result.dogTagProfileId;
                            if (updatedPlayer.profileId.empty())
                                updatedPlayer.profileId = result.dogTagProfileId;
                        }

                        if (!result.accountId.empty())
                            updatedPlayer.accountId = result.accountId;
                        if (!result.nickname.empty())
                            updatedPlayer.name = result.nickname;
                        if (result.lvl > 0)
                            updatedPlayer.DT_lvl = result.lvl;

                        watchListManager.logUpdatePlayerPID(updatedPlayer);
                    }
                }

                const uint64_t instance = result.instance;
                const uint64_t cacheEntryId = result.cacheEntryId;
                queuePlayerEdit(instance, cacheEntryId, [result = std::move(result)](Player& player) mutable
                    {
                        if (player.isBTR || player.isDead || player.hasExfiled)
                            return;

                        if (player._slots != result.slots || player.playerValue != result.playerValue)
                        {
                            player._slots = std::move(result.slots);
                            player.playerValue = result.playerValue;
                        }
                        player.lastEquipmentUpdate = result.updateTime;
                        player.dogTagApiLookupAttempted = result.dogTagApiLookupAttempted;
                        player.equipmentDogTagDataComplete = result.dogTagDataComplete;

                        if (!result.dogTagProfileId.empty())
                        {
                            player.equipmentDogTagProfileId = result.dogTagProfileId;
                            if (player.profileId.empty())
                                player.profileId = result.dogTagProfileId;
                        }

                        if (result.hasDogTagDataUpdate && !result.accountId.empty())
                        {
                            player.equipmentDogTagAccountId = result.accountId;
                            player.accountId = std::move(result.accountId);
                        }
                        if (result.hasDogTagDataUpdate && !result.nickname.empty())
                        {
                            player.equipmentDogTagNickname = result.nickname;
                            player.name = std::move(result.nickname);
                        }
                        if (result.hasDogTagDataUpdate && result.lvl > 0)
                        {
                            player.equipmentDogTagLevel = result.lvl;
                            player.DT_lvl = result.lvl;
                        }
                    });
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
