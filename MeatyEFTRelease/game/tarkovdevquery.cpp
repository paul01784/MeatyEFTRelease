
#include <curl/curl.h>
#include "headers/tarkovdevquery.h"
#include <iostream>
#include <nlohmann/json.hpp>
#include "headers/players.h"
#include "../app/debug.h"

using json = nlohmann::json;
TarkovDev tarkovDev;

nlohmann::json tarkovDevData;
nlohmann::json tarkovDevDataTasks;

constexpr const char* TDEV_CACHE_FILE = "tarkovdev_maps_cache.json";
constexpr const char* TDEV_CACHE_FILE_TASK = "tarkovdev_tasks_cache.json";

std::vector<TarkovDevExtract> tarkovDevExfilsData;
std::vector<TarkovDevTransit> tarkovDevTransitData;

std::vector<TarkovDevTasks> tarkovDevTasksData;

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    static_cast<std::string*>(userp)->append((char*)contents, totalSize);
    return totalSize;
}

std::string TarkovDevProfileClient::HttpGet(const std::string& url, long& httpCode)
{
    httpCode = 0;
    std::string response;

    CURL* curl = curl_easy_init();
    if (!curl)
        return response;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

    //
    // Force plain JSON to avoid brotli decoding issue
    //
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");

    //
    // SSL settings
    //
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

    //
    // Write callback
    //
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        std::cerr << "[ProfileClient] CURL Error: "
            << curl_easy_strerror(res) << std::endl;
    }
    else
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    }

    curl_easy_cleanup(curl);
    return response;
}

std::optional<PlayerProfileStats>
TarkovDevProfileClient::GetProfileForAccountId(const std::string& accountId)
{
    if (accountId.empty())
        return std::nullopt;

    // Make sure conversion is safe
    long long aid = 0;
    try
    {
        aid = std::stoll(accountId);
    }
    catch (...)
    {
        std::cerr << "[ProfileClient] Invalid AID: " << accountId << std::endl;
        return std::nullopt;
    }

    return FetchProfile(aid);
}

std::optional<PlayerProfileStats> TarkovDevProfileClient::FetchProfile(long long accountId)
{
    std::string url = "https://players.tarkov.dev/profile/" + std::to_string(accountId) + ".json";

    long httpCode = 0;
    std::string body = HttpGet(url, httpCode);

    if (httpCode == 404)
    {
        std::cout << "[ProfileClient] AID not found" << std::endl;
        return std::nullopt;
    }

    if (httpCode < 200 || httpCode >= 300 || body.empty())
    {
        std::cerr << "[ProfileClient] HTTP error: " << httpCode << std::endl;
        return std::nullopt;
    }

    try
    {
        json j = json::parse(body);

        PlayerProfileStats info;

        
        info.aid = j.value("aid", 0LL);

        if (j.contains("info"))
        {
            auto& i = j["info"];
            info.nickname = i.value("nickname", "");
            info.side = i.value("side", "");
            info.experience = i.value("experience", 0);
            info.memberCategory = i.value("memberCategory", 0);
            info.selectedMemberCategory = i.value("selectedMemberCategory", 0);
            info.prestigeLevel = i.value("prestigeLevel", 0);
        }

        
        if (j.contains("pmcStats") && j["pmcStats"].contains("eft"))
        {
            auto& eft = j["pmcStats"]["eft"];

            long long totalSeconds = eft.value("totalInGameTime", 0LL);
            info.hoursPlayed = static_cast<uint32_t>(totalSeconds / 3600);

            
            if (eft.contains("overAllCounters") &&
                eft["overAllCounters"].contains("Items"))
            {
                for (auto& item : eft["overAllCounters"]["Items"])
                {
                    // Extract Key list
                    std::vector<std::string> keyList;
                    if (item.contains("Key"))
                    {
                        for (auto& k : item["Key"])
                        {
                            keyList.emplace_back(k.get<std::string>());
                        }
                    }

                    int value = item.value("Value", 0);

                    if (keyList.size() == 1 && keyList[0] == "Kills")
                        info.Kills = value;
                    else if (keyList.size() == 1 && keyList[0] == "Deaths")
                        info.deathsPMC = value;
                    else if (keyList.size() == 1 && keyList[0] == "KilledPmc")
                        info.killedPMC = value;
                    else if (keyList == std::vector<std::string>{"ExitStatus", "Survived", "Pmc"})
                        info.survivedRaids = value;
                    else if (keyList == std::vector<std::string>{"ExitStatus", "Killed", "Pmc"})
                        info.killedInRaids = value;
                    else if (keyList == std::vector<std::string>{"ExitStatus", "Runner", "Pmc"})
                        info.runsThrough = value;
                    else if (keyList == std::vector<std::string>{"Sessions", "Pmc"})
                        info.totalRaids = value;
                    else
                        info.otherCounters.emplace_back(keyList, value);
                }
            }
        }

        long long epochMs = j.value("updated", 0LL);
        info.updated = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(epochMs)
        );

        std::cout << "[ProfileClient] Got Profile: " << info.nickname
            << " | Hours: " << info.hoursPlayed
            << " | Kills: " << info.Kills
            << " | KilledPMC: " << info.killedPMC
            << " | Deaths: " << info.deathsPMC
            << std::endl;

        return info;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[ProfileClient] JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }
}

