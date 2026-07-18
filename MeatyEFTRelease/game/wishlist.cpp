#include "headers/wishlist.h"

#include "../memory/Memory.h"
#include "headers/sdk.h"
#include "headers/unityHelper.h"
#include "headers/maingame.h"
#include "headers/utils.h"
#include "headers/tarkovdevquery.h"

WishListManager wishListManager;
std::vector<wishListItems> wishListData;

void WishListManager::createWishList()
{
	try
	{
		if (!Utils::valid_pointer(mainGame.localplayerProfile))
			return;

		if (wishListData.size() > 0)
		{
			return;
		}

		auto wishListManagerPTR = mem.Read<uint64_t>(mainGame.localplayerProfile + sdk::Profile::WishlistManager);
		auto itemPtr = mem.Read<uint64_t>(wishListManagerPTR + sdk::WishlistManager::_wishlistItems);
		if (!Utils::valid_pointer(itemPtr))
		{
			std::cout << "[WishList] : Invaild Pointer to items, exiting...\n";
			return;
		}

		auto items = UnityDictionary<MongoID, int>(itemPtr);

		if (items.GetCount() == 0)
		{
			auto& e0 = *items.begin();
			//std::cout << std::hex
			//	<< "MongoID ts=0x" << e0.Key._timeStamp
			//	<< " counter=0x" << e0.Key._counter
			//	<< " strId=0x" << e0.Key._stringId
			//	<< std::dec << "\n";

			std::cout << "[WishList] : Item list count 0, exiting...\n";
			return;
		}

		for (auto& item : items)
		{
			try
			{
				wishListItems wishListNew;
				wishListNew.bsgId = TrimEFT(item.Key.ReadString(mem));
				if (wishListNew.bsgId != " " || wishListNew.bsgId.empty())
				{

					for (auto& ml : marketList)
					{
						if (ml.bsgid != wishListNew.bsgId.c_str())
							continue;

						wishListNew.shortName = ml.shortName;
						break;

					}

					wishListData.emplace_back(wishListNew);
				}

			}
			catch (...)
			{
			}
		}

		if (wishListData.size() > 1)
		{
			LOGS.logInfo("[WishList] Size of database : ", wishListData.size());
		}
		else
		{
			LOGS.logError("[WishList] Error collecting items in wishList");
		}
	}
	catch (const std::exception& e) {
		LOGS.logError("Exception caught in wishListManager: " + std::string(e.what()) + ". Retrying...");
		return;
	}
	catch (...) {
		LOGS.logError("Unknown exception caught in wishListManager. Retrying...");
		return;
	}
}