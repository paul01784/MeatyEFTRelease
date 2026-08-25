#include "headers/explosives.h"

#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"
#include "../app/globals.h"


#include "../memory/memory.h"
#include "../memory/ScatterReadBatch.h"
#include "../game/headers/sdk.h"


ExplosiveManager::ExplosiveManager()
    : m_publishedGrenades(
        std::make_shared<const GrenadeCacheCollection>())
{
}

ExplosiveManager explosiveManager;

namespace
{
    constexpr std::uint64_t GrenadesListOffset = 0x18;
    constexpr std::size_t MaxReasonableGrenades = 512;
    constexpr std::size_t MaxReasonableTripwires = 512;

    bool grenadesEnabled() noexcept
    {
        return espGlobals::drawGrenades || radarGlobals::drawGrenades;
    }

    bool tripwiresEnabled() noexcept
    {
        return espGlobals::drawTripwires || radarGlobals::drawTripwires;
    }

    bool isTripwireActive(const std::int32_t state) noexcept
    {
        // ETripwireState.Wait and ETripwireState.Active.
        return state == 1 || state == 2;
    }
}

void ExplosiveManager::initManager()
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    if (!grenadesEnabled())
    {
        clearExplosivesOfTypeUnlocked(ExplosiveType::Grenade);
        return;
    }

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return;

    m_localGameWorld = mainGame.localGameWorld;

    refreshGrenadesUnlocked();
}

void ExplosiveManager::refreshTripwires()
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    if (!tripwiresEnabled())
    {
        clearExplosivesOfTypeUnlocked(ExplosiveType::Tripwire);
        return;
    }

    if (!Utils::valid_pointer(mainGame.localGameWorld))
        return;

    m_localGameWorld = mainGame.localGameWorld;

    refreshTripwiresUnlocked();
}

void ExplosiveManager::reset()
{
    std::lock_guard<std::mutex> refreshLock(m_refreshMutex);

    resetUnlocked();
}

std::vector<GrenadeList> ExplosiveManager::getGrenades() const
{
    return *getGrenadesSnapshot();
}

GrenadeCacheSnapshot ExplosiveManager::getGrenadesSnapshot() const noexcept
{
    GrenadeCacheSnapshot snapshot = m_publishedGrenades.load(std::memory_order_acquire);

    if (snapshot)
        return snapshot;

    static const GrenadeCacheSnapshot emptySnapshot = std::make_shared<const GrenadeCacheCollection>();

    return emptySnapshot;
}

std::size_t ExplosiveManager::getGrenadeCount() const
{
    const GrenadeCacheSnapshot snapshot = getGrenadesSnapshot();

    return static_cast<std::size_t>(std::count_if(
        snapshot->begin(),
        snapshot->end(),
        [](const GrenadeList& explosive)
        {
            return explosive.type == ExplosiveType::Grenade;
        }));
}

std::size_t ExplosiveManager::getTripwireCount() const
{
    const GrenadeCacheSnapshot snapshot = getGrenadesSnapshot();

    return static_cast<std::size_t>(std::count_if(
        snapshot->begin(),
        snapshot->end(),
        [](const GrenadeList& explosive)
        {
            return explosive.type == ExplosiveType::Tripwire;
        }));
}

