#pragma once
#include <shared_mutex>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <vector>
#include <glm/glm.hpp>

using RegisteredPlayerArray = std::vector<std::uint64_t>;
using RegisteredPlayerArraySnapshot = std::shared_ptr<const RegisteredPlayerArray>;

class MainGame {

public:

	static uint64_t gameObjectManager;
	
	static uint64_t gameWorld;
	static uint64_t localGameWorld;

	static uint64_t registeredPlayers;
	static uint64_t registeredPlayersList;
	static int registeredPlayersCount;
	static uint64_t player_buffer[327];
	static std::atomic<RegisteredPlayerArraySnapshot> registeredPlayerArraySnapshot;

	static bool btrAllocated;

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

	void mainThread(std::stop_token stopToken = {});
	bool checkIfRaidStarted();
	bool updatePlayerList(bool waitForDma = false);
	bool getGameWorldDetails(std::stop_token stopToken = {});
	[[nodiscard]] static RegisteredPlayerArraySnapshot getRegisteredPlayerArraySnapshot() noexcept;

	void keyManagerTask();

	void raidMonitorTask();

	void clearCache();

private:
	struct BattleModeSavedState
	{
		bool radarDrawLoot = false;
		bool radarDrawQuestHelper = false;
		bool radarDrawGrenades = false;
		bool radarDrawTripwires = false;
		bool radarDrawExfils = false;

		bool espDrawLoot = false;
		bool espDrawCorpse = false;
		bool espDrawQuestHelper = false;
		bool espDrawTripwires = false;
		bool espDrawExfil = false;

		bool valid = false;
	};

	BattleModeSavedState battleModeSavedState{};
	bool battleModeEnabled = false;
	std::uint8_t emptyPlayerListConfirmations = 0;

	void SetBattleMode(bool enabled);
};

extern MainGame mainGame;
