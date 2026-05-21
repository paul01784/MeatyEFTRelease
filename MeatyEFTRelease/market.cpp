#include "app/includes.h"
#include "app/market.h"


#include <curl/curl.h>
#include <nlohmann/json.hpp>

std::vector<gameItemList> marketList;
std::vector<gameCatList> catList;

//temp storage for downloaded data from API
nlohmann::json jf;

// callback function writes data to a std::ostream
static size_t data_write(void* buf, size_t size, size_t nmemb, void* userp)
{
	if (userp)
	{
		std::ostream& os = *static_cast<std::ostream*>(userp);
		std::streamsize len = size * nmemb;
		if (os.write(static_cast<char*>(buf), len))
			return len;
	}

	return 0;
}

static void sortByName() {
	std::sort(marketList.begin(), marketList.end(), [](const gameItemList& lhs, const gameItemList& rhs)
		{
			return lhs.name < rhs.name;
		});
}

CURLcode curl_read(const std::string& url, std::ostream& os, long timeout = 30)
{
	CURLcode code(CURLE_FAILED_INIT);
	CURL* curl = curl_easy_init();

	if (curl)
	{
		if (CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &data_write))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_FILE, &os))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout))
			&& CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str())))
		{
			code = curl_easy_perform(curl);
		}
		curl_easy_cleanup(curl);
	}
	return code;
}

constexpr const char* MARKET_CACHE_FILE = "market_items_cache.json";
constexpr int CACHE_MAX_AGE_SECONDS = 3 * 24 * 60 * 60; // 3 days
static bool IsCacheValid(const std::string& path)
{
	struct _stat st;
	if (_stat(path.c_str(), &st) != 0)
		return false;

	const std::time_t now = std::time(nullptr);
	return (now - st.st_mtime) <= CACHE_MAX_AGE_SECONDS;
}
std::string loadjson()
{
	curl_global_init(CURL_GLOBAL_ALL);

	std::ostringstream oss;

	const std::string url =
		"https://api.tarkov.dev/graphql?query=%7BitemsByType%28type%3A+any%29+%7Bid+name+shortName+basePrice+avg24hPrice+categories%7Bname%7D%7D%7D";

	
	// TRY LOAD CACHE FIRST
	if (IsCacheValid(MARKET_CACHE_FILE))
	{
		try
		{
			std::ifstream in(MARKET_CACHE_FILE, std::ios::binary);
			if (in.is_open())
			{
				std::stringstream buffer;
				buffer << in.rdbuf();
				in.close();

				auto json = nlohmann::json::parse(buffer.str());

				if (json.contains("data") && json["data"].contains("itemsByType"))
				{
					jf = json["data"]["itemsByType"];
					LOGS.logInfo("[MARKET][JSON] Loaded data from cache");
					curl_global_cleanup();
					return buffer.str();
				}
			}
		}
		catch (...)
		{
			LOGS.logError("[MARKET][JSON] Cached file invalid, falling back to API");
		}
	}

	
	// CACHE MISS → API CALL
	CURLcode res = curl_read(url, oss);

	if (res != CURLE_OK)
	{
		LOGS.logError(
			std::string("[MARKET][JSON] API request failed: ") +
			curl_easy_strerror(res)
		);

		curl_global_cleanup();
		return "";
	}

	
	//PARSE API RESPONSE
	try
	{
		const std::string response = oss.str();
		auto json = nlohmann::json::parse(response);

		if (!json.contains("data") || !json["data"].contains("itemsByType"))
		{
			LOGS.logError("[MARKET][JSON] API response missing data.itemsByType");
			curl_global_cleanup();
			return "";
		}

		jf = json["data"]["itemsByType"];

		/* ============================
		   4) SAVE CACHE
		   ============================ */
		std::ofstream out(MARKET_CACHE_FILE, std::ios::binary);
		if (out.is_open())
		{
			out << response;
			out.close();
			LOGS.logInfo("[MARKET][JSON] API data cached to disk");
		}
		else
		{
			LOGS.logError("[MARKET][JSON] Failed to write cache file");
		}

		curl_global_cleanup();
		return response;
	}
	catch (...)
	{
		LOGS.logError("[MARKET][JSON] Exception parsing API JSON");
	}

	curl_global_cleanup();
	return "";
}

