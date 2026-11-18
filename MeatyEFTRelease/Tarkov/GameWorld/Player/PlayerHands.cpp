#include "../../../UI/includes.h"
#include "../RegisteredPlayers.h"
#include "PlayerClassifier.h"

#include "../../../UI/globals.h"
#include "../../../memory/Memory.h"
#include "../../../memory/ScatterReadBatch.h"
#include "PlayerMemoryAccess.h"
#include "../MainGame.h"
#include "../../../Core/Utilities.h"
#include "../../Unity/UnityContainers.h"
#include "../../Unity/UnityOffsets.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"
#include "../Loot/Loot.h"

#include <algorithm>
#include <cmath>
#include <vector>

template <typename T>
static bool TryReadItemArray(
    uint64_t arrayPointer,
    int maximumCount,
    std::vector<T>& output)
{
    output.clear();

    int count = 0;
    if (!Utils::valid_pointer(arrayPointer) ||
        !PlayerMemoryAccess::tryRead(
            arrayPointer + OffsetsArray::CountOffset,
            count) ||
        count <= 0 ||
        count > maximumCount)
    {
        return false;
    }

    output.resize(static_cast<size_t>(count));
    ScatterReadBatch batch(mem, DmaCacheMode::Cached, "Player item array");
    if (!batch.AddBytes(
            arrayPointer + OffsetsArray::ArrBaseOffset,
            output.data(),
            output.size() * sizeof(T)) ||
        !batch.TryExecuteNoPaging())
    {
        output.clear();
        return false;
    }

    return true;
}

template <typename T>
static bool TryReadItemList(
    uint64_t listPointer,
    int maximumCount,
    std::vector<T>& output,
    DmaCacheMode cacheMode = DmaCacheMode::Uncached)
{
    output.clear();

    int count = 0;
    uint64_t arrayPointer = 0;
    ScatterReadBatch header(mem, cacheMode, "Player item list header");
    if (!Utils::valid_pointer(listPointer) ||
        !header.Add(listPointer + UnityList<T>::CountOffset, count) ||
        !header.Add(listPointer + UnityList<T>::ArrOffset, arrayPointer) ||
        !header.TryExecuteNoPaging() ||
        count < 0 ||
        count > maximumCount ||
        !Utils::valid_pointer(arrayPointer))
    {
        return false;
    }

    if (count == 0)
        return true;

    output.resize(static_cast<size_t>(count));
    ScatterReadBatch entries(mem, cacheMode, "Player item list");
    if (!entries.AddBytes(
            arrayPointer + UnityList<T>::ArrStartOffset,
            output.data(),
            output.size() * sizeof(T)) ||
        !entries.TryExecuteNoPaging())
    {
        output.clear();
        return false;
    }

    return true;
}

