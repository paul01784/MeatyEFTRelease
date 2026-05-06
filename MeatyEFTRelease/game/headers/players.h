#pragma once
#include <SimpleMath.h>
#include <glm/glm.hpp>
#include "tarkovdevquery.h"

#include <mutex>
#include <shared_mutex>

struct PlayerGroups {
	int id;
	std::string groupId;

	PlayerGroups()
		: id(0),
		groupId("") {
	}
};

struct TransformAccessReadOnly
{
	uint64_t pTransformData; // offset 0x88
	int32_t index;          // offset 0x90
	int32_t pad;            // optional padding for alignment
};

struct TransformData
{
	uint64_t pTransformArray;   // relation_array   (pTransformData + 0x40)
	uint64_t pTransformIndices; // dependency_index_array (pTransformData + 0x68)
};

struct Matrix34
{
	DirectX::SimpleMath::Vector4 vec0;
	DirectX::SimpleMath::Vector4 vec1;
	DirectX::SimpleMath::Vector4 vec2;
};


enum allPlayerBones : int
{
	HumanBase = 0,
	HumanPelvis = 14,
	HumanLThigh1 = 15,
	HumanLThigh2 = 16,
	HumanLCalf = 17,
	HumanLFoot = 18,
	HumanLToe = 19,
	HumanRThigh1 = 20,
	HumanRThigh2 = 21,
	HumanRCalf = 22,
	HumanRFoot = 23,
	HumanRToe = 24,
	HumanSpine1 = 29,
	HumanSpine2 = 36,
	HumanSpine3 = 37,
	HumanLCollarbone = 89,
	HumanLUpperarm = 90,
	HumanLForearm1 = 91,
	HumanLForearm2 = 92,
	HumanLForearm3 = 93,
	HumanLPalm = 94,
	HumanRCollarbone = 110,
	HumanRUpperarm = 111,
	HumanRForearm1 = 112,
	HumanRForearm2 = 113,
	HumanRForearm3 = 114,
	HumanRPalm = 115,
	HumanNeck = 132,
	HumanHead = 133
};

enum class EPlayerSide : int
{
	Usec = 1,
	Bear = 2,
	Savage = 4
};

enum class PlayerType : uint8_t
{
	Default = 0,      
	Teammate,          
	PMC,               
	AIRaider,
	AIScav,
	AIBoss,            
	PScav,             
	SpecialPlayer,     
	Streamer           
};

struct AIRole
{
	std::string Name;
	PlayerType Type;
};

enum class ETagStatus : uint32_t
{
	Unaware = 1,        // 0b0000_0000_0000_0001
	Aware = 2,        // 0b0000_0000_0000_0010
	Combat = 4,        // 0b0000_0000_0000_0100
	Solo = 8,        // 0b0000_0000_0000_1000
	Coop = 16,       // 0b0000_0000_0001_0000
	Bear = 32,       // 0b0000_0000_0010_0000
	Usec = 64,       // 0b0000_0000_0100_0000
	Scav = 128,      // 0b0000_0000_1000_0000
	TargetSolo = 256,      // 0b0000_0001_0000_0000
	TargetMultiple = 512,      // 0b0000_0010_0000_0000
	Healthy = 1024,     // 0b0000_0100_0000_0000
	Injured = 2048,     // 0b0000_1000_0000_0000
	BadlyInjured = 4096,     // 0b0001_0000_0000_0000
	Dying = 8192,     // 0b0010_0000_0000_0000
	Birdeye = 16384,    // 0b0100_0000_0000_0000
	Knight = 32768,    // 0b1000_0000_0000_0000
	BigPipe = 65536     // 0b1_0000_0000_0000_0000
};

namespace UnityHelper
{
	DirectX::SimpleMath::Vector3 GetTransformPosition(uint64_t transformInternalAddress);
}

struct slots
{
	std::string name;
	uint64_t addr;
	std::string equipName;
	int price;
	bool wanted;
};

struct PlayerCache {

	uint64_t instance;
	bool isLocal;
	std::string className;
	std::string name;
	std::string groupId;
	std::string accountId;
	std::string profileId;
	std::string side;
	std::string voice;
	EPlayerSide playerSide;

	glm::vec3 location;
	
	std::chrono::steady_clock::time_point lastDogTagLookup;
	bool foundDogTagCache;

	PlayerProfileStats profileStats;
	bool hasProfileData;
	bool triedprofileonce;
	std::string DT_profileId;
	std::string DT_accountId;
	std::string DT_nickname;
	int DT_lvl;
	int DT_Side;


	int kd;
	double pkd;
	int hours;

	glm::vec4 colour;
	glm::vec3 rotationRAW;
	glm::vec2 rotation;
	int distance;
	int healthETAG; //ETagStatus


	bool isAi;
	bool isZombie;
	bool isPlayer;
	bool isPlayerScav;
	bool isBoss;
	bool isWatched;
	bool isBTR;
	uint64_t btrView;
	bool invalidBones;

	bool isDead;
	bool hasExfiled;

	bool isAiming;

	std::chrono::steady_clock::time_point lastWeaponUpdate{};
	std::chrono::milliseconds weaponUpdateInterval{};
	std::string itemInHand;
	uint64_t _lastObservedHands;


