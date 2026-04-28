#include "headers/explosives.h"

#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/unityHelper.h"
#include "headers/unitysdk.h"


#include "../memory/memory.h"
#include "../game/headers/sdk.h"


ExplosiveManager explosiveManager;

//init globals
uint64_t ExplosiveManager::grenadesListPtr = NULL;
uint64_t ExplosiveManager::syncObjectorsPtr = NULL;

std::vector<GrenadeList>& ExplosiveManager::getGrenades()
{
	return this->grenadeList;
}

void ExplosiveManager::initManager()
{
    try
    {
        if (!Utils::valid_pointer(mainGame.localGameWorld))
            return;

        std::vector<GrenadeList>& grenadeCache = explosiveManager.getGrenades();

        uint64_t grenades = mem.Read<uint64_t>(
            mainGame.localGameWorld + sdk::ClientLocalGameWorld::Grenades
        );

        if (!Utils::valid_pointer(grenades))
            return;

        uint64_t newGrenadesListPtr = mem.Read<uint64_t>(grenades + 0x18);

        if (!Utils::valid_pointer(newGrenadesListPtr))
            return;

        if (newGrenadesListPtr != this->grenadesListPtr)
        {
            this->grenadesListPtr = newGrenadesListPtr;
            grenadeCache.clear();
        }

        auto grenadeListRaw = UnityList<uint64_t>::Create(this->grenadesListPtr);

        const int rawCount = grenadeListRaw.count();

        if (rawCount < 0 || rawCount > 256)
            return;

        if (rawCount == 0)
        {
            grenadeCache.clear();
            return;
        }

        std::unordered_set<uint64_t> aliveNades;
        aliveNades.reserve(rawCount);

        for (int i = 0; i < rawCount; ++i)
        {
            uint64_t grenadeInstance = grenadeListRaw[i];

            if (!Utils::valid_pointer(grenadeInstance))
                continue;

            aliveNades.insert(grenadeInstance);

            auto it = std::find_if(
                grenadeCache.begin(),
                grenadeCache.end(),
                [&](const GrenadeList& nade)
                {
                    return nade.instance == grenadeInstance;
                });

            if (it != grenadeCache.end())
                continue;

            uint64_t transformPtr = mem.ReadChain(grenadeInstance, TransformChain);

            if (!Utils::valid_pointer(transformPtr))
                continue;

            GrenadeList newNade{};
            newNade.instance = grenadeInstance;
            newNade.transformInternal = transformPtr;
            newNade.worldLocation = glm::vec3{ 0.f, 0.f, 0.f };

            grenadeCache.push_back(newNade);
        }

        // Remove nades no longer in the raw live list
        grenadeCache.erase(
            std::remove_if(
                grenadeCache.begin(),
                grenadeCache.end(),
                [&](const GrenadeList& nade)
                {
                    return aliveNades.find(nade.instance) == aliveNades.end();
                }),
            grenadeCache.end()
        );

        // Update positions
        for (auto& nade : grenadeCache)
        {
            if (!Utils::valid_pointer(nade.instance))
                continue;

            if (!Utils::valid_pointer(nade.transformInternal))
                continue;

            UnityTransform transform(nade.transformInternal);

            glm::vec3 pos = transform.UpdatePosition();

            if (!std::isfinite(pos.x) ||
                !std::isfinite(pos.y) ||
                !std::isfinite(pos.z))
            {
                continue;
            }

            nade.worldLocation = pos;
        }
    }
    catch (const std::exception& e)
    {
        LOGS.logError("Exception caught in explosiveManager: " + std::string(e.what()));
    }
    catch (...)
    {
        LOGS.logError("Unknown exception caught in explosiveManager.");
    }
}

void ExplosiveManager::clearCache()
{
	this->grenadeList.clear();
	this->grenadesListPtr = NULL;

	this->tripwireSet.clear();
	this->tripwireList.clear();
	this->syncObjectorsPtr = NULL;
}

void ExplosiveManager::clearCacheNades()
{
	this->grenadeList.clear();
	this->grenadesListPtr = NULL;
}

void ExplosiveManager::clearCacheTripwires()
{
	this->tripwireList.clear();
	this->tripwireSet.clear();
	this->tripwireList.clear();
	this->syncObjectorsPtr = NULL;
}