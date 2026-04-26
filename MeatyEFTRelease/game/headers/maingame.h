#pragma once
#include <shared_mutex>
#include <array>
#include <glm/glm.hpp>

class MainGame {

public:

	static uint64_t gameObjectManager;
	
	static uint64_t gameWorld;
	static uint64_t localGameWorld;

	static uint64_t registeredPlayers;
	static uint64_t registeredPlayersList;
	static int registeredPlayersCount;
	static uint64_t player_buffer[327];

	static std::string selectedLocation;
	static bool onlineRaid;
	
	static int pmcNumber;

	static uint64_t localPlayerPtr;
	static uint64_t localPlayerHands;
	static glm::vec3 localLocation;
	static glm::vec2 localRotation;
	static std::string localGroupId;
	static bool localIsScoped;
	static bool localIsSavage;
	static uint64_t localPlayerPWA;
	static uint64_t localplayerProfile;

	static std::array<uint64_t, 2> active_objects;
	static std::array<uint64_t, 2> tagged_objects;


	//functions

	void mainThread();
	bool checkIfRaidStarted();
	void updateLocalPlayerPtr();
	void updatePlayerList();
	void getPlayerListDetails();
	void getGameWorldDetails();

	void cameraAndAimWorker();

	void featuresTaskWorker();

	void keyManagerTask();

	void raidMonitorTask();

	void clearCache();


};

extern MainGame mainGame;