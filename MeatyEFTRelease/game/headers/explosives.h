#pragma once
#include <cstdint>
#include <unordered_set>
#include <iostream>
#include <algorithm>
#include "../game/headers/transform.h"

#include <chrono>

enum class ETripwireState
{
	None = 0,
	Wait = 1,
	Active = 2,
	Exploding = 3,
	Exploded = 4,
	Inert = 5,
};

enum class SynchronizableObjectType
{
	AirDrop = 0,
	AirPlane = 1,
	Tripwire = 2,
};


struct GrenadeList
{
	uint64_t instance = 0;
	bool isSmoke = false;
	bool isDestroyed = false;

	std::chrono::steady_clock::time_point addedAt{};

	UnityTransform _transform = NULL;
	glm::vec3 worldLocation{ 0.f, 0.f, 0.f };

};

static constexpr auto TTL_GRENADE = std::chrono::seconds(25);
static constexpr auto TTL_SMOKE = std::chrono::minutes(3);

struct TripwireList
{
	uint64_t instance = 0;
	int state = 0;
	bool _destroyed = false;
	bool _isActive = false;
	glm::vec3 worldLocation{ 0.f, 0.f, 0.f };
};

class ExplosiveManager
{
public:
	std::vector<GrenadeList>& getGrenades();
	std::vector<TripwireList>& getTripwires();
	
	void initManager();
	void clearCacheNades();
	void clearCacheTripwires();
	void clearCache();

	void tryAddGrenade(uint64_t instance);
	void tryAddTripwire(uint64_t instance);

	void pruneGrenades();


	//manager raid ptrs
	static uint64_t grenadesListPtr;
	static uint64_t syncObjectorsPtr;


private:

	std::vector<GrenadeList> grenadeList;
	std::vector<TripwireList> tripwireList;

	std::unordered_set<uint64_t> grenadeSet; // tracks instances grenades
	std::unordered_set<uint64_t> tripwireSet; // tracks instances

};

extern ExplosiveManager explosiveManager;