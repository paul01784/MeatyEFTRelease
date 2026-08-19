#pragma once
#include <atomic>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <chrono>
#include <vector>

struct exfilsMemory {

	uint64_t instance;
	glm::vec3 locationWorld;
	std::string extractName;
	std::string status;
	int distance;
	int statusRaw;

	exfilsMemory()
		: instance(0),
		locationWorld(glm::vec3()),
		extractName(""),
		status(""),
		distance(0),
		statusRaw(0) {
	}
};

struct exfilsSecret {

	uint64_t instance;
	glm::vec3 locationWorld;
	std::string extractName;
	std::string status;
	int distance;
	int statusRaw;

	exfilsSecret()
		: instance(0),
		locationWorld(glm::vec3()),
		extractName(""),
		status(""),
		distance(0),
		statusRaw(0) {
	}
};

struct exfilsTransit {

	uint64_t instance;
	glm::vec3 locationWorld;
	std::string extractName;
	std::string status;
	int distance;
	int statusRaw;

	exfilsTransit()
		: instance(0),
		locationWorld(glm::vec3()),
		extractName(""),
		status(""),
		distance(0),
		statusRaw(0) {
	}
};

using ExfilCacheCollection = std::vector<exfilsMemory>;
using ExfilCacheSnapshot = std::shared_ptr<const ExfilCacheCollection>;

class Exfil 
{
public:
	Exfil();

	[[nodiscard]] ExfilCacheSnapshot getCacheExfilSnapshot() const noexcept;
	std::vector<exfilsSecret>& getCacheSecret();
	std::vector<exfilsTransit>& getCacheTransit();

	void exfilTask();

	void clearCache();


	std::string getExfilStatusText(int statusInt);

private:

	std::vector<exfilsMemory> exfilList;
	std::atomic<ExfilCacheSnapshot> publishedExfilCache;
	std::vector<exfilsSecret> exfilSecret;
	std::vector<exfilsTransit> exfilTransit;

	std::vector<std::string> _pmcEntries;
	std::vector<std::string> _scavIds;

	std::chrono::steady_clock::time_point lastExfilStatusUpdate;
	std::chrono::steady_clock::time_point lastExfilDiscovery;

	void tryLoadMemoryExfils();
	void publishCacheSnapshot();
	
	int getDistance(glm::vec3 point1, glm::vec3 point2);
	void updateStatus();

	void LoadEligibleEntryPoints(uint64_t exfilPointAddr);

};

extern Exfil exfil;
