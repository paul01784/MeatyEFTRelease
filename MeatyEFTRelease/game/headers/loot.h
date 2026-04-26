#pragma once
#include <map>
#include <string>
#include <glm/glm.hpp>

struct corpseEquipment {
	std::string equipmentName;
	int value;
	bool wanted;
};

struct lootFilterItems {
	std::string bsgid;
	std::string name;
	std::string shortName;
	long traderPrice;
	long marketPrice;

	lootFilterItems()
		: bsgid(""),
		name(""),
		shortName(""),
		traderPrice(0),
		marketPrice(0) {
	}
};

struct LootFilters {
	long id;
	bool active;
	std::string filterName;
	glm::vec4 filterColour;
	std::vector<lootFilterItems> lootItems;

};

//list of loot in memory (not inside containers) cached items
struct LootList {
	uint64_t instance;

	//stored ptrs
	uint64_t m_itemObject;
	uint64_t m_interactiveClass;
	uint64_t m_baseObject;
	uint64_t m_gameObject;
	uint64_t m_pGameObjectName;
	std::string m_objectClassName;
	uint64_t m_objectClass;
	uint64_t m_pointerToTransform1;
	uint64_t m_pointerToTransform2;

	glm::vec3 worldLocation;
	std::string gameObjectName;
	std::string bsgId;
	std::string longName;
	std::string shortName;
	int avgMarketPrice;
	int traderPrice;
	int corpseValue;

	int distance;

	//type
	bool isItem;
	bool isContainer;
	bool isQuestItem;
	bool isCorpse;

	//corpse stuff
	std::vector<corpseEquipment> corpseEquip;

	//wanted or not
	bool wanted;
	bool forceWanted;
	glm::vec4 color;

	LootList()
		: m_itemObject(0),
		m_interactiveClass(0),
		m_baseObject(0),
		m_gameObject(0),
		m_pGameObjectName(0),
		m_objectClassName(""),
		m_objectClass(0),
		m_pointerToTransform1(0),
		m_pointerToTransform2(0),
		worldLocation(glm::vec3()),
		gameObjectName(""),
		bsgId(""),
		longName(""),
		shortName(""),
		avgMarketPrice(0),
		traderPrice(0),
		corpseValue(0),
		isItem(false),
		isContainer(false),
		isQuestItem(false),
		isCorpse(false),
		wanted(false),
		forceWanted(false),
		color(glm::vec4()) {
	}
};


extern std::vector<LootFilters> lootFilters;

class loot {
public:

	std::vector<LootList>& getCacheLoot();

	void lootTask();
	void clearCache();


	uint64_t lootListP;
	uint64_t lootListPtr;
	long lootCount;

	bool drawDrawer;
	bool drawDuffle;
	bool drawSafe;
	bool drawWeaponBox;
	bool drawTechCrate;
	bool drawRationCrate;
	bool drawMedicalCrate;
	bool drawJacket;
	bool drawMedPackage;
	bool drawMedBox;
	bool drawToolbox;
	bool drawGrenadeBox;
	bool drawBuriedStash;
	bool drawGroundCache;
	bool drawWoodenCrate;
	bool drawSuitcase;
	bool drawAmmoBox;
	bool drawDeadBody;
	bool drawPCBlock;
	bool drawRegister;
	bool drawAirDrops;


	

private:

	bool buildPointers();

	bool get_lootCount();

	bool buildLootBuffer();

	bool isPointerInVector_lootList(uint64_t ptr) const;

	void scanCorpseEquipment(uint64_t interactive, LootList& lootList, bool update = false);

	std::chrono::steady_clock::time_point lastCorpseEquipUpdate;
	void updateCorpseRequirements();

	//storage containers
	std::vector<LootList> lootList;
	std::vector<uint64_t> loot_buffer;

};



extern loot Loot;