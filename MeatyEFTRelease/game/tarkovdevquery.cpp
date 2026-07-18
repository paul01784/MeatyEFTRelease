
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

namespace
{
    class CurlGlobalGuard final
    {
    public:
        CurlGlobalGuard()
            : result_(curl_global_init(CURL_GLOBAL_ALL))
        {
        }

        ~CurlGlobalGuard()
        {
            if (result_ == CURLE_OK)
                curl_global_cleanup();
        }

        CURLcode result() const noexcept
        {
            return result_;
        }

    private:
        CURLcode result_ = CURLE_FAILED_INIT;
    };

    CurlGlobalGuard& GetCurlGlobalGuard()
    {
        static CurlGlobalGuard guard;
        return guard;
    }
}

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
    const auto loadFromCache = [&]() -> std::string
        {
            std::ifstream in(TDEV_CACHE_FILE_TASK, std::ios::binary);

            if (!in.is_open())
            {
                LOGS.logError("[TDEV][JSON] No cached task JSON available");

                return "";
            }

            std::stringstream fileBuffer;
            fileBuffer << in.rdbuf();
            in.close();

            const std::string cachedResponse = fileBuffer.str();

            auto cachedJson = nlohmann::json::parse(cachedResponse, nullptr, false);

            if (cachedJson.is_discarded())
            {
                LOGS.logError("[TDEV][JSON] Cached task JSON could not be parsed");

                return "";
            }

            if (!cachedJson.is_object() ||
                !cachedJson.contains("data") ||
                !cachedJson["data"].is_object() ||
                !cachedJson["data"].contains("tasks") ||
                !cachedJson["data"]["tasks"].is_array())
            {
                LOGS.logError("[TDEV][JSON] Cached task JSON is invalid or corrupt");

                return "";
            }

            tarkovDevDataTasks = cachedJson["data"]["tasks"];

            LOGS.logInfo("[TDEV][JSON] Tasks loaded from local cache");

            return cachedResponse;
        };

    CurlGlobalGuard& curlGlobal = GetCurlGlobalGuard();

    if (curlGlobal.result() != CURLE_OK)
    {
        LOGS.logError("[TDEV][CURL] curl_global_init failed: " + std::string(curl_easy_strerror(curlGlobal.result())));

        return loadFromCache();
    }

    static constexpr const char* apiUrl = "https://api.tarkov.dev/graphql";

    static constexpr const char* query = R"graphql(
