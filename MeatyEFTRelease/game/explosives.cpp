#include "headers/explosives.h"

#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"


#include "../memory/memory.h"
#include "../game/headers/sdk.h"


ExplosiveManager explosiveManager;

namespace
{
    constexpr std::uint64_t GrenadesListOffset = 0x18;
    constexpr std::size_t MaxReasonableGrenades = 512;
}

void ExplosiveManager::initManager()
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return;

    m_localGameWorld = mainGame.localGameWorld;

    refreshGrenadesUnlocked();
}

void ExplosiveManager::reset()
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    resetUnlocked();
}

std::vector<GrenadeList> ExplosiveManager::getGrenades() const
{
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

    return m_grenades;
}

std::size_t ExplosiveManager::getGrenadeCount() const
{
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

    return m_grenades.size();
}

std::uint64_t ExplosiveManager::getLocalGameWorld() const
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    return m_localGameWorld;
}

std::uint64_t ExplosiveManager::getGrenadesController() const
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    return m_grenadesController;
}

std::uint64_t ExplosiveManager::getGrenadesListPointer() const
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    return m_grenadesListPointer;
}

std::size_t ExplosiveManager::getLastUnityListCount() const
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    return m_lastUnityListCount;
}

bool ExplosiveManager::lastUnityListReadSucceeded() const
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    return m_lastUnityListReadSucceeded;
}

bool ExplosiveManager::refreshPointersUnlocked()
{
    if (!Utils::valid_pointer(m_localGameWorld))
        return false;

    const std::uint64_t grenadesController =
        mem.Read<std::uint64_t>(
            m_localGameWorld +
            sdk::ClientLocalGameWorld::Grenades);

    if (!Utils::valid_pointer(grenadesController))
    {
        m_grenadesController = 0;
        m_grenadesListPointer = 0;

        return false;
    }

    const std::uint64_t grenadesListPointer =
        mem.Read<std::uint64_t>(
            grenadesController + GrenadesListOffset);

    if (!Utils::valid_pointer(grenadesListPointer))
    {
        m_grenadesController = grenadesController;
        m_grenadesListPointer = 0;

        return false;
    }

    m_grenadesController = grenadesController;
    m_grenadesListPointer = grenadesListPointer;

    return true;
}

bool ExplosiveManager::readGrenadeAddressesUnlocked(
    std::vector<std::uint64_t>& addresses)
{
    addresses.clear();

    m_lastUnityListCount = 0;
    m_lastUnityListReadSucceeded = false;

    if (!refreshPointersUnlocked())
        return false;

    try
    {
        
        auto allGrenades =
            UnityList<std::uint64_t>::Create(m_grenadesListPointer);

        std::unordered_set<std::uint64_t> uniqueAddresses;
        uniqueAddresses.reserve(allGrenades.count());

        for (const std::uint64_t grenadeAddress : allGrenades)
        {
            if (!Utils::valid_pointer(grenadeAddress))
                continue;

            uniqueAddresses.insert(grenadeAddress);

            if (uniqueAddresses.size() > MaxReasonableGrenades)
                return false;
        }

        addresses.reserve(uniqueAddresses.size());

        for (const std::uint64_t grenadeAddress : uniqueAddresses)
        {
            addresses.push_back(grenadeAddress);
        }

        m_lastUnityListCount = addresses.size();
        m_lastUnityListReadSucceeded = true;

        return true;
    }
    catch (...)
    {
        m_grenadesListPointer = 0;

        return false;
    }
}

