#pragma once
#include <atomic>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <chrono>
#include <vector>

#include "TransitLocations.h"

enum class ExfilType : std::uint8_t
{
	Regular,
	Secret,
	Transit,
};

struct exfilsMemory {

	uint64_t instance;
	glm::vec3 locationWorld;
	std::string extractName;
	std::string status;
	int distance;
	int statusRaw;
	ExfilType type = ExfilType::Regular;
	TransitId transitId = TransitId::Unknown;
};

using ExfilCacheCollection = std::vector<exfilsMemory>;
using ExfilCacheSnapshot = std::shared_ptr<const ExfilCacheCollection>;

class Exfil 
{
public:
	Exfil();

	[[nodiscard]] ExfilCacheSnapshot getCacheExfilSnapshot() const noexcept;

	void exfilTask();

	void clearCache();


	std::string getExfilStatusText(int statusInt);

private:

	std::vector<exfilsMemory> exfilList;
	std::atomic<ExfilCacheSnapshot> publishedExfilCache;

	std::chrono::steady_clock::time_point lastExfilStatusUpdate;
	std::chrono::steady_clock::time_point lastExfilDiscovery;

	void tryLoadMemoryExfils();
	void loadStaticTransits();
	void publishCacheSnapshot();
	
	int getDistance(glm::vec3 point1, glm::vec3 point2);
	void updateStatus();
	std::string getLocalEligibilityId() const;
	bool isEligibleForLocalPlayer(uint64_t exfilPointAddr, const std::string& localEligibilityId) const;

};

extern Exfil exfil;
