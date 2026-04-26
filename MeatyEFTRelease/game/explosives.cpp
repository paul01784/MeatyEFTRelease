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

std::vector<TripwireList>& ExplosiveManager::getTripwires()
{
	return this->tripwireList;
}

void ExplosiveManager::tryAddTripwire(uint64_t instance)
{
	if (!instance)
		return;

	if (tripwireSet.contains(instance))
		return;

	

	glm::vec3 position = mem.Read<glm::vec3>(instance + sdk::TripwireSynchronizableObject::ToPosition);
	int state = mem.Read<int>(instance + sdk::TripwireSynchronizableObject::_tripwireState);

	tripwireList.emplace_back(TripwireList{
		instance,
		state,
		false,
		true,
		position
		});

	tripwireSet.insert(instance);
}

void ExplosiveManager::tryAddGrenade(uint64_t instance)
{
	if (!instance)
		return;

	if (grenadeSet.contains(instance))
		return;

	

	bool isSmoke = false;

	// Read grenade type/name
	const std::string type = TrimEFT(ReadName(instance, 64, false));
	if (type == "SmokeGrenade")
	{
		isSmoke = true;
		return;
	}

	// Resolve TransformInternal (uint64_t), then construct UnityTransform from it
	const uint64_t ti = mem.ReadChain(instance, TransformChain);
	if (!ti)
		return;

	UnityTransform transform(ti);

	grenadeList.emplace_back(GrenadeList{
		instance,
		isSmoke,
		false,
		std::chrono::steady_clock::now(),
		transform,
		glm::vec3(0.f)
		});

	grenadeSet.insert(instance);
}

void ExplosiveManager::pruneGrenades()
{
	const auto now = std::chrono::steady_clock::now();

	std::erase_if(grenadeList, [&](const GrenadeList& g)
		{
			const auto ttl = g.isSmoke ? TTL_SMOKE : TTL_GRENADE;
			const bool expired = (now - g.addedAt) > ttl;

			if (!g.isDestroyed && !expired)
				return false;

			grenadeSet.erase(g.instance);
			return true;
		});
}


void ExplosiveManager::initManager()
{
	try
	{

		if (!Utils::valid_pointer(this->grenadesListPtr))
		{
			uint64_t grenades = mem.Read<uint64_t>(mainGame.localGameWorld + sdk::ClientLocalGameWorld::Grenades);
			this->grenadesListPtr = mem.Read<uint64_t>(grenades + 0x18);
			//std::cout << "[EXPLOSIVES] GrenadeList Ptr : 0x" << std::hex << this->grenadesListPtr << std::endl;
		}
		else
		{
			// update grenade list
			auto grenadeListRaw = UnityList<uint64_t>::Create(this->grenadesListPtr);



			for (auto& nade : grenadeListRaw)
				explosiveManager.tryAddGrenade(nade);

			// cache reference (ONLY ONCE)
			auto& cache = getGrenades();

			// scatter destroyed flags
			auto handle = mem.CreateScatterHandle();

			for (auto& g : cache)
			{
				if (g.isSmoke)
					continue;

				mem.AddScatterReadRequest(handle,
					g.instance + sdk::Throwable::_isDestroyed,
					&g.isDestroyed,
					sizeof(bool));
			}

			mem.ExecuteReadScatter(handle);
			mem.CloseScatterHandle(handle);

			// update positions for live grenades
			for (auto& g : cache)
			{
				if (g.isSmoke || g.isDestroyed)
					continue;

				g.worldLocation = g._transform.UpdatePosition();
			}

			// remove destroyed items (+ remove from set)
			std::erase_if(cache, [&](const GrenadeList& g)
				{
					if (!g.isDestroyed)
						return false;

					grenadeSet.erase(g.instance);
					return true;
				});

			pruneGrenades();


		}
		/*
		if (!Utils::valid_pointer(syncObjectorsPtr))
		{
			// setup tripwire ptr
			syncObjectorsPtr = mem.ReadChain(mainGame.localGameWorld, {
				sdk::ClientLocalGameWorld::SynchronizableObjectLogicProcessor,
				sdk::SynchronizableObjectLogicProcessor::_staticSynchronizableObjects
				});
			//std::cout << "[EXPLOSIVES] syncObjectors Ptr : 0x" << std::hex << this->syncObjectorsPtr << std::endl;
		}
		else
		{
			auto tripwireListRaw = UnityList<uint64_t>::Create(syncObjectorsPtr);



			for (auto& tripWire : tripwireListRaw)
			{
				auto type = mem.Read<SynchronizableObjectType>(tripWire + sdk::SynchronizableObject::Type);
				if (type != SynchronizableObjectType::Tripwire)
					continue;

				explosiveManager.tryAddTripwire(tripWire);
			}

			//check state of tripwires

			auto& cache = getTripwires();

			auto handle = mem.CreateScatterHandle();
			for (auto& tripWireCache : cache)
			{
				mem.AddScatterReadRequest(handle,
					tripWireCache.instance + sdk::TripwireSynchronizableObject::_tripwireState,
					&tripWireCache.state,
					sizeof(int));
			}
			mem.ExecuteReadScatter(handle);
			mem.CloseScatterHandle(handle);

			auto& cache2 = getTripwires();

			for (auto& tripWireCache : cache2)
			{
				if (tripWireCache.state == 4 || tripWireCache.state == 5)
					tripWireCache._destroyed = true;
				else
					tripWireCache._destroyed = false;

				if (tripWireCache.state == 1 || tripWireCache.state == 2)
					tripWireCache._isActive = true;
				else
					tripWireCache._isActive = false;

			}

			// remove destroyed items (+ remove from set)
			std::erase_if(cache2, [&](const TripwireList& g)
				{
					if (!g._destroyed)
						return false;

					tripwireSet.erase(g.instance);
					return true;
				});



		}

		*/
	}
	catch (const std::exception& e) {
		LOGS.logError("Exception caught in explosiveManager: " + std::string(e.what()) + ". Retrying...");
		return;
	}
	catch (...) {
		LOGS.logError("Unknown exception caught in explosiveManager. Retrying...");
		return;
	}

		
}

void ExplosiveManager::clearCache()
{
	this->grenadeList.clear();
	this->grenadeSet.clear();
	this->grenadesListPtr = NULL;

	this->tripwireSet.clear();
	this->tripwireList.clear();
	this->syncObjectorsPtr = NULL;

}

void ExplosiveManager::clearCacheNades()
{
	this->grenadeList.clear();
	this->grenadeSet.clear();

	this->grenadesListPtr = NULL;
	
}

void ExplosiveManager::clearCacheTripwires()
{
	this->tripwireList.clear();
	this->tripwireSet.clear();

	this->tripwireList.clear();

	this->syncObjectorsPtr = NULL;
}