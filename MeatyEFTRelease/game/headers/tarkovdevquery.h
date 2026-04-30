#pragma once
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <glm/glm.hpp>
#include <curl/curl.h>

static const std::vector<long long> LevelXpThresholds = {
    0,            // Lvl 1
    1000,         // Lvl 2
    4017,         // Lvl 3
    8432,         // Lvl 4
    14256,        // Lvl 5
    21477,        // Lvl 6
    30023,        // Lvl 7
    39936,        // Lvl 8
    51204,        // Lvl 9
    63723,        // Lvl 10
    77563,        // Lvl 11
    93279,        // Lvl 12
    115302,       // Lvl 13
    143253,       // Lvl 14
    177337,       // Lvl 15
    217885,       // Lvl 16
    264432,       // Lvl 17
    316851,       // Lvl 18
    374400,       // Lvl 19
    437465,       // Lvl 20
    505161,       // Lvl 21
    577978,       // Lvl 22
    656347,       // Lvl 23
    741150,       // Lvl 24
    836066,       // Lvl 25
    944133,       // Lvl 26
    1066259,      // Lvl 27
    1199423,      // Lvl 28
    1343743,      // Lvl 29
    1499338,      // Lvl 30
    1666320,      // Lvl 31
    1846664,      // Lvl 32
    2043349,      // Lvl 33
    2258436,      // Lvl 34
    2492126,      // Lvl 35
    2750217,      // Lvl 36
    3032022,      // Lvl 37
    3337766,      // Lvl 38
    3663831,      // Lvl 39
    4010401,      // Lvl 40
    4377662,      // Lvl 41
    4765799,      // Lvl 42
    5182399,      // Lvl 43
    5627732,      // Lvl 44
    6102063,      // Lvl 45
    6630287,      // Lvl 46
    7189442,      // Lvl 47
    7779792,      // Lvl 48
    8401607,      // Lvl 49
    9055144,      // Lvl 50
    9740666,      // Lvl 51
    10458431,     // Lvl 52
    11219666,     // Lvl 53
    12024744,     // Lvl 54
    12874041,     // Lvl 55
    13767918,     // Lvl 56
    14706741,     // Lvl 57
    15690872,     // Lvl 58
    16720667,     // Lvl 59
    17816442,     // Lvl 60
    19041492,     // Lvl 61
    20360945,     // Lvl 62
    21792266,     // Lvl 63
    23350443,     // Lvl 64
    25098462,     // Lvl 65
    27100775,     // Lvl 66
    29581231,     // Lvl 67
    33028574,     // Lvl 68
    37953544,     // Lvl 69
    44260543,     // Lvl 70
    51901513,     // Lvl 71
    60887711,     // Lvl 72
    71228846,     // Lvl 73
    82933459,     // Lvl 74
    96009180,     // Lvl 75
    110462910,    // Lvl 76
    126300949,    // Lvl 77
    144924572,    // Lvl 78
    172016256     // Lvl 79
};

struct PlayerProfileStats
{
    //
    // Identity & Account Info
    //
    long long aid = 0;                     // From "aid"
    std::string nickname;                  // info.nickname
    std::string side;                      // info.side ("Usec", "Bear", "Savage")
    int experience = 0;                    // info.experience
    int memberCategory = 0;                // info.memberCategory
    int selectedMemberCategory = 0;        // info.selectedMemberCategory
    int prestigeLevel = 0;                 // info.prestigeLevel

    //
    // Derived Gameplay Info
    //
    int level = 0;                         // Calculated from experience

    //
    // Time Played  (source is seconds)
    //
    uint32_t hoursPlayed = 0;

    //
    // PMC Stats & Counters
    //
    uint32_t Kills = 0;                    // "Kills"
    uint32_t killedPMC = 0;                // "KilledPMC"
    uint32_t deathsPMC = 0;                // "Deaths"
    uint32_t survivedRaids = 0;            // Key: ["ExitStatus","Survived","Pmc"]
    uint32_t killedInRaids = 0;            // Key: ["ExitStatus","Killed","Pmc"]
    uint32_t runsThrough = 0;              // Key: ["ExitStatus","Runner","Pmc"]
    uint32_t totalRaids = 0;               // Key: ["Sessions","Pmc"]

    //
    // Metadata
    //
    std::chrono::system_clock::time_point updated; // From "updated" epoch ms

    //
    // Storage for any unhandled counters so nothing is lost
    //
    std::vector<std::pair<std::vector<std::string>, int>> otherCounters;
};

struct ItemCounter
{
    std::vector<std::string> Key;
    int Value = 0;
};

struct TarkovDevExtract
{
    std::string mapNameId;
    std::string extractName;
    std::string extractFaction;
    glm::vec3 extractPosition;
};

struct TarkovDevTransit
{
    std::string mapNameId;    
    std::string description;    
    glm::vec3   position;
};

struct TarkovZone
{
    glm::vec3 position;
    std::string mapNameId; // map.nameId
};

struct TarkovObjective
{
    std::string type; // "shoot", "giveItem", "visit", ...

    // Optional fields (present depending on type)
    std::string id;           // objective id (often present, not always)
    std::string itemId;       // item.id for findItem / giveItem
    std::string questItemId;  // questItem.id for findQuestItem / giveQuestItem

    std::vector<std::string> maps; // maps[].nameId (quest item objectives)
    std::vector<TarkovZone> zones; // zones[] (visit objectives)
};

struct TarkovDevTasks
{
    std::string qID;   // task id
    std::string qName; // task name
    std::vector<TarkovObjective> objectives;
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

    
    std::string loadJsonQuests();

    void buildTasksList();

    std::string loadJsonExfils();

    static size_t data_write(void* buf, size_t size, size_t nmemb, void* userp);
    CURLcode curl_read(const std::string& url, std::ostream& os, long timeout);

};

extern TarkovDev tarkovDev;
extern std::vector<TarkovDevExtract> tarkovDevExfilsData;
extern std::vector<TarkovDevTransit> tarkovDevTransitData;
extern std::vector<TarkovDevTasks> tarkovDevTasksData;