static inline bool TryReadAmmoTemplateFromRound(uint64_t roundPtr, uint64_t& ammoTemplate)
{
    ammoTemplate = 0;

    if (!Utils::valid_pointer(roundPtr))
        return false;

    if (!PlayerMemoryAccess::tryRead(roundPtr + sdk::LootItem::Template, ammoTemplate))
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

    std::vector<Chamber> chambers;
    if (!TryReadItemArray(chambersPtr, 64, chambers))
        return false;

    maxAmmoCount += static_cast<int>(chambers.size());

    for (const Chamber chamber : chambers)
    {
        const uint64_t chamberPtr = static_cast<uint64_t>(chamber);
        uint64_t containedItem = 0;
        if (!Utils::valid_pointer(chamberPtr))
            return false;

        if (!PlayerMemoryAccess::tryRead(
                chamberPtr + sdk::Slot::ContainedItem,
                containedItem,
                DmaCacheMode::Uncached))
        {
            return false;
        }

        if (!Utils::valid_pointer(containedItem))
            continue;

        ++currentAmmoCount;
        if (!Utils::valid_pointer(firstRound))
            firstRound = containedItem;
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

    if (!PlayerMemoryAccess::tryRead(
            itemBase + sdk::Weapon::Chambers,
            chambersPtr,
            DmaCacheMode::Uncached))
    {
        return false;
    }

    if (Utils::valid_pointer(chambersPtr))
    {
        if (!CountLoadedChamberArray(
                chambersPtr,
                firstRound,
                currentAmmoCount,
                maxAmmoCount))
        {
            return false;
        }
    }

    // ----------------------------------------------------
    // 2. Magazine path
    // ----------------------------------------------------
    uint64_t magSlot = 0;
    uint64_t magItemPtr = 0;

    if (!PlayerMemoryAccess::tryRead(
            itemBase + sdk::Weapon::MagazineSlotCache,
            magSlot,
            DmaCacheMode::Uncached) ||
        !Utils::valid_pointer(magSlot))
    {
        return false;
    }

    if (!PlayerMemoryAccess::tryRead(
            magSlot + sdk::Slot::ContainedItem,
            magItemPtr,
            DmaCacheMode::Uncached) ||
        !Utils::valid_pointer(magItemPtr))
    {
        return false;
    }

    // Read normal magazine cartridges first. Magazine items can also expose
    // attachment slots, so treating any slot array as a cylinder loses the
    // regular stack and breaks ammunition for ordinary weapons.
    uint64_t cartridges = 0;
    if (!PlayerMemoryAccess::tryRead(
            magItemPtr + sdk::Magazine::Cartridges,
            cartridges,
            DmaCacheMode::Uncached))
    {
        return false;
    }

    if (Utils::valid_pointer(cartridges))
    {
        uint64_t magStackPtr = 0;
        int magMaxCount = 0;
        if (!PlayerMemoryAccess::tryRead(
                cartridges + sdk::StackSlot::items,
                magStackPtr,
                DmaCacheMode::Uncached) ||
            !Utils::valid_pointer(magStackPtr))
        {
            return false;
        }

        if (!PlayerMemoryAccess::tryRead(
                cartridges + sdk::StackSlot::MaxCount,
                magMaxCount))
        {
            return false;
        }

        maxAmmoCount += std::max(0, magMaxCount);

        std::vector<uint64_t> magStack;
        if (!TryReadItemList(
                magStackPtr,
                512,
                magStack,
                DmaCacheMode::Uncached))
        {
            return false;
        }

        for (const auto& stack : magStack)
        {
            if (!Utils::valid_pointer(stack))
                return false;

            int stackNumber = 0;

            if (!PlayerMemoryAccess::tryRead(
                    stack + sdk::Item::StackObjectsCount,
                    stackNumber,
                    DmaCacheMode::Uncached))
            {
                return false;
            }

            if (stackNumber < 0)
                return false;

            currentAmmoCount += stackNumber;

            // If no chamber round was found, use the first mag round for ammo type
            if (!Utils::valid_pointer(firstRound))
                firstRound = stack;
        }

        if (currentAmmoCount > maxAmmoCount)
            return false;

        chamberCount = currentAmmoCount;
        magazineCount = maxAmmoCount;
        return TryReadAmmoTemplateFromRound(firstRound, ammoTemplate);
    }

    // Cylinder-style magazines do not expose a regular cartridge stack.
    // Only use their chamber slots when the normal cartridge path is absent.
    uint64_t magChambersPtr = 0;
    if (!PlayerMemoryAccess::tryRead(
            magItemPtr + sdk::LootItemMod::Slots,
            magChambersPtr,
            DmaCacheMode::Uncached) ||
        !Utils::valid_pointer(magChambersPtr) ||
        !CountLoadedChamberArray(
            magChambersPtr,
            firstRound,
            currentAmmoCount,
            maxAmmoCount))
    {
        return false;
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
    if (!PlayerMemoryAccess::tryRead(
        itemBase + sdk::LootItemMod::Slots,
        slotsPointer) ||
        !Utils::valid_pointer(slotsPointer))
    {
        return;
    }

    try
    {
        std::vector<uint64_t> slots;
        if (!TryReadItemArray(slotsPointer, 100, slots))
            return;

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

            if (!PlayerMemoryAccess::tryRead(
                slot + sdk::Slot::ContainedItem,
                containedItem) ||
                !Utils::valid_pointer(containedItem) ||
                !PlayerMemoryAccess::tryRead(
                    containedItem + sdk::LootItem::Template,
                    itemTemplate) ||
                !Utils::valid_pointer(itemTemplate))
            {
                continue;
            }

            if (PlayerMemoryAccess::tryRead(
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
        !PlayerMemoryAccess::tryRead(
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

    std::vector<DWORD> bytesRead(std::size(requests), 0);
    if (mem.TryReadScatter(
            requests,
            std::size(requests),
            DmaCacheMode::Cached,
            "Weapon ballistics",
            bytesRead,
            DmaPagingMode::NoPaging,
            DmaLockMode::Try) != Memory::TryScatterReadResult::Success ||
        !std::all_of(bytesRead.begin(), bytesRead.end(), [](DWORD bytes)
        {
            return bytes == sizeof(float);
        }))
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

    if (!PlayerMemoryAccess::tryRead(
            playerCache.P_HandsController + heldItemOffset,
            itemBase,
            DmaCacheMode::Uncached))
        return false;

    if (!Utils::valid_pointer(itemBase))
    {
        reset();
        return true;
    }

    if (itemName == "Unknown")
        reset();

    const bool itemChanged = itemBase != cachedItem;
    const uint64_t marketRevision = marketListRevision.load(std::memory_order_acquire);
    const bool identityChanged = itemChanged || marketRevision != cachedMarketRevision;
    bool isWeapon = cachedIsWeapon;

    if (identityChanged)
    {
        reset();
        cachedMarketRevision = marketRevision;
        isWeapon = false;

        uint64_t itemTemp = 0;

        if (!PlayerMemoryAccess::tryRead(itemBase + sdk::LootItem::Template, itemTemp) ||
            !Utils::valid_pointer(itemTemp))
        {
            return false;
        }

        cachedItemTemplate = itemTemp;

        MongoID mongoId{};

        if (!PlayerMemoryAccess::tryRead(itemTemp + sdk::ItemTemplate::_id, mongoId))
            return false;

        std::string itemId = TrimEFT(PlayerMemoryAccess::readString(mongoId._stringId, 64));

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

            if (PlayerMemoryAccess::tryRead(itemTemp + sdk::ItemTemplate::ShortName, itemNamePointer) &&
                Utils::valid_pointer(itemNamePointer))
            {
                std::string shortNameMem = TrimEFT(
                    PlayerMemoryAccess::readString(itemNamePointer, 32)
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

        if (!isWeapon)
        {
            uint64_t weaponMember = 0;
            isWeapon =
                PlayerMemoryAccess::tryReadPointer(
                    itemBase + sdk::Weapon::MagazineSlotCache,
                    weaponMember,
                    DmaCacheMode::Uncached) ||
                PlayerMemoryAccess::tryReadPointer(
                    itemBase + sdk::Weapon::Chambers,
                    weaponMember,
                    DmaCacheMode::Uncached);
        }

        cachedIsWeapon = isWeapon;
        cachedItem = itemBase;
    }

    // Use cached weapon state after item identity refresh
    isWeapon = cachedIsWeapon;

    int currentWeaponVersion = weaponVersion;
    const bool versionRead = isWeapon &&
        PlayerMemoryAccess::tryRead(
            itemBase + sdk::LootItem::Version,
            currentWeaponVersion,
            DmaCacheMode::Uncached);
    const auto ballisticsNow = std::chrono::steady_clock::now();
    const bool ballisticsRequested =
        playerCache.isLocal && aimGlobals::predictionEnabled;
    const bool velocityModifierRefreshDue =
        ballisticsRequested &&
        (nextVelocityModifierRefresh ==
            std::chrono::steady_clock::time_point{} ||
            ballisticsNow >= nextVelocityModifierRefresh);

    if (isWeapon)
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

            if (gotAmmoTemplate &&
                (!Utils::valid_pointer(ammoTemplate) ||
                    !weaponVelocityModifierValid))
            {
                loadedAmmoTemplate = 0;
                ballistics = {};
            }
            else if (gotAmmoTemplate &&
                (ammoChanged ||
                velocityModifierRefreshed ||
                !ballistics.IsValid()))
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
            }
        }

        
        if (gotAmmoTemplate &&
            Utils::valid_pointer(ammoTemplate) &&
            (ammoTemplate != cachedAmmoNameTemplate || ammoName.empty()))
        {
            std::string resolvedAmmoName;
            MongoID ammoMongoId{};
            if (PlayerMemoryAccess::tryRead(ammoTemplate + sdk::ItemTemplate::_id, ammoMongoId))
            {
                const std::string ammoId = TrimEFT(PlayerMemoryAccess::readString(ammoMongoId._stringId, 64));
                if (!ammoId.empty())
                {
                    for (const auto& ml : marketList)
                    {
                        if (ml.bsgid != ammoId)
                            continue;

                        if (!ml.shortName.empty())
                            resolvedAmmoName = ml.shortName;
                        break;
                    }
                }
            }

            if (resolvedAmmoName.empty())
            {
                uint64_t shortNamePtr = 0;
                if (PlayerMemoryAccess::tryRead(ammoTemplate + sdk::ItemTemplate::ShortName, shortNamePtr) &&
                    Utils::valid_pointer(shortNamePtr))
                {
                    resolvedAmmoName = TrimEFT(PlayerMemoryAccess::readString(shortNamePtr, 32));
                }
            }

            if (!resolvedAmmoName.empty())
            {
                ammoName = std::move(resolvedAmmoName);
                cachedAmmoNameTemplate = ammoTemplate;
            }
        }
    }


    if (!isWeapon)
    {
        chamberCount = 0;
        magazineCount = 0;
        ammoName = "";
        cachedAmmoNameTemplate = 0;
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
