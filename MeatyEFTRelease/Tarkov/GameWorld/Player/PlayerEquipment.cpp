#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerLookup.h"

#include "../../../UI/DogTagAPI.h"
#include "../../../UI/debug.h"
#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "DogTagCache.h"
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

            std::vector<Player>& cache =
                registeredPlayers.getCache();

            initCandidates.reserve(cache.size());

            for (const Player& player : cache)
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

            std::vector<Player>& cache =
                registeredPlayers.getCache();

            for (InitResult& result : initResults)
            {
                Player* player =
                    PlayerLookup::findByInstance(
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

            std::vector<Player>& cache =
                registeredPlayers.getCache();

            scanCandidates.reserve(cache.size());

            const Clock::time_point now = Clock::now();

            for (const Player& player : cache)
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

            std::vector<Player>& cache =
                registeredPlayers.getCache();

            for (ScanResult& result : scanResults)
            {
                Player* player =
                    PlayerLookup::findByInstance(
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