void ExplosiveManager::refreshGrenadesUnlocked()
{
    std::vector<std::uint64_t> liveAddresses;

    if (!readGrenadeAddressesUnlocked(liveAddresses))
        return;

    const std::unordered_set<std::uint64_t> liveSet(
        liveAddresses.begin(),
        liveAddresses.end());

    std::vector<GrenadeList> workingGrenades;

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        workingGrenades = m_grenades;
    }

    // Remove invalid entries and grenades that are no longer present
    workingGrenades.erase(
        std::remove_if(
            workingGrenades.begin(),
            workingGrenades.end(),
            [&liveSet](const GrenadeList& grenade)
            {
                if (!Utils::valid_pointer(grenade.instance))
                    return true;

                return !liveSet.contains(grenade.instance);
            }),
        workingGrenades.end());

    workingGrenades.reserve(liveAddresses.size());

    //Discover grenades
    for (const std::uint64_t grenadeAddress : liveAddresses)
    {
        if (!Utils::valid_pointer(grenadeAddress))
            continue;

        if (findGrenade(workingGrenades, grenadeAddress) != nullptr)
            continue;

        // Avoid an already-destroyed grenade.
        const bool isDestroyed =
            mem.Read<std::uint8_t>(
                grenadeAddress +
                sdk::Throwable::_isDestroyed) != 0;

        if (isDestroyed)
            continue;

        const std::uint64_t transformInternal =
            mem.ReadChain(
                grenadeAddress,
                TransformChain);

        if (!Utils::valid_pointer(transformInternal))
            continue;

        GrenadeList grenade{};

        grenade.type = ExplosiveType::Grenade;
        grenade.instance = grenadeAddress;
        grenade.transformInternal = transformInternal;
        grenade.isDestroyed = false;

        workingGrenades.emplace_back(std::move(grenade));
    }

    // Queue one IsDestroyed read
    std::vector<std::uint8_t> destroyedResults(
        workingGrenades.size(),
        0);

    bool destroyedScatterRan = false;

    if (!workingGrenades.empty())
    {
        auto scatterHandle = mem.CreateScatterHandle();

        if (scatterHandle)
        {
            for (std::size_t i = 0;
                i < workingGrenades.size();
                ++i)
            {
                mem.AddScatterReadRequest(
                    scatterHandle,
                    workingGrenades[i].instance +
                    sdk::Throwable::_isDestroyed,
                    &destroyedResults[i],
                    sizeof(destroyedResults[i]));
            }

            mem.ExecuteReadScatter(scatterHandle);

            destroyedScatterRan = true;

            mem.CloseScatterHandle(scatterHandle);
        }
    }

    // Apply destroyed-state results and update live positions.
    for (std::size_t i = 0;
        i < workingGrenades.size();
        ++i)
    {
        GrenadeList& grenade = workingGrenades[i];

        if (destroyedScatterRan)
        {
            grenade.isDestroyed =
                destroyedResults[i] != 0;
        }
        else
        {
            // Fallback
            grenade.isDestroyed =
                mem.Read<std::uint8_t>(
                    grenade.instance +
                    sdk::Throwable::_isDestroyed) != 0;
        }

        if (grenade.isDestroyed)
            continue;

        // Retry transform resolution when it has not yetresolved or has become invalid.
        if (!Utils::valid_pointer(grenade.transformInternal))
        {
            grenade.transformInternal =
                mem.ReadChain(
                    grenade.instance,
                    TransformChain);
        }

        if (!Utils::valid_pointer(grenade.transformInternal))
            continue;

        UnityTransform transform(grenade.transformInternal);

        const glm::vec3 newPosition =
            transform.UpdatePosition();

        if (positionLooksValid(newPosition))
        {
            grenade.worldLocation = newPosition;
        }
    }

    // Remove marked destroyed
    workingGrenades.erase(
        std::remove_if(
            workingGrenades.begin(),
            workingGrenades.end(),
            [](const GrenadeList& grenade)
            {
                return grenade.isDestroyed;
            }),
        workingGrenades.end());

    // Publish
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_grenades = std::move(workingGrenades);
    }
}

void ExplosiveManager::resetUnlocked()
{
    m_localGameWorld = 0;

    m_grenadesController = 0;
    m_grenadesListPointer = 0;

    m_lastUnityListCount = 0;
    m_lastUnityListReadSucceeded = false;

    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    m_grenades.clear();
}

bool ExplosiveManager::positionLooksValid(
    const glm::vec3& position)
{
    if (!std::isfinite(position.x) ||
        !std::isfinite(position.y) ||
        !std::isfinite(position.z))
    {
        return false;
    }

    constexpr float MaxWorldCoordinate = 100000.0f;

    if (std::fabs(position.x) > MaxWorldCoordinate ||
        std::fabs(position.y) > MaxWorldCoordinate ||
        std::fabs(position.z) > MaxWorldCoordinate)
    {
        return false;
    }

    constexpr float Epsilon = 0.0001f;

    return
        std::fabs(position.x) > Epsilon ||
        std::fabs(position.y) > Epsilon ||
        std::fabs(position.z) > Epsilon;
}

GrenadeList* ExplosiveManager::findGrenade(
    std::vector<GrenadeList>& grenades,
    const std::uint64_t instance)
{
    const auto it =
        std::find_if(
            grenades.begin(),
            grenades.end(),
            [instance](const GrenadeList& grenade)
            {
                return grenade.instance == instance;
            });

    if (it == grenades.end())
        return nullptr;

    return &(*it);
}