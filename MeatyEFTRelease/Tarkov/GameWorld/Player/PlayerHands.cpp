#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerClassifier.h"

#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../MainGame.h"
#include "../../../Core/Utilities.h"
#include "../../Unity/UnityContainers.h"
#include "../../Unity/UnityOffsets.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"
#include "../Loot/Loot.h"

#include <algorithm>
#include <cmath>

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

bool HandsInfo::update(const Player& playerCache)
{
    if (playerCache.isDead || playerCache.hasExfiled)
    {
        reset();
        return true;
    }

    if (!Utils::valid_pointer(playerCache.P_HandsController))
        return false;

    const uint64_t heldItemOffset = PlayerClassifier::get(playerCache).getHeldItemOffset();
    uint64_t itemBase = 0;

    if (!mem.TryRead<uint64_t>(playerCache.P_HandsController + heldItemOffset, itemBase))
        return false;

    if (!Utils::valid_pointer(itemBase))
    {
        reset();
        return true;
    }

    if (itemName == "Unknown")
        reset();

    bool isWeapon = cachedIsWeapon;
    const bool itemChanged = itemBase != cachedItem;

    if (itemChanged)
    {
        reset();
        isWeapon = false;

        uint64_t itemTemp = 0;

        if (!mem.TryRead<uint64_t>(itemBase + sdk::LootItem::Template, itemTemp) ||
            !Utils::valid_pointer(itemTemp))
        {
            return false;
        }

        cachedItemTemplate = itemTemp;

        MongoID mongoId{};

        if (!mem.TryRead<MongoID>(itemTemp + sdk::ItemTemplate::_id, mongoId))
            return false;

        std::string itemId = TrimEFT(mongoId.ReadString(mem, 64));

        std::string itemMarketName;

        if (!itemId.empty())
        {
            for (const auto& ml : marketList)
            {
                if (ml.bsgid != itemId)
                    continue;

                itemMarketName = ml.shortName;

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
            }
            else
                return false;

            if (itemName.empty())
                return false;

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

        if (itemName.empty() || itemName == "Unknown")
            return false;

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
