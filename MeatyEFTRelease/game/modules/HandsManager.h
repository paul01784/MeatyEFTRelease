// HandsManager.h
#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include "../headers/sdk.h"
#include "../headers/ballistics.h"
#include "../headers/utils.h"
#include "../../memory/Memory.h"

//reference only 
struct PlayerCache;

struct HandsInfo
{
    uint64_t cachedItem = 0;
    uint64_t cachedItemTemplate = 0;
    uint64_t loadedAmmoTemplate = 0;
    bool cachedIsWeapon = false;
    int weaponVersion = -1;
    BallisticsInfo ballistics{};
    std::chrono::steady_clock::time_point nextBallisticsRefresh{};
    float weaponVelocityModifier = 0.0f;
    bool weaponVelocityModifierValid = false;
    std::chrono::steady_clock::time_point nextVelocityModifierRefresh{};

    // Item in hand info
    uint64_t itemPtr = 0;
    std::string itemName;
    std::string ammoName;

    int chamberCount = 0;
    int magazineCount = 0;

    void reset()
    {
        itemPtr = 0;
        cachedItem = 0;
        cachedItemTemplate = 0;
        loadedAmmoTemplate = 0;
        cachedIsWeapon = false;
        weaponVersion = -1;
        ballistics = {};
        nextBallisticsRefresh = {};
        weaponVelocityModifier = 0.0f;
        weaponVelocityModifierValid = false;
        nextVelocityModifierRefresh = {};

        itemName.clear();
        ammoName.clear();

        chamberCount = 0;
        magazineCount = 0;
    }

    bool update(const PlayerCache& playerCache);

    
};

struct Chamber
{
private:
    uint64_t _base = 0;

public:
    Chamber() = default;

    explicit Chamber(uint64_t base)
        : _base(base)
    {
    }

    operator uint64_t() const
    {
        return _base;
    }

    bool IsValid() const
    {
        return Utils::valid_pointer(_base);
    }

    bool HasBullet(bool useCache = false) const
    {
        if (!Utils::valid_pointer(_base))
            return false;

        uint64_t containedItem = 0;

        if (!mem.TryRead<uint64_t>(_base + sdk::Slot::ContainedItem, containedItem))
            return false;

        return Utils::valid_pointer(containedItem);
    }
};