query Tasks {
    tasks(gameMode: regular) {
        id
        name

        objectives {
            id
            type

            ... on TaskObjectiveBasic {
                zones {
                    position {
                        x
                        y
                        z
                    }

                    map {
                        nameId
                    }
                }
            }

            ... on TaskObjectiveItem {
                item {
                    id
                }

                items {
                    id
                }

                zones {
                    map {
                        nameId
                    }

                    position {
                        x
                        y
                        z
                    }
                }
            }

            ... on TaskObjectiveMark {
                zones {
                    position {
                        x
                        y
                        z
                    }

                    map {
                        nameId
                    }
                }
            }

            ... on TaskObjectiveQuestItem {
                questItem {
                    id
                }

                maps {
                    nameId
                }
            }
        }
    }
}
)graphql";

    nlohmann::json requestBody;

    requestBody["operationName"] = "Tasks";
    requestBody["query"] = query;
    requestBody["variables"] = nlohmann::json::object();

    const std::string postData = requestBody.dump();

    std::ostringstream responseStream;

    long httpStatus = 0;

    const CURLcode curlResult = curl_read(apiUrl, responseStream, 30, postData, &httpStatus);

    const std::string response = responseStream.str();

    const auto saveErrorResponse = [&]()
        {
            if (response.empty())
                return;

            std::ofstream errorFile("logs\\tarkov_tasks_api_error.txt", std::ios::binary | std::ios::trunc);

            if (errorFile.is_open())
                errorFile << response;
        };

    if (curlResult != CURLE_OK)
    {
        LOGS.logError("[TDEV][JSON] Task API transport failed: " + std::string(curl_easy_strerror(curlResult)) + " — attempting cache load");

        saveErrorResponse();
        return loadFromCache();
    }

    if (httpStatus < 200 || httpStatus >= 300)
    {
        LOGS.logError("[TDEV][JSON] Task API returned HTTP " + std::to_string(httpStatus) + " — attempting cache load"
        );

        if (httpStatus == 403)
        {
            LOGS.logError("[TDEV][JSON] HTTP 403: request blocked by server/Cloudflare");
        }
        else if (httpStatus == 429)
        {
            LOGS.logError("[TDEV][JSON] HTTP 429: too many requests");
        }

        saveErrorResponse();
        return loadFromCache();
    }

    if (response.empty())
    {
        LOGS.logError("[TDEV][JSON] Task API returned an empty response");

        return loadFromCache();
    }

    auto json = nlohmann::json::parse(response, nullptr, false);

    if (json.is_discarded())
    {
        LOGS.logError("[TDEV][JSON] Task API returned non-JSON data");

        saveErrorResponse();
        return loadFromCache();
    }

    if (json.contains("errors") &&
        json["errors"].is_array() &&
        !json["errors"].empty())
    {
        std::string errorText = json["errors"].dump();

        if (errorText.size() > 1500)
        {
            errorText.resize(1500);
            errorText += "...";
        }

        LOGS.logError("[TDEV][JSON] GraphQL errors: " + errorText);
    }

    if (!json.is_object() ||
        !json.contains("data") ||
        !json["data"].is_object() ||
        !json["data"].contains("tasks") ||
        !json["data"]["tasks"].is_array())
    {
        LOGS.logError("[TDEV][JSON] API response missing a valid data.tasks array");

        saveErrorResponse();
        return loadFromCache();
    }

    tarkovDevDataTasks = json["data"]["tasks"];

    std::ofstream out(TDEV_CACHE_FILE_TASK, std::ios::binary | std::ios::trunc);

    if (out.is_open())
    {
        out << response;
        out.close();

        LOGS.logInfo("[TDEV][JSON] Task API data cached to file");
    }
    else
    {
        LOGS.logError("[TDEV][JSON] Failed to write task cache file");
    }

    LOGS.logInfo("[TDEV][JSON] Task API request completed with HTTP " + std::to_string(httpStatus));

    return response;
}