void buildCatList() {

	catList.empty();

	int i = 0;
	for (auto& ec : jf) {

		gameCatList cat;

		//check list size if 0 just add first one we get in market list and continue to next!
		if (catList.size() == 0)
		{
			cat.id = i;
			cat.categoryName = "None";
			catList.emplace_back(cat);
			i++;
			continue;
		}

		//search our list atm and check if we already have one in it, if so skip it and move onto the next one!
		bool found = false; // set false unless we find it
		for (auto& curList : catList)
		{
			if (curList.categoryName == ec["category"]["normalizedName"].get<std::string>())
				found = true;
		}

		//if we havent found it then lets add it to the list
		if (found == false)
		{
			cat.id = i;
			cat.categoryName = ec["category"]["normalizedName"].get<std::string>();
			catList.emplace_back(cat);
		}

		i++; // increase counter int
	}

	if (catList.size() > 0)
	{
		//std::cout << "Category list created. We have " << std::to_string(catList.size()) << " in our list!" << std::endl;
		LOGS.logInfo("[MARKET][BUILD] Category List Created");

		std::sort(catList.begin(), catList.end(), [](const gameCatList& lhs, const gameCatList& rhs)
			{ return lhs.categoryName < rhs.categoryName; }
		);
	}
	else
		LOGS.logError("[MARKET][BUILD] Category List didn't build correctly");

}


void buildItemList() {


	marketList.clear();

	for (auto& ec : jf) {

		//if (ec[xorstr_("id")] != item.bsgid)
			//continue;
		gameItemList item;

		item.bsgid = ec["id"];
		if (item.bsgid == "mosinscopedbarter0000001" || item.bsgid == "5648b2414bdc2d3b4c8b4578" || item.bsgid == "5648b6ff4bdc2d3d1c8b4581" || item.bsgid == "59984b4286f77445bd2d4a07") 
		{
			//std::cout << "Bad ID : " << item.bsgid << std::endl;
			continue;
		}


		//item.bsgCategoryId = ec["bsgCategoryId"];
		item.name = ec["name"];
		item.shortName = ec["shortName"];
		item.traderPrice = ec["basePrice"];
		if (ec["avg24hPrice"] < 1)
		{
			item.marketPrice = 0;
		}
		else
		{
			item.marketPrice = ec["avg24hPrice"];
		}
		
		item.bsgCategory.clear();

		if (ec.contains("categories") && ec["categories"].is_array())
		{
			for (const auto& cat : ec["categories"])
			{
				if (cat.contains("name") && cat["name"].is_string())
				{
					item.bsgCategory.push_back(cat["name"].get<std::string>());
				}
			}
		}

		marketList.emplace_back(item);
	}
	if (marketList.size() > 0)
	{
		//std::cout << "Success! Total items in market is [" << std::to_string(marketList.size()) << "]" << std::endl;
		LOGS.logInfo("[MARKET][BUILD] Market info updated");

		std::sort(marketList.begin(), marketList.end(), [](const gameItemList& lhs, const gameItemList& rhs)
			{ return lhs.name < rhs.name; }
		);
	}
	else
		LOGS.logError("[MARKET][BUILD] Market info failed to build correctly");
}

std::string BSGidToName(std::string bsgid)
{
	if (bsgid.empty())
		return "ERR";

	if (bsgid.find("5d52cc5ba4b9367408500062") != std::string::npos)
		return "AGS-30";

	if (bsgid.find("5cdeb229d7f00c000e7ce174") != std::string::npos)
		return "NSV";

	for (auto& ec : marketList) {

		if (strcmp(ec.bsgid.c_str(), bsgid.c_str()) == 0)
		{
			return ec.shortName;
		}

	}
	std::ostringstream oss;
	return "NoData";
}




int Marketprice(std::string bsgid)
{
	for (auto& ec : marketList) {

		if (strcmp(ec.bsgid.c_str(), bsgid.c_str()) == 0)
		{
			if (ec.marketPrice != 0)
			{
				return ec.marketPrice;
			}
			else
				return 0;
		}

	}
}