void ExplosiveManager::publishGrenadesLocked()
{
    m_publishedGrenades.store(
        std::make_shared<const GrenadeCacheCollection>(m_grenades),
        std::memory_order_release);
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
            UnityList<std::uint64_t>::Create(
                m_grenadesListPointer,
                DmaCacheMode::Uncached);

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

bool ExplosiveManager::readTripwireAddressesUnlocked(std::vector<std::uint64_t>& addresses)
{
    addresses.clear();

    if (!Utils::valid_pointer(m_localGameWorld))
        return false;

    try
    {
        const std::uint64_t logicProcessor = mem.Read<std::uint64_t>(m_localGameWorld + sdk::GameWorld::SynchronizableObjectLogicProcessor);

        if (!Utils::valid_pointer(logicProcessor))
        {
            m_synchronizableObjectLogicProcessor = 0;
            m_synchronizableObjectsListPointer = 0;
            return false;
        }

        const std::uint64_t synchronizableObjects = mem.Read<std::uint64_t>(logicProcessor + sdk::SynchronizableObjectLogicProcessor::_activeSynchronizableObjects);

        if (!Utils::valid_pointer(synchronizableObjects))
        {
            m_synchronizableObjectLogicProcessor = logicProcessor;
            m_synchronizableObjectsListPointer = 0;
            return false;
        }

        m_synchronizableObjectLogicProcessor = logicProcessor;
        m_synchronizableObjectsListPointer = synchronizableObjects;

        const auto synchronizableObjectList = UnityList<std::uint64_t>::Create(
            synchronizableObjects,
            DmaCacheMode::Uncached);

        std::vector<std::uint64_t> candidates;
        candidates.reserve(synchronizableObjectList.count());

        for (const std::uint64_t objectAddress : synchronizableObjectList)
        {
            if (Utils::valid_pointer(objectAddress))
                candidates.emplace_back(objectAddress);
        }

        std::vector<std::int32_t> objectTypes(candidates.size(), -1);

        if (!candidates.empty())
        {
            ScatterReadBatch typeReads(
                mem,
                DmaCacheMode::Cached,
                "Tripwire object types");

            for (size_t i = 0; i < candidates.size(); ++i)
            {
                typeReads.Add(
                    candidates[i] + sdk::SynchronizableObject::Type,
                    objectTypes[i]);
            }

            if (!typeReads.Execute())
                return false;
        }

        std::unordered_set<std::uint64_t> uniqueAddresses;
        uniqueAddresses.reserve(synchronizableObjectList.count());

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (objectTypes[i] != 2)
                continue;

            uniqueAddresses.insert(candidates[i]);

            if (uniqueAddresses.size() > MaxReasonableTripwires)
                return false;
        }

        addresses.reserve(uniqueAddresses.size());

        for (const std::uint64_t objectAddress : uniqueAddresses)
            addresses.push_back(objectAddress);

        return true;
    }
    catch (...)
    {
        m_synchronizableObjectsListPointer = 0;
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

    workingGrenades.erase(
        std::remove_if(
            workingGrenades.begin(),
            workingGrenades.end(),
            [&liveSet](const GrenadeList& grenade)
            {
                if (grenade.type != ExplosiveType::Grenade)
                    return false;

                if (!Utils::valid_pointer(grenade.instance))
                    return true;

                return !liveSet.contains(grenade.instance);
            }),
        workingGrenades.end());

    workingGrenades.reserve(workingGrenades.size() + liveAddresses.size());

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
                sdk::Throwable::_isDestroyed,
                DmaCacheMode::Uncached) != 0;

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
        grenade.isActive = true;

        workingGrenades.emplace_back(std::move(grenade));
    }

    // Queue one IsDestroyed read
    std::vector<std::uint8_t> destroyedResults(
        workingGrenades.size(),
        0);

    bool destroyedScatterRan = false;
    bool queuedDestroyedReads = false;

    if (!workingGrenades.empty())
    {
        ScatterReadBatch scatter(
            mem,
            DmaCacheMode::Uncached,
            "Grenade destroyed state"
        );

        if (scatter.Valid())
        {
            for (std::size_t i = 0;
                i < workingGrenades.size();
                ++i)
            {
                if (workingGrenades[i].type != ExplosiveType::Grenade)
                    continue;

                scatter.Add(
                    workingGrenades[i].instance +
                    sdk::Throwable::_isDestroyed,
                    destroyedResults[i]);

                queuedDestroyedReads = true;
            }

            if (queuedDestroyedReads)
                destroyedScatterRan = scatter.Execute();
        }
    }

    // Apply destroyed-state results and update live positions.
    for (std::size_t i = 0;
        i < workingGrenades.size();
        ++i)
    {
        GrenadeList& grenade = workingGrenades[i];

        if (grenade.type != ExplosiveType::Grenade)
            continue;

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
                    sdk::Throwable::_isDestroyed,
                    DmaCacheMode::Uncached) != 0;
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
                return grenade.type == ExplosiveType::Grenade &&
                    grenade.isDestroyed;
            }),
        workingGrenades.end());

    // Publish
    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_grenades = std::move(workingGrenades);
        publishGrenadesLocked();
    }
}