void TarkovDev::buildTasksList()
{
    tarkovDevTasksData.clear();

    const nlohmann::json* tasksPtr = &tarkovDevDataTasks;

    if (tarkovDevDataTasks.is_object() &&
        tarkovDevDataTasks.contains("data") &&
        tarkovDevDataTasks["data"].is_object() &&
        tarkovDevDataTasks["data"].contains("tasks"))
    {
        tasksPtr =
            &tarkovDevDataTasks["data"]["tasks"];
    }

    if (!tasksPtr->is_array())
    {
        LOGS.logError("[TASKS][BUILD] Task JSON is not an array");

        return;
    }

    const auto readString = [](const nlohmann::json& object, const char* key) -> std::string
        {
            if (!object.is_object())
                return "";

            const auto it = object.find(key);

            if (it == object.end() || !it->is_string())
                return "";

            return it->get<std::string>();
        };

    const auto readFloat = [](const nlohmann::json& object, const char* key) -> float
        {
            if (!object.is_object())
                return 0.0f;

            const auto it = object.find(key);

            if (it == object.end() || !it->is_number())
                return 0.0f;

            return it->get<float>();
        };

    tarkovDevTasksData.reserve(tasksPtr->size());

    for (const auto& taskJson : *tasksPtr)
    {
        if (!taskJson.is_object())
            continue;

        TarkovDevTasks task{};

        task.qID = readString(taskJson, "id");

        task.qName = readString(taskJson, "name");

        const auto objectivesIt = taskJson.find("objectives");

        if (objectivesIt != taskJson.end() && objectivesIt->is_array())
        {
            task.objectives.reserve(objectivesIt->size());

            for (const auto& objectiveJson : *objectivesIt)
            {
                if (!objectiveJson.is_object())
                    continue;

                TarkovObjective objective{};

                objective.type = readString(objectiveJson, "type");

                objective.id = readString(objectiveJson, "id");

                const auto itemIt = objectiveJson.find("item");

                if (itemIt != objectiveJson.end() && itemIt->is_object())
                {
                    objective.itemId = readString(*itemIt, "id");
                }

                if (objective.itemId.empty())
                {
                    const auto itemsIt = objectiveJson.find("items");

                    if (itemsIt != objectiveJson.end() && itemsIt->is_array() && !itemsIt->empty())
                    {
                        const auto& firstItem = itemsIt->front();

                        if (firstItem.is_object())
                        {
                            objective.itemId = readString(firstItem, "id");
                        }
                    }
                }

                const auto questItemIt = objectiveJson.find("questItem");

                if (questItemIt != objectiveJson.end() && questItemIt->is_object())
                {
                    objective.questItemId = readString(*questItemIt, "id");
                }

                const auto mapsIt = objectiveJson.find("maps");

                if (mapsIt != objectiveJson.end() && mapsIt->is_array())
                {
                    objective.maps.reserve(mapsIt->size());

                    for (const auto& mapJson : *mapsIt)
                    {
                        const std::string mapNameId = readString(mapJson, "nameId");

                        if (!mapNameId.empty())
                        {
                            objective.maps.emplace_back(mapNameId);
                        }
                    }
                }

                const auto zonesIt = objectiveJson.find("zones");

                if (zonesIt != objectiveJson.end() && zonesIt->is_array())
                {
                    objective.zones.reserve(zonesIt->size());

                    for (const auto& zoneJson : *zonesIt)
                    {
                        if (!zoneJson.is_object())
                            continue;

                        TarkovZone zone{};

                        const auto positionIt = zoneJson.find("position");

                        if (positionIt != zoneJson.end() && positionIt->is_object())
                        {
                            zone.position = glm::vec3{
                                readFloat(*positionIt, "x"),
                                readFloat(*positionIt, "y"),
                                readFloat(*positionIt, "z")
                            };
                        }

                        const auto mapIt = zoneJson.find("map");

                        if (mapIt != zoneJson.end() && mapIt->is_object())
                        {
                            zone.mapNameId = readString(*mapIt, "nameId");
                        }

                        objective.zones.emplace_back(std::move(zone));
                    }
                }

                task.objectives.emplace_back(std::move(objective));
            }
        }

        tarkovDevTasksData.emplace_back(std::move(task));
    }

    if (!tarkovDevTasksData.empty())
    {
        LOGS.logInfo("[TASKS][BUILD] Task data updated (" + std::to_string(tarkovDevTasksData.size()) + ")");
    }
    else
    {
        LOGS.logError("[TASKS][BUILD] No task data parsed");
    }
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

CURLcode TarkovDev::curl_read(const std::string& url, std::ostream& os, long timeout, const std::string& postData, long* httpStatus)
{
    if (httpStatus)
        *httpStatus = 0;

    CURL* curl = curl_easy_init();

    if (!curl)
        return CURLE_FAILED_INIT;

    CURLcode code = CURLE_OK;
    curl_slist* headers = nullptr;

    char errorBuffer[CURL_ERROR_SIZE]{};

    const auto appendHeader = [&](const char* value) -> bool
        {
            curl_slist* updatedHeaders = curl_slist_append(headers, value);

            if (!updatedHeaders)
                return false;

            headers = updatedHeaders;
            return true;
        };

    do
    {
        if (!appendHeader("Accept: application/json"))
        {
            code = CURLE_OUT_OF_MEMORY;
            break;
        }

        if (!postData.empty())
        {
            if (!appendHeader("Content-Type: application/json"))
            {
                code = CURLE_OUT_OF_MEMORY;
                break;
            }
        }

        code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, TarkovDev::data_write);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &os);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "Meaty/1.0");
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        if (code != CURLE_OK)
            break;

        code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        if (code != CURLE_OK)
            break;

        if (!postData.empty())
        {
            code = curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (code != CURLE_OK)
                break;

            code = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
            if (code != CURLE_OK)
                break;

            code = curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postData.size()));
            if (code != CURLE_OK)
                break;
        }

        code = curl_easy_perform(curl);

        long responseCode = 0;

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        if (httpStatus)
            *httpStatus = responseCode;

        if (code != CURLE_OK && errorBuffer[0] != '\0')
        {
            LOGS.logError(std::string("[TDEV][CURL] ") + errorBuffer);
        }
    } while (false);

    if (headers)
        curl_slist_free_all(headers);

    curl_easy_cleanup(curl);

    return code;
}