#pragma once
#include <glm/glm.hpp>
#include <string>
#include <chrono>

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

class Exfil 
{
public:

	std::vector<exfilsMemory>& getCacheExfil();
	std::vector<exfilsSecret>& getCacheSecret();
	std::vector<exfilsTransit>& getCacheTransit();

	void exfilTask();

	void clearCache();

private:

	std::vector<exfilsMemory> exfilList;
	std::vector<exfilsSecret> exfilSecret;
	std::vector<exfilsTransit> exfilTransit;

	std::vector<std::string> _pmcEntries;
	std::vector<std::string> _scavIds;

	std::chrono::steady_clock::time_point lastExfilStatusUpdate;

	void tryLoadMemoryExfils();
	std::string getExfilStatusText(int statusInt);
	int getDistance(glm::vec3 point1, glm::vec3 point2);
	void updateStatus();

	void LoadEligibleEntryPoints(uint64_t exfilPointAddr);

};

extern Exfil exfil;