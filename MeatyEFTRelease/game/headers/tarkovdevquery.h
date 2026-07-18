#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <glm/glm.hpp>

extern const std::vector<long long> LevelXpThresholds;

struct PlayerProfileStats
{
    long long aid = 0;
    std::string nickname;
    std::string side;
    int experience = 0;
    int memberCategory = 0;
    int selectedMemberCategory = 0;
    int prestigeLevel = 0;

    int level = 0;
    std::uint32_t hoursPlayed = 0;

    std::uint32_t Kills = 0;
    std::uint32_t killedPMC = 0;
    std::uint32_t deathsPMC = 0;
    std::uint32_t survivedRaids = 0;
    std::uint32_t killedInRaids = 0;
    std::uint32_t runsThrough = 0;
    std::uint32_t totalRaids = 0;

    std::chrono::system_clock::time_point updated{};

    std::vector<std::pair<std::vector<std::string>, int>> otherCounters;
};

struct ItemCounter
{
    std::vector<std::string> Key;
    int Value = 0;
};

struct TarkovZone
{
    glm::vec3 position{ 0.0f, 0.0f, 0.0f };
    std::string mapNameId;
};

struct TarkovObjective
{
    std::string type;
    std::string id;
    std::string itemId;
    std::string questItemId;

    std::vector<std::string> maps;
    std::vector<TarkovZone> zones;
};

struct TarkovDevTasks
{
    std::string qID;
    std::string qName;
    std::vector<TarkovObjective> objectives;
};

struct gameItemList
{
    std::string bsgid;
    std::vector<std::string> bsgCategory;
    std::string name;
    std::string shortName;
    long traderPrice = 0;
    long marketPrice = 0;
};

struct gameCatList
{
    long id = 0;
    std::string categoryName;
};

class TarkovDevProfileClient
{
public:
    static std::optional<PlayerProfileStats> FetchProfile(long long accountId);
    static std::optional<PlayerProfileStats> GetProfileForAccountId(const std::string& accountId);

private:
    static std::string HttpGet(const std::string& url, long& httpCode);
};

class TarkovDev
{
public:
    bool Initialize(bool forceRefresh = false);

    std::string loadJsonQuests(bool forceRefresh = false);
    std::string loadJsonItems(bool forceRefresh = false);

    void buildTasksList();
    void buildItemList();
    void buildCatList();

    std::string BSGidToName(const std::string& bsgid) const;
    long MarketPrice(const std::string& bsgid) const;

private:
    enum class Dataset
    {
        Tasks,
        Items
    };

    std::string loadDataset(Dataset dataset, bool forceRefresh);

    static size_t data_write(void* buf, size_t size, size_t nmemb, void* userp);

    CURLcode curl_read(const std::string& url, std::ostream& os, long timeout, long* httpStatus = nullptr);

    bool tasksLoaded_ = false;
    bool itemsLoaded_ = false;
    std::string tasksRawJson_;
    std::string itemsRawJson_;
};

extern TarkovDev tarkovDev;
extern std::vector<TarkovDevTasks> tarkovDevTasksData;
extern std::vector<gameItemList> marketList;
extern std::vector<gameCatList> catList;

// Compatibility wrappers for existing call sites. New code can use tarkovDev directly.
std::string loadjson(bool forceRefresh = false);
void buildCatList();
void buildItemList();
std::string BSGidToName(const std::string& bsgid);
int Marketprice(const std::string& bsgid);