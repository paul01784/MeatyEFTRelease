#pragma once
#include <string>
#include <vector>

struct wishListItems
{
	std::string bsgId;
	std::string shortName;
};

class WishListManager
{
public:
	void createWishList();
};

extern std::vector<wishListItems> wishListData;
extern WishListManager wishListManager;