	std::chrono::steady_clock::time_point lastEquipmentUpdate{};
	std::chrono::milliseconds equipmentUpdateInterval{};
	bool equipInited;
	std::vector<slots> _slots;
	int playerValue;

	//ptrs to classes
	
	uint64_t P_Profile;
	uint64_t P_Info;
	uint64_t P_PWA;
	uint64_t P_Body;
	uint64_t P_InventoryControllerAddr;
	uint64_t P_HandsControllerAddr;
	uint64_t P_CorpseAddr;
	uint64_t P_ObservedPlayerController;
	uint64_t P_ObservedHealthController;
	uint64_t P_CorpseClass;
	
	uint64_t P_MovementContext;
	uint64_t P_RotationAddress;
	uint64_t P_HandsController;

	// Bone related stuff

	uint64_t playerBoneMatrixPtr;

	std::vector<allPlayerBones> boneList = {
		allPlayerBones::HumanPelvis, allPlayerBones::HumanHead, allPlayerBones::HumanNeck, allPlayerBones::HumanSpine1, allPlayerBones::HumanLForearm2,
		allPlayerBones::HumanLPalm, allPlayerBones::HumanRForearm2, allPlayerBones::HumanRPalm,
		allPlayerBones::HumanLThigh2, allPlayerBones::HumanLFoot,
		allPlayerBones::HumanRThigh2, allPlayerBones::HumanRFoot, allPlayerBones::HumanBase
	};

	std::vector<uint64_t> bonePtrs = std::vector<uint64_t>(boneList.size());

	std::vector<TransformAccessReadOnly> boneTransforms = std::vector<TransformAccessReadOnly>(boneList.size());
	std::vector<TransformData> boneTransformsData = std::vector<TransformData>(boneList.size());
	std::vector<glm::vec3> bonePositions = std::vector<glm::vec3>(boneList.size());

	std::vector<PVOID> pMatriciesBuffers = std::vector<PVOID>(boneList.size());
	std::vector<PVOID> pIndicesBuffers = std::vector<PVOID>(boneList.size());

	std::vector<size_t> matCap;
	std::vector<size_t> idxCap;
	
	void UpdateBonePositions();

	glm::vec3 GetTransformPosition(int boneIndex);

	// Default constructor
	PlayerCache()
		: instance(0),
		P_HandsController(0),
		_lastObservedHands(0),
		equipInited(0),
		invalidBones(0),
		isAiming(0),
		location(glm::vec3()),
		playerBoneMatrixPtr(0),
		playerValue(0),
		rotationRAW(glm::vec3()),
		isLocal(false),
		className(""),
		name(""),
		groupId(""),
		accountId(""),
		profileId(""),
		side(""),
		voice(""),
		hasProfileData(0),
		triedprofileonce(0),
		profileStats(0),
		DT_profileId(""),
		DT_accountId(""),
		DT_nickname(""),
		DT_lvl(0),
		DT_Side(0),
		kd(0),
		hours(0),
		lastDogTagLookup{},
		foundDogTagCache(false),
		colour(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f)),
		rotation(glm::vec2(0.0f, 0.0f)),
		distance(0),
		healthETAG(1024),
		isAi(false),
		isZombie(false),
		isPlayer(false),
		isPlayerScav(false),
		isBoss(false),
		isWatched(false),
		isBTR(false),
		btrView(0),
		isDead(false),
		hasExfiled(false),
		weaponUpdateInterval{ 600 },
		equipmentUpdateInterval{ 2000 },
		P_Profile(0),
		P_Info(0),
		P_PWA(0),
		P_Body(0),
		P_InventoryControllerAddr(0),
		P_HandsControllerAddr(0),
		P_CorpseAddr(0),
		P_CorpseClass(0),
		P_ObservedPlayerController(0),
		P_ObservedHealthController(0),
		P_MovementContext(0),
		P_RotationAddress(0)
		{
	}

};

class Players {

public:

	void clearCache();

	void softRestart();

	std::vector<PlayerCache>& getCache();
	std::vector<PlayerGroups>& getGroupCache();

	int getDistance(glm::vec3 point1, glm::vec3 point2);
	
	void playersTask();
	void boneTask();
	void playerEquipment();

	static bool groupIDSet;
	

private:

	

	//vector to store player cache;
	std::vector<PlayerCache> playerCache;
	std::vector<PlayerGroups> playerGroups;

	std::string voice2Name(std::string voiceName);

	PlayerCache getBonePtrs(PlayerCache& players);

	void readDogTagComponent(PlayerCache& players, bool force = false);

	void addEntity(uint64_t instance, bool isLocal);
	void tryFindBTR();
	void updateEntity();
	void checkGroupIDs();
	

	std::string heldItemName(PlayerCache& player);
	std::string ReadNameFromHandsItem(uint64_t itemBase);

	void checkExfil();

	uint64_t getPlayerHealthControllerPtr(uint64_t instance);
	uint64_t getPlayerBoneMatrixPtr(uint64_t instance);
	
};

extern Players players;
extern std::mutex playerMutex;