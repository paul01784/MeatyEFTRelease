#pragma once

// List of all items in game
struct gameItemList {
	std::string bsgid;
	std::string bsgCategory;
	std::string name;
	std::string shortName;
	long traderPrice;
	long marketPrice;
};

struct gameCatList {
	long id;
	std::string categoryName;
};

extern std::vector<gameItemList> marketList;
extern std::vector<gameCatList> catList;


//forward functions to other areas
std::string loadjson();
void buildCatList();
void buildItemList();
std::string BSGidToName(std::string bsgid);