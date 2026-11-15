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

    static constexpr size_t kMaxEquipmentInitPerPass = 2;
    static constexpr size_t kMaxEquipmentScanPerPass = 1;

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
        bool success = false;
    };

    struct ScanJob
    {
        uint64_t instance = 0;
        uint64_t cacheEntryId = 0;
        bool isPlayer = false;
        bool isPmc = false;
        bool dogTagApiLookupAttempted = false;
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
        uint64_t cacheEntryId = 0;
        bool isPmc = false;
        SlotVec slots;
        PlayerValueT playerValue{};
        Clock::time_point updateTime{};

        bool hasProfileUpdate = false;
        bool hasDogTagDataUpdate = false;
        bool dogTagApiLookupAttempted = false;
        std::string profileId;
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

            return batch.ExecuteNoPaging();
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

            initCandidates.emplace_back(std::move(job));
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

                int slotCount = 0;
                ScatterReadBatch slotCountBatch(mem, DmaCacheMode::Uncached, "Player equipment slot count");
                if (!slotCountBatch.Add(job.slotsPtr + OffsetsArray::CountOffset, slotCount) ||
                    !slotCountBatch.ExecuteNoPaging() || slotCount < 0 || slotCount > 128)
                    continue;

                std::vector<uint64_t> slotPointers(static_cast<size_t>(slotCount), 0);
                if (!slotPointers.empty())
                {
                    ScatterReadBatch slotsBatch(mem, DmaCacheMode::Uncached, "Player equipment slots");
                    if (!slotsBatch.AddBytes(job.slotsPtr + OffsetsArray::ArrBaseOffset, slotPointers.data(), slotPointers.size() * sizeof(uint64_t)) ||
                        !slotsBatch.ExecuteNoPaging())
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

                if (queuedStrings && !stringBatch.ExecuteNoPaging())
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

                result.success = true;
                initResults.emplace_back(std::move(result));
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

        // Stage 3: Build normal contents-update candidates
        // Each cached player is due every 10 seconds
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

            if (now - player.lastEquipmentUpdate <
                player.equipmentUpdateInterval)
            {
                continue;
            }

            ScanJob job{};
            job.instance = player.instance;
            job.cacheEntryId = player.cacheEntryId;
            job.isPlayer = player.isPlayer;
            job.isPmc = player.isPlayer && !player.isPlayerScav && !player.isAi;
            job.dogTagApiLookupAttempted = player.dogTagApiLookupAttempted;
            job.profileId = player.profileId;
            job.slots = player._slots;
            job.updateTime = now;

            scanCandidates.emplace_back(std::move(job));
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

                if (job.isPmc &&
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
            logFailure("[PLAYER][EQUIP] Slot-content scatter failed; rebuilding that player's slot cache");

            for (const ScanJob& job : scanJobs)
            {
                queuePlayerEdit(job.instance, job.cacheEntryId, [](Player& player)
                    {
                        player._slots.clear();
                        player.equipInited = false;
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
            result.profileId = job.profileId;
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
                uint64_t classNamePointer = 0;
                const std::string className = PlayerMemoryAccess::tryReadChain(
                    read.containedItem,
                    { 0x0, 0x10 },
                    classNamePointer,
                    DmaCacheMode::Uncached)
                    ? PlayerMemoryAccess::readUtf8(classNamePointer, 128, DmaCacheMode::Uncached)
                    : std::string{};

                if (className == "BarterOther")
                {
                    uint64_t dogtag = 0;
                    PlayerMemoryAccess::tryReadPointer(
                        read.containedItem + sdk::BarterOtherOffsets::Dogtag,
                        dogtag,
                        DmaCacheMode::Uncached);

                    if (Utils::valid_pointer(dogtag))
                    {
                        const uint64_t profileIdPtr =
                            dogtag +
                            sdk::DogtagComponent::ProfileId;

                        if (Utils::valid_pointer(profileIdPtr))
                        {
                            uint64_t profileString = 0;
                            const std::string readString = PlayerMemoryAccess::tryReadPointer(
                                profileIdPtr,
                                profileString,
                                DmaCacheMode::Uncached)
                                ? PlayerMemoryAccess::readString(profileString, 256)
                                : std::string{};

                            if (!readString.empty())
                            {
                                result.hasProfileUpdate = true;
                                result.profileId = readString;
                            }
                        }
                    }
                }
            }

            if (!Utils::valid_pointer(read.itemTemplate))
                continue;

            const std::string id =
                TrimEFT(PlayerMemoryAccess::readString(read.mongoId._stringId));

            if (id.empty())
                continue;

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

            if (!job.isPlayer || result.profileId.empty())
                continue;

            auto apiResult = getCachedDogTagApiResult(result.profileId);

            if (apiResult)
            {
                result.dogTagApiLookupAttempted = true;
            }
            else if (!result.dogTagApiLookupAttempted && g_DogTagAPI.hasApiKey())
            {
                result.dogTagApiLookupAttempted = true;

                if (tryBeginDogTagApiRequest(result.profileId))
                {
                    const auto fetchedResult = g_DogTagAPI.getByProfile(result.profileId);

                    if (fetchedResult)
                    {
                        DogTagApiResult cachedResult{};
                        cachedResult.accountId = fetchedResult->accountId;
                        cachedResult.nickname = fetchedResult->nickname;
                        cachedResult.level = fetchedResult->lvl;
                        cacheDogTagApiResult(result.profileId, cachedResult);
                        apiResult = std::move(cachedResult);
                    }
                }
            }

            if (!apiResult)
                continue;

            result.hasDogTagDataUpdate = true;
            result.accountId = apiResult->accountId;
            result.nickname = apiResult->nickname;
            result.lvl = apiResult->level;
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
                    (result.isPmc && slotName == "Scabbard"))
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
                        if (result.hasProfileUpdate && updatedPlayer.profileId.empty())
                            updatedPlayer.profileId = result.profileId;

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

                        player._slots = std::move(result.slots);
                        player.playerValue = result.playerValue;
                        player.lastEquipmentUpdate = result.updateTime;
                        player.dogTagApiLookupAttempted = result.dogTagApiLookupAttempted;

                        if (result.hasProfileUpdate && player.profileId.empty())
                            player.profileId = std::move(result.profileId);

                        if (result.hasDogTagDataUpdate && !result.accountId.empty())
                            player.accountId = std::move(result.accountId);
                        if (result.hasDogTagDataUpdate && !result.nickname.empty())
                            player.name = std::move(result.nickname);
                        if (result.hasDogTagDataUpdate && result.lvl > 0)
                            player.DT_lvl = result.lvl;
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
