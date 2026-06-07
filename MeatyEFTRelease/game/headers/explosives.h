#pragma once
#include <cstdint>
#include <unordered_set>
#include <iostream>
#include <algorithm>
#include "../game/headers/transform.h"

#include <chrono>
#include <mutex>

enum class ExplosiveType : std::uint8_t
{
    Grenade = 0,

};

struct GrenadeList
{
    ExplosiveType type = ExplosiveType::Grenade;

    std::uint64_t instance = 0;
    std::uint64_t transformInternal = 0;

    glm::vec3 worldLocation{ 0.0f, 0.0f, 0.0f };

    bool isDestroyed = false;
};

class ExplosiveManager
{
public:
    ExplosiveManager() = default;
    ~ExplosiveManager() = default;

    ExplosiveManager(const ExplosiveManager&) = delete;
    ExplosiveManager& operator=(const ExplosiveManager&) = delete;

    void initManager();

    // Called when leaving the raid.
    void reset();

    [[nodiscard]] std::vector<GrenadeList> getGrenades() const;
    [[nodiscard]] std::size_t getGrenadeCount() const;

    // Debug
    [[nodiscard]] std::uint64_t getLocalGameWorld() const;
    [[nodiscard]] std::uint64_t getGrenadesController() const;
    [[nodiscard]] std::uint64_t getGrenadesListPointer() const;
    [[nodiscard]] std::size_t getLastUnityListCount() const;
    [[nodiscard]] bool lastUnityListReadSucceeded() const;

private:
    bool initManagerUnlocked(std::uint64_t localGameWorld);
    bool refreshPointersUnlocked();

    bool readGrenadeAddressesUnlocked(
        std::vector<std::uint64_t>& addresses);

    void refreshGrenadesUnlocked();
    void resetUnlocked();

    static bool positionLooksValid(const glm::vec3& position);

    static GrenadeList* findGrenade(
        std::vector<GrenadeList>& grenades,
        std::uint64_t instance);

private:
    // Stops two refresh operations from running at the same time.
    mutable std::mutex m_refreshMutex;

    // Protects the published grenade cache.
    mutable std::mutex m_cacheMutex;

    std::uint64_t m_localGameWorld = 0;

    // localGameWorld + ClientLocalGameWorld::Grenades
    std::uint64_t m_grenadesController = 0;

    // grenadesController + 0x18
    std::uint64_t m_grenadesListPointer = 0;

    std::size_t m_lastUnityListCount = 0;
    bool m_lastUnityListReadSucceeded = false;

    std::vector<GrenadeList> m_grenades;
};

extern ExplosiveManager explosiveManager;