std::string TarkovDev::loadJsonQuests()
{
    curl_global_init(CURL_GLOBAL_ALL);

    std::ostringstream oss;
    const std::string url = "https://api.tarkov.dev/graphql?query=%7Btasks%28gameMode%3Aregular%29%7Bid%20name%20objectives%7Bid%20type%20...%20on%20TaskObjectiveBasic%7Bzones%7Bposition%7Bx%20y%20z%7Dmap%7BnameId%7D%7D%7D...%20on%20TaskObjectiveItem%7Bitem%7Bid%7Dzones%7Bmap%7BnameId%7Dposition%7Bx%20y%20z%7D%7D%7D...%20on%20TaskObjectiveMark%7Bzones%7Bposition%7Bx%20y%20z%7Dmap%7BnameId%7D%7D%7D...%20on%20TaskObjectiveQuestItem%7BquestItem%7Bid%7Dmaps%7BnameId%7D%7D%7D%7D%7D";

    try
    {
        CURLcode res = curl_read(url, oss, 3);

        if (res == CURLE_OK)
        {
            const std::string response = oss.str();

            auto json = nlohmann::json::parse(response);

            if (!json.contains("data") || !json["data"].contains("tasks"))
            {
                LOGS.logError("[TDEV][JSON] API response missing data.tasks");
                throw std::runtime_error("Invalid API JSON");
            }

            tarkovDevDataTasks = json["data"]["tasks"];

            /* Save to cache file */
            std::ofstream out(TDEV_CACHE_FILE_TASK, std::ios::binary);
            if (out.is_open())
            {
                out << response;
                out.close();
                LOGS.logInfo("[TDEV][JSON] API data cached to file");
            }
            else
            {
                LOGS.logError("[TDEV][JSON] Failed to write cache file");
            }

            curl_global_cleanup();
            return response;
        }

        LOGS.logError(
            "[TDEV][JSON] API request failed Tasks: " +
            std::string(curl_easy_strerror(res)) +
            " — attempting cache load"
        );

        std::ifstream in(TDEV_CACHE_FILE_TASK, std::ios::binary);
        if (!in.is_open())
        {
            LOGS.logError("[TDEV][JSON] No cached JSON file Tasks available");
            curl_global_cleanup();
            return "";
        }

        std::stringstream fileBuffer;
        fileBuffer << in.rdbuf();
        in.close();

        auto json = nlohmann::json::parse(fileBuffer.str());

        if (!json.contains("data") || !json["data"].contains("tasks"))
        {
            LOGS.logError("[TDEV][JSON] Task Cached JSON invalid or corrupt");
            curl_global_cleanup();
            return "";
        }

        tarkovDevDataTasks = json["data"]["tasks"];
        LOGS.logInfo("[TDEV][JSON] Tasks Loaded data from local cache");

        curl_global_cleanup();
        return fileBuffer.str();
    }
    catch (const std::exception& e)
    {
        LOGS.logError(std::string("[TDEV][JSON] Tasks Exception: ") + e.what());
    }
    catch (...)
    {
        LOGS.logError("[TDEV][JSON] Tasks Unknown exception");
    }

    curl_global_cleanup();
    return "";
}

