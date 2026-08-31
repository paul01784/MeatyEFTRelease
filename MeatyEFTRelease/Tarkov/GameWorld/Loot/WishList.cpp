#include "WishList.h"

#include "../../../memory/Memory.h"
#include "../../SDK/EftOffsets.h"
#include "../../Unity/UnityContainers.h"
#include "../MainGame.h"
#include "../../../Core/Utilities.h"
#include "../../../Web/TarkovDev/TarkovDevClient.h"

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

		constexpr int kMaxWishlistItems = 512;
		auto items = UnityDictionary<MongoID, int>(
			itemPtr,
			kMaxWishlistItems,
			"Wishlist items");

		if (items.GetCount() == 0)
		{
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
