#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_set>
#include <iostream>
#include <algorithm>
#include "../../Unity/Transform.h"

#include <chrono>
#include <mutex>

enum class ExplosiveType : std::uint8_t
{
    Grenade = 0,
    Tripwire = 1,
};

struct GrenadeList
{
    ExplosiveType type = ExplosiveType::Grenade;

    std::uint64_t instance = 0;
    std::uint64_t transformInternal = 0;

    glm::vec3 worldLocation{ 0.0f, 0.0f, 0.0f };
    glm::vec3 fromWorldLocation{ 0.0f, 0.0f, 0.0f };

    bool isDestroyed = false;
    bool isActive = false;
};

using GrenadeCacheCollection = std::vector<GrenadeList>;
using GrenadeCacheSnapshot =
    std::shared_ptr<const GrenadeCacheCollection>;

class ExplosiveManager
{
public:
    ExplosiveManager();
    ~ExplosiveManager() = default;

    ExplosiveManager(const ExplosiveManager&) = delete;
    ExplosiveManager& operator=(const ExplosiveManager&) = delete;

    void initManager();

    // Kept separate from grenades so tripwires can be refreshed at a lower
    // latency without increasing the grenade polling rate.
    void refreshTripwires();

    // Called when leaving the raid.
    void reset();

    [[nodiscard]] std::vector<GrenadeList> getGrenades() const;
    [[nodiscard]] GrenadeCacheSnapshot getGrenadesSnapshot() const noexcept;
    [[nodiscard]] std::size_t getGrenadeCount() const;
    [[nodiscard]] std::size_t getTripwireCount() const;

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

    bool readTripwireAddressesUnlocked(
        std::vector<std::uint64_t>& addresses);

    void refreshGrenadesUnlocked();
    void refreshTripwiresUnlocked();
    void resetUnlocked();

    void clearExplosivesOfTypeUnlocked(ExplosiveType type);

    static bool positionLooksValid(const glm::vec3& position);

    static GrenadeList* findGrenade(
        std::vector<GrenadeList>& grenades,
        std::uint64_t instance);

private:
    // Stops two refresh operations from running at the same time.
    mutable std::mutex m_refreshMutex;

    // Protects the published grenade cache.
    mutable std::mutex m_cacheMutex;
    std::atomic<GrenadeCacheSnapshot> m_publishedGrenades;

    std::uint64_t m_localGameWorld = 0;

    // localGameWorld + ClientLocalGameWorld::Grenades
    std::uint64_t m_grenadesController = 0;

    // grenadesController + 0x18
    std::uint64_t m_grenadesListPointer = 0;

    // localGameWorld + GameWorld::SynchronizableObjectLogicProcessor
    std::uint64_t m_synchronizableObjectLogicProcessor = 0;

    // synchronizableObjectLogicProcessor +
    // SynchronizableObjectLogicProcessor::_activeSynchronizableObjects
    std::uint64_t m_synchronizableObjectsListPointer = 0;

    std::size_t m_lastUnityListCount = 0;
    bool m_lastUnityListReadSucceeded = false;

    std::vector<GrenadeList> m_grenades;

    void publishGrenadesLocked();
};

extern ExplosiveManager explosiveManager;