void TarkovDev::buildTasksList()
{
    tarkovDevTasksData.clear();

    //tasks are at [data][tasks] in the response
    const auto* tasksPtr = &tarkovDevDataTasks;

    if (tarkovDevDataTasks.is_object() &&
        tarkovDevDataTasks.contains("data") &&
        tarkovDevDataTasks["data"].is_object() &&
        tarkovDevDataTasks["data"].contains("tasks"))
    {
        tasksPtr = &tarkovDevDataTasks["data"]["tasks"];
    }

    if (!tasksPtr->is_array())
    {
        LOGS.logError("[TASKS][BUILD] JSON not in expected array format at [data][tasks]");
        return;
    }

    for (const auto& t : *tasksPtr)
    {
        TarkovDevTasks task;

        task.qID = t.value("id", "");
        task.qName = t.value("name", "");

        // objectives
        if (t.contains("objectives") && t["objectives"].is_array())
        {
            for (const auto& o : t["objectives"])
            {
                TarkovObjective obj;

                obj.type = o.value("type", "");
                obj.id = o.value("id", "");

                // item { id }
                if (o.contains("item") && o["item"].is_object())
                {
                    obj.itemId = o["item"].value("id", "");
                }

                // questItem { id }
                if (o.contains("questItem") && o["questItem"].is_object())
                {
                    obj.questItemId = o["questItem"].value("id", "");
                }

                // maps: [ { nameId }, ... ]
                if (o.contains("maps") && o["maps"].is_array())
                {
                    obj.maps.reserve(o["maps"].size());
                    for (const auto& m : o["maps"])
                    {
                        obj.maps.emplace_back(m.value("nameId", ""));
                    }
                }

                // zones: [ { position {x,y,z}, map {nameId} }, ... ]
                if (o.contains("zones") && o["zones"].is_array())
                {
                    obj.zones.reserve(o["zones"].size());
                    for (const auto& z : o["zones"])
                    {
                        TarkovZone zone;

                        // position
                        if (z.contains("position") && z["position"].is_object())
                        {
                            zone.position = glm::vec3{
                                z["position"].value("x", 0.f),
                                z["position"].value("y", 0.f),
                                z["position"].value("z", 0.f)
                            };
                        }

                        // map { nameId }
                        if (z.contains("map") && z["map"].is_object())
                        {
                            zone.mapNameId = z["map"].value("nameId", "");
                        }

                        obj.zones.emplace_back(std::move(zone));
                    }
                }

                task.objectives.emplace_back(std::move(obj));
            }
        }

        tarkovDevTasksData.emplace_back(std::move(task));
    }

    if (!tarkovDevTasksData.empty())
        LOGS.logInfo("[TASKS][BUILD] info updated (" + std::to_string(tarkovDevTasksData.size()) + ")");
    else
        LOGS.logError("[TASKS][BUILD] No data parsed");
}

std::string TarkovDev::loadJsonExfils()
{
    curl_global_init(CURL_GLOBAL_ALL);

    std::ostringstream oss;
    const std::string url =
        "https://api.tarkov.dev/graphql?query=%7B%20maps%20%7B%20extracts%20%7B%20name%20faction%20position%20%7B%20x%20y%20z%20%7D%20%7D%20transits%20%7B%20description%20position%20%7B%20x%20y%20z%20%7D%20%7D%20nameId%20%7D%20%7D";

    try
    {
        CURLcode res = curl_read(url, oss, 3);

        if (res == CURLE_OK)
        {
            const std::string response = oss.str();

            auto json = nlohmann::json::parse(response);

            if (!json.contains("data") || !json["data"].contains("maps"))
            {
                LOGS.logError("[TDEV][JSON] API response missing data.maps");
                throw std::runtime_error("Invalid API JSON");
            }

            tarkovDevData = json["data"]["maps"];

            /* Save to cache file */
            std::ofstream out(TDEV_CACHE_FILE, std::ios::binary);
            if (out.is_open())
            {
                out << response;
                out.close();
                LOGS.logInfo("[TDEV][JSON] API data cached to file");
            }
            else
            {
                LOGS.logError("[TDEV][JSON] Failed to write cache file");
            }

            curl_global_cleanup();
            return response;
        }

        
        //ONLINE FAILURE
        LOGS.logError(
            "[TDEV][JSON] API request failed: " +
            std::string(curl_easy_strerror(res)) +
            " — attempting cache load"
        );

        std::ifstream in(TDEV_CACHE_FILE, std::ios::binary);
        if (!in.is_open())
        {
            LOGS.logError("[TDEV][JSON] No cached JSON file available");
            curl_global_cleanup();
            return "";
        }

        std::stringstream fileBuffer;
        fileBuffer << in.rdbuf();
        in.close();

        auto json = nlohmann::json::parse(fileBuffer.str());

        if (!json.contains("data") || !json["data"].contains("maps"))
        {
            LOGS.logError("[TDEV][JSON] Cached JSON invalid or corrupt");
            curl_global_cleanup();
            return "";
        }

        tarkovDevData = json["data"]["maps"];
        LOGS.logInfo("[TDEV][JSON] Loaded data from local cache");

        curl_global_cleanup();
        return fileBuffer.str();
    }
    catch (const std::exception& e)
    {
        LOGS.logError(std::string("[TDEV][JSON] Exception: ") + e.what());
    }
    catch (...)
    {
        LOGS.logError("[TDEV][JSON] Unknown exception");
    }

    curl_global_cleanup();
    return "";
}

size_t TarkovDev::data_write(void* buf, size_t size, size_t nmemb, void* userp)
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

CURLcode TarkovDev::curl_read(const std::string& url, std::ostream& os, long timeout = 30)
{
    CURLcode code(CURLE_FAILED_INIT);
    CURL* curl = curl_easy_init();

    if (curl)
    {
        if (CURLE_OK == (code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, TarkovDev::data_write))
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