void ExplosiveManager::refreshTripwiresUnlocked()
{
    std::vector<std::uint64_t> liveAddresses;

    if (!readTripwireAddressesUnlocked(liveAddresses))
        return;

    const std::unordered_set<std::uint64_t> liveSet(
        liveAddresses.begin(),
        liveAddresses.end());

    std::vector<GrenadeList> workingExplosives;

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        workingExplosives = m_grenades;
    }

    // A tripwire is no longer usable once its synchronizable object disappears.
    workingExplosives.erase(
        std::remove_if(
            workingExplosives.begin(),
            workingExplosives.end(),
            [&liveSet](const GrenadeList& explosive)
            {
                if (explosive.type != ExplosiveType::Tripwire)
                    return false;

                return !Utils::valid_pointer(explosive.instance) ||
                    !liveSet.contains(explosive.instance);
            }),
        workingExplosives.end());

    workingExplosives.reserve(workingExplosives.size() + liveAddresses.size());

    for (const std::uint64_t tripwireAddress : liveAddresses)
    {
        if (findGrenade(workingExplosives, tripwireAddress) != nullptr)
            continue;

        GrenadeList tripwire{};
        tripwire.type = ExplosiveType::Tripwire;
        tripwire.instance = tripwireAddress;

        workingExplosives.emplace_back(std::move(tripwire));
    }

    struct TripwireLiveRead
    {
        size_t explosiveIndex{};
        std::int32_t state{};
        glm::vec3 toPosition{};
        glm::vec3 fromPosition{};
    };

    std::vector<TripwireLiveRead> reads;
    reads.reserve(workingExplosives.size());

    for (size_t i = 0; i < workingExplosives.size(); ++i)
    {
        if (workingExplosives[i].type != ExplosiveType::Tripwire)
            continue;

        TripwireLiveRead read{};
        read.explosiveIndex = i;
        reads.emplace_back(read);
    }

    if (!reads.empty())
    {
        ScatterReadBatch stateReads(
            mem,
            DmaCacheMode::Uncached,
            "Tripwire states");

        for (TripwireLiveRead& read : reads)
        {
            const GrenadeList& tripwire = workingExplosives[read.explosiveIndex];
            stateReads.Add(
                tripwire.instance + sdk::TripwireSynchronizableObject::_tripwireState,
                read.state);
        }

        if (!stateReads.Execute())
            return;
    }

    bool hasActiveTripwire = false;

    for (TripwireLiveRead& read : reads)
    {
        GrenadeList& tripwire = workingExplosives[read.explosiveIndex];
        tripwire.isActive = isTripwireActive(read.state);
        hasActiveTripwire = hasActiveTripwire || tripwire.isActive;
    }

    if (hasActiveTripwire)
    {
        ScatterReadBatch positionReads(
            mem,
            DmaCacheMode::Uncached,
            "Tripwire positions");

        for (TripwireLiveRead& read : reads)
        {
            const GrenadeList& tripwire = workingExplosives[read.explosiveIndex];

            if (!tripwire.isActive)
                continue;

            positionReads.Add(
                tripwire.instance + sdk::TripwireSynchronizableObject::ToPosition,
                read.toPosition);
            positionReads.Add(
                tripwire.instance + sdk::TripwireSynchronizableObject::FromPosition,
                read.fromPosition);
        }

        if (!positionReads.Execute())
            return;
    }

    for (TripwireLiveRead& read : reads)
    {
        GrenadeList& tripwire = workingExplosives[read.explosiveIndex];

        if (!tripwire.isActive)
            continue;

        // The game stores both anchor points slightly below the visible wire.
        read.toPosition.y += 0.175f;
        read.fromPosition.y += 0.175f;

        if (!positionLooksValid(read.toPosition) ||
            !positionLooksValid(read.fromPosition))
        {
            tripwire.isActive = false;
            continue;
        }

        tripwire.worldLocation = read.toPosition;
        tripwire.fromWorldLocation = read.fromPosition;
    }

    workingExplosives.erase(
        std::remove_if(
            workingExplosives.begin(),
            workingExplosives.end(),
            [](const GrenadeList& explosive)
            {
                return explosive.type == ExplosiveType::Tripwire &&
                    !explosive.isActive;
            }),
        workingExplosives.end());

    {
        std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
        m_grenades = std::move(workingExplosives);
        publishGrenadesLocked();
    }
}

void ExplosiveManager::clearExplosivesOfTypeUnlocked(const ExplosiveType type)
{
    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);

    const auto previousSize = m_grenades.size();

    m_grenades.erase(
        std::remove_if(
            m_grenades.begin(),
            m_grenades.end(),
            [type](const GrenadeList& explosive)
            {
                return explosive.type == type;
            }),
        m_grenades.end());

    if (m_grenades.size() != previousSize)
        publishGrenadesLocked();
}

void ExplosiveManager::resetUnlocked()
{
    m_localGameWorld = 0;

    m_grenadesController = 0;
    m_grenadesListPointer = 0;
    m_synchronizableObjectLogicProcessor = 0;
    m_synchronizableObjectsListPointer = 0;

    m_lastUnityListCount = 0;
    m_lastUnityListReadSucceeded = false;

    std::lock_guard<std::mutex> cacheLock(m_cacheMutex);
    m_grenades.clear();
    publishGrenadesLocked();
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
