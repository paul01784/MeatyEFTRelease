#include "headers/tarkovdevquery.h"

#include "../app/debug.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

TarkovDev tarkovDev;
std::vector<TarkovDevTasks> tarkovDevTasksData;
std::vector<gameItemList> marketList;
std::vector<gameCatList> catList;

const std::vector<long long> LevelXpThresholds = {
    0, 1000, 4017, 8432, 14256, 21477, 30023, 39936, 51204, 63723,
    77563, 93279, 115302, 143253, 177337, 217885, 264432, 316851,
    374400, 437465, 505161, 577978, 656347, 741150, 836066, 944133,
    1066259, 1199423, 1343743, 1499338, 1666320, 1846664, 2043349,
    2258436, 2492126, 2750217, 3032022, 3337766, 3663831, 4010401,
    4377662, 4765799, 5182399, 5627732, 6102063, 6630287, 7189442,
    7779792, 8401607, 9055144, 9740666, 10458431, 11219666, 12024744,
    12874041, 13767918, 14706741, 15690872, 16720667, 17816442,
    19041492, 20360945, 21792266, 23350443, 25098462, 27100775,
    29581231, 33028574, 37953544, 44260543, 51901513, 60887711,
    71228846, 82933459, 96009180, 110462910, 126300949, 144924572,
    172016256
};

namespace
{
    constexpr const char* TASKS_URL = "https://json.tarkov.dev/regular/tasks";
    constexpr const char* ITEMS_URL = "https://json.tarkov.dev/regular/items";

    constexpr const char* TASKS_CACHE_FILE = "tarkovdev_tasks_cache.json";
    constexpr const char* ITEMS_CACHE_FILE = "market_items_cache.json";

    constexpr auto CACHE_MAX_AGE = std::chrono::hours(48);

    json tarkovDevDataTasks = json::array();
    json tarkovDevDataItems = json::array();
    json tarkovDevItemCategoriesById = json::object();

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

    bool IsCacheFresh(const std::filesystem::path& path)
    {
        std::error_code ec;

        if (!std::filesystem::is_regular_file(path, ec) || ec)
            return false;

        const auto modified = std::filesystem::last_write_time(path, ec);
        if (ec)
            return false;

        const auto now = std::filesystem::file_time_type::clock::now();
        if (modified > now)
            return true;

        return (now - modified) <= CACHE_MAX_AGE;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open())
            return {};

        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    bool WriteTextFileAtomically(const std::filesystem::path& path, const std::string& contents)
    {
        const std::filesystem::path tempPath = path.string() + ".tmp";

        {
            std::ofstream out(
                tempPath,
                std::ios::binary | std::ios::trunc);

            if (!out.is_open())
                return false;

            out.write(
                contents.data(),
                static_cast<std::streamsize>(contents.size()));

            if (!out.good())
            {
                out.close();
                std::error_code removeError;
                std::filesystem::remove(tempPath, removeError);
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::remove(path, ec);

        ec.clear();
        std::filesystem::rename(tempPath, path, ec);

        if (ec)
        {
            std::error_code removeError;
            std::filesystem::remove(tempPath, removeError);
            return false;
        }

        return true;
    }

    const json* FindCollection(const json& root, bool tasks)
    {
        if (root.is_array())
            return &root;

        const auto findInObject = [tasks](const json& object) -> const json*
            {
                if (!object.is_object())
                    return nullptr;

                if (tasks)
                {
                    const auto it = object.find("tasks");
                    if (it != object.end() && (it->is_array() || it->is_object()))
                        return &(*it);
                }
                else
                {
                    const auto itemsIt = object.find("items");
                    if (itemsIt != object.end() &&
                        (itemsIt->is_array() || itemsIt->is_object()))
                    {
                        return &(*itemsIt);
                    }

                    //allows an existing GraphQL cache to remain usable
                    const auto oldItemsIt = object.find("itemsByType");
                    if (oldItemsIt != object.end() &&
                        (oldItemsIt->is_array() || oldItemsIt->is_object()))
                    {
                        return &(*oldItemsIt);
                    }
                }

                return nullptr;
            };

        const auto dataIt = root.find("data");
        if (dataIt != root.end())
        {
            if (const json* collection = findInObject(*dataIt))
                return collection;
        }

        return findInObject(root);
    }

    bool CopyCollectionToArray(const json& collection, json& destination)
    {
        if (collection.is_array())
        {
            destination = collection;
            return true;
        }

        if (!collection.is_object())
            return false;

        destination = json::array();

        for (auto it = collection.begin(); it != collection.end(); ++it)
        {
            if (!it.value().is_object())
                continue;

            json entry = it.value();

            if ((!entry.contains("id") || !entry["id"].is_string()) &&
                !it.key().empty())
            {
                entry["id"] = it.key();
            }

            destination.emplace_back(std::move(entry));
        }

        return true;
    }

    void CaptureItemCategories(const json& root)
    {
        tarkovDevItemCategoriesById = json::object();

        const json* payload = &root;
        const auto dataIt = root.find("data");
        if (dataIt != root.end() && dataIt->is_object())
            payload = &(*dataIt);

        if (!payload->is_object())
            return;

        const auto categoriesIt = payload->find("itemCategories");
        if (categoriesIt == payload->end())
            return;

        if (categoriesIt->is_object())
        {
            for (auto it = categoriesIt->begin(); it != categoriesIt->end(); ++it)
            {
                if (!it.value().is_object())
                    continue;

                json category = it.value();
                if ((!category.contains("id") || !category["id"].is_string()) &&
                    !it.key().empty())
                {
                    category["id"] = it.key();
                }

                tarkovDevItemCategoriesById[it.key()] = std::move(category);
            }
        }
        else if (categoriesIt->is_array())
        {
            for (const auto& category : *categoriesIt)
            {
                if (!category.is_object())
                    continue;

                const auto idIt = category.find("id");
                if (idIt != category.end() && idIt->is_string())
                    tarkovDevItemCategoriesById[idIt->get<std::string>()] = category;
            }
        }
    }

    void ApplyTranslationLookup(json& value, const json& translations)
    {
        if (!translations.is_object())
            return;

        if (value.is_string())
        {
            const std::string key = value.get<std::string>();
            const auto translatedIt = translations.find(key);

            if (translatedIt != translations.end() && translatedIt->is_string())
                value = translatedIt->get<std::string>();

            return;
        }

        if (value.is_array())
        {
            for (auto& entry : value)
                ApplyTranslationLookup(entry, translations);

            return;
        }

        if (value.is_object())
        {
            for (auto it = value.begin(); it != value.end(); ++it)
                ApplyTranslationLookup(it.value(), translations);
        }
    }

    std::string ApplyEnglishTranslations(const std::string& baseResponse, const std::string& translationResponse)
    {
        if (baseResponse.empty() || translationResponse.empty())
            return baseResponse;

        json baseRoot = json::parse(baseResponse, nullptr, false);
        json translationRoot = json::parse(translationResponse, nullptr, false);

        if (baseRoot.is_discarded() || translationRoot.is_discarded() ||
            !baseRoot.is_object() || !translationRoot.is_object())
        {
            return baseResponse;
        }

        const auto translationDataIt = translationRoot.find("data");
        if (translationDataIt == translationRoot.end() ||
            !translationDataIt->is_object())
        {
            return baseResponse;
        }

        auto baseDataIt = baseRoot.find("data");
        if (baseDataIt == baseRoot.end())
            return baseResponse;

        ApplyTranslationLookup(*baseDataIt, *translationDataIt);
        return baseRoot.dump();
    }

    bool ParseDataset(const std::string& raw, bool tasks, json& destination)
    {
        if (raw.empty())
            return false;

        json root = json::parse(raw, nullptr, false);
        if (root.is_discarded())
            return false;

        if (!tasks)
            CaptureItemCategories(root);

        const json* collection = FindCollection(root, tasks);
        if (!collection)
            return false;

        return CopyCollectionToArray(*collection, destination);
    }

    void SaveErrorBody(const char* filename, const std::string& body)
    {
        if (body.empty())
            return;

        std::error_code ec;
        std::filesystem::create_directories("logs", ec);

        std::ofstream out(
            std::filesystem::path("logs") / filename,
            std::ios::binary | std::ios::trunc);

        if (out.is_open())
            out << body;
    }

    std::string ReadString(const json& object, const char* key)
    {
        if (!object.is_object())
            return {};

        const auto it = object.find(key);
        if (it == object.end() || !it->is_string())
            return {};

        return it->get<std::string>();
    }

    std::string ReadReferenceId(const json& value)
    {
        if (value.is_string())
            return value.get<std::string>();

        if (!value.is_object())
            return {};

        std::string id = ReadString(value, "id");
        if (id.empty())
            id = ReadString(value, "nameId");

        return id;
    }

    std::string ReadCategoryName(const json& category)
    {
        const json* resolved = &category;

        if (category.is_string())
        {
            const std::string categoryId = category.get<std::string>();
            const auto categoryIt = tarkovDevItemCategoriesById.find(categoryId);

            if (categoryIt == tarkovDevItemCategoriesById.end() ||
                !categoryIt->is_object())
            {
                return {};
            }

            resolved = &(*categoryIt);
        }

        if (!resolved->is_object())
            return {};

        std::string name = ReadString(*resolved, "name");
        if (name.empty())
            name = ReadString(*resolved, "normalizedName");

        return name;
    }

    long ReadLong(const json& object, const char* key, long fallback = 0)
    {
        if (!object.is_object())
            return fallback;

        const auto it = object.find(key);
        if (it == object.end() || !it->is_number())
            return fallback;

        try
        {
            return it->get<long>();
        }
        catch (...)
        {
            return fallback;
        }
    }

    float ReadFloat(const json& object, const char* key, float fallback = 0.0f)
    {
        if (!object.is_object())
            return fallback;

        const auto it = object.find(key);
        if (it == object.end() || !it->is_number())
            return fallback;

        try
        {
            return it->get<float>();
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::string ReadMapId(const json& map)
    {
        std::string value = ReadString(map, "nameId");
        if (value.empty())
            value = ReadString(map, "id");
        return value;
    }

    long ReadMarketPrice(const json& item)
    {
        long price = ReadLong(item, "avg24hPrice", 0);
        if (price > 0)
            return price;

        const auto fleaIt = item.find("fleaMarket");
        if (fleaIt != item.end() && fleaIt->is_object())
        {
            price = ReadLong(*fleaIt, "avg24hPrice", 0);
            if (price > 0)
                return price;
        }

        const auto fleaDataIt = item.find("fleaMarketData");
        if (fleaDataIt != item.end() && fleaDataIt->is_object())
        {
            price = ReadLong(*fleaDataIt, "avg24hPrice", 0);
            if (price > 0)
                return price;
        }

        return 0;
    }

    int CalculateLevel(int experience)
    {
        int level = 1;

        for (std::size_t i = 0; i < LevelXpThresholds.size(); ++i)
        {
            if (static_cast<long long>(experience) < LevelXpThresholds[i])
                break;

            level = static_cast<int>(i + 1);
        }

        return level;
    }

    size_t ProfileWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        if (!userp)
            return 0;

        const size_t totalSize = size * nmemb;
        static_cast<std::string*>(userp)->append(
            static_cast<const char*>(contents),
            totalSize);

        return totalSize;
    }
}

std::string TarkovDevProfileClient::HttpGet(const std::string& url, long& httpCode)
{
    httpCode = 0;
    std::string response;

    CurlGlobalGuard& curlGlobal = GetCurlGlobalGuard();
    if (curlGlobal.result() != CURLE_OK)
    {
        LOGS.logError("[PROFILE][CURL] curl_global_init failed: " + std::string(curl_easy_strerror(curlGlobal.result())));
        return response;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
        return response;

    char errorBuffer[CURL_ERROR_SIZE]{};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Meaty/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "identity");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ProfileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    const CURLcode result = curl_easy_perform(curl);

    if (result == CURLE_OK)
    {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    }
    else
    {
        const std::string detail =
            errorBuffer[0] != '\0'
            ? std::string(errorBuffer)
            : std::string(curl_easy_strerror(result));

        LOGS.logError("[PROFILE][CURL] " + detail);
    }

    curl_easy_cleanup(curl);
    return response;
}

std::optional<PlayerProfileStats> TarkovDevProfileClient::GetProfileForAccountId(const std::string& accountId)
{
    if (accountId.empty())
        return std::nullopt;

    long long aid = 0;

    try
    {
        aid = std::stoll(accountId);
    }
    catch (...)
    {
        LOGS.logError("[PROFILE] Invalid account ID: " + accountId);
        return std::nullopt;
    }

    return FetchProfile(aid);
}

std::optional<PlayerProfileStats> TarkovDevProfileClient::FetchProfile(long long accountId)
{
    if (accountId <= 0)
        return std::nullopt;

    const std::string url = "https://players.tarkov.dev/profile/" + std::to_string(accountId) + ".json";

    long httpCode = 0;
    const std::string body = HttpGet(url, httpCode);

    if (httpCode == 404)
    {
        LOGS.logInfo("[PROFILE] Account ID not found");
        return std::nullopt;
    }

    if (httpCode < 200 || httpCode >= 300 || body.empty())
    {
        LOGS.logError("[PROFILE] HTTP error: " + std::to_string(httpCode));
        return std::nullopt;
    }

    const json root = json::parse(body, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        LOGS.logError("[PROFILE] Invalid JSON response");
        return std::nullopt;
    }

    PlayerProfileStats info{};
    info.aid = root.value("aid", 0LL);

    const auto infoIt = root.find("info");
    if (infoIt != root.end() && infoIt->is_object())
    {
        info.nickname = infoIt->value("nickname", "");
        info.side = infoIt->value("side", "");
        info.experience = infoIt->value("experience", 0);
        info.memberCategory = infoIt->value("memberCategory", 0);
        info.selectedMemberCategory = infoIt->value("selectedMemberCategory", 0);
        info.prestigeLevel = infoIt->value("prestigeLevel", 0);
    }

    info.level = CalculateLevel(info.experience);

    const auto pmcStatsIt = root.find("pmcStats");
    if (pmcStatsIt != root.end() && pmcStatsIt->is_object())
    {
        const auto eftIt = pmcStatsIt->find("eft");
        if (eftIt != pmcStatsIt->end() && eftIt->is_object())
        {
            const long long totalSeconds =
                eftIt->value("totalInGameTime", 0LL);

            info.hoursPlayed = static_cast<std::uint32_t>(
                std::max(0LL, totalSeconds) / 3600LL);

            const auto countersIt = eftIt->find("overAllCounters");
            if (countersIt != eftIt->end() && countersIt->is_object())
            {
                const auto itemsIt = countersIt->find("Items");
                if (itemsIt != countersIt->end() && itemsIt->is_array())
                {
                    for (const auto& item : *itemsIt)
                    {
                        if (!item.is_object())
                            continue;

                        std::vector<std::string> keyList;

                        const auto keyIt = item.find("Key");
                        if (keyIt != item.end() && keyIt->is_array())
                        {
                            keyList.reserve(keyIt->size());

                            for (const auto& key : *keyIt)
                            {
                                if (key.is_string())
                                    keyList.emplace_back(key.get<std::string>());
                            }
                        }

                        const int value = item.value("Value", 0);

                        if (keyList == std::vector<std::string>{ "Kills" })
                            info.Kills = static_cast<std::uint32_t>(std::max(0, value));
                        else if (keyList == std::vector<std::string>{ "Deaths" })
                            info.deathsPMC = static_cast<std::uint32_t>(std::max(0, value));
                        else if (keyList == std::vector<std::string>{ "KilledPmc" } ||
                            keyList == std::vector<std::string>{ "KilledPMC" })
                            info.killedPMC = static_cast<std::uint32_t>(std::max(0, value));
                        else if (keyList == std::vector<std::string>{ "ExitStatus", "Survived", "Pmc" })
                            info.survivedRaids = static_cast<std::uint32_t>(std::max(0, value));
                        else if (keyList == std::vector<std::string>{ "ExitStatus", "Killed", "Pmc" })
                            info.killedInRaids = static_cast<std::uint32_t>(std::max(0, value));
                        else if (keyList == std::vector<std::string>{ "ExitStatus", "Runner", "Pmc" })
                            info.runsThrough = static_cast<std::uint32_t>(std::max(0, value));
                        else if (keyList == std::vector<std::string>{ "Sessions", "Pmc" })
                            info.totalRaids = static_cast<std::uint32_t>(std::max(0, value));
                        else
                            info.otherCounters.emplace_back(std::move(keyList), value);
                    }
                }
            }
        }
    }

    const long long epochMs = root.value("updated", 0LL);
    info.updated = std::chrono::system_clock::time_point(std::chrono::milliseconds(epochMs));

    LOGS.logInfo("[PROFILE] Loaded " + info.nickname + " | Level: " + std::to_string(info.level) + " | Hours: " + std::to_string(info.hoursPlayed));

    return info;
}

bool TarkovDev::Initialize(bool forceRefresh)
{
    const bool tasksOk = !loadJsonQuests(forceRefresh).empty();
    if (tasksOk)
        buildTasksList();

    const bool itemsOk = !loadJsonItems(forceRefresh).empty();
    if (itemsOk)
    {
        buildItemList();
        buildCatList();
    }

    return tasksOk && itemsOk;
}

std::string TarkovDev::loadJsonQuests(bool forceRefresh)
{
    return loadDataset(Dataset::Tasks, forceRefresh);
}

std::string TarkovDev::loadJsonItems(bool forceRefresh)
{
    return loadDataset(Dataset::Items, forceRefresh);
}

std::string TarkovDev::loadDataset(Dataset dataset, bool forceRefresh)
{
    const bool isTasks = dataset == Dataset::Tasks;

    bool& loaded = isTasks ? tasksLoaded_ : itemsLoaded_;
    std::string& rawStorage = isTasks ? tasksRawJson_ : itemsRawJson_;
    json& destination = isTasks ? tarkovDevDataTasks : tarkovDevDataItems;

    const char* label = isTasks ? "TASKS" : "MARKET";
    const char* url = isTasks ? TASKS_URL : ITEMS_URL;
    const std::filesystem::path cacheFile =
        isTasks ? TASKS_CACHE_FILE : ITEMS_CACHE_FILE;

    if (loaded && !forceRefresh)
        return rawStorage;

    const auto tryCache = [&](bool requireFresh) -> std::string
        {
            if (requireFresh && !IsCacheFresh(cacheFile))
                return {};

            const std::string cached = ReadTextFile(cacheFile);
            if (cached.empty())
                return {};

            json parsedArray;
            if (!ParseDataset(cached, isTasks, parsedArray))
            {
                LOGS.logError(std::string("[") + label + "][CACHE] Cached JSON is invalid");
                return {};
            }

            destination = std::move(parsedArray);
            rawStorage = cached;
            loaded = true;

            LOGS.logInfo(std::string("[") + label + "][CACHE] Loaded cached JSON");

            return cached;
        };

    if (!forceRefresh)
    {
        const std::string freshCache = tryCache(true);
        if (!freshCache.empty())
            return freshCache;
    }

    CurlGlobalGuard& curlGlobal = GetCurlGlobalGuard();
    if (curlGlobal.result() != CURLE_OK)
    {
        LOGS.logError(std::string("[") + label + "][CURL] curl_global_init failed: " + curl_easy_strerror(curlGlobal.result()));

        const std::string staleCache = tryCache(false);
        if (!staleCache.empty())
            return staleCache;

        return rawStorage;
    }

    std::ostringstream responseStream;
    long httpStatus = 0;

    const CURLcode curlResult = curl_read(
        url,
        responseStream,
        30,
        &httpStatus);

    const std::string response = responseStream.str();

    if (curlResult == CURLE_OK &&
        httpStatus >= 200 &&
        httpStatus < 300)
    {
        std::string processedResponse = response;

        const json baseRoot = json::parse(response, nullptr, false);
        if (!baseRoot.is_discarded() && baseRoot.is_object())
        {
            const auto pathsIt = baseRoot.find("translations");
            if (pathsIt != baseRoot.end() &&
                pathsIt->is_array() &&
                !pathsIt->empty())
            {
                std::ostringstream translationStream;
                long translationHttpStatus = 0;
                const std::string translationUrl = std::string(url) + "_en";

                const CURLcode translationResult = curl_read(
                    translationUrl,
                    translationStream,
                    30,
                    &translationHttpStatus);

                if (translationResult == CURLE_OK &&
                    translationHttpStatus >= 200 &&
                    translationHttpStatus < 300)
                {
                    processedResponse = ApplyEnglishTranslations(
                        response,
                        translationStream.str());
                }
                else
                {
                    LOGS.logError(std::string("[") + label + "][JSON] English translation lookup failed; using raw values");
                }
            }
        }

        json parsedArray;

        if (ParseDataset(processedResponse, isTasks, parsedArray))
        {
            destination = std::move(parsedArray);
            rawStorage = processedResponse;
            loaded = true;

            if (WriteTextFileAtomically(cacheFile, processedResponse))
            {
                LOGS.logInfo(std::string("[") + label + "][CACHE] Refreshed cache from JSON endpoint");
            }
            else
            {
                LOGS.logError(std::string("[") + label + "][CACHE] Failed to write cache file");
            }

            return processedResponse;
        }

        LOGS.logError(std::string("[") + label + "][JSON] Response did not contain the expected " +  (isTasks ? "data.tasks" : "data.items") + " collection");

        SaveErrorBody(
            isTasks
            ? "tarkov_tasks_api_error.txt"
            : "tarkov_items_api_error.txt",
            response);
    }
    else
    {
        if (curlResult != CURLE_OK)
        {
            LOGS.logError(std::string("[") + label + "][CURL] Request failed: " + curl_easy_strerror(curlResult));
        }
        else
        {
            LOGS.logError(std::string("[") + label + "][HTTP] Request returned HTTP " + std::to_string(httpStatus));
        }

        SaveErrorBody(
            isTasks
            ? "tarkov_tasks_api_error.txt"
            : "tarkov_items_api_error.txt",
            response);
    }

    const std::string staleCache = tryCache(false);
    if (!staleCache.empty())
        return staleCache;

    if (!rawStorage.empty() && destination.is_array())
        return rawStorage;

    LOGS.logError(std::string("[") + label + "][JSON] No usable API response or cache");

    return {};
}

void TarkovDev::buildTasksList()
{
    tarkovDevTasksData.clear();

    if (!tarkovDevDataTasks.is_array())
    {
        LOGS.logError("[TASKS][BUILD] Task JSON is not an array");
        return;
    }

    tarkovDevTasksData.reserve(tarkovDevDataTasks.size());

    for (const auto& taskJson : tarkovDevDataTasks)
    {
        if (!taskJson.is_object())
            continue;

        TarkovDevTasks task{};
        task.qID = ReadString(taskJson, "id");
        task.qName = ReadString(taskJson, "name");

        const auto objectivesIt = taskJson.find("objectives");
        if (objectivesIt != taskJson.end() && objectivesIt->is_array())
        {
            task.objectives.reserve(objectivesIt->size());

            for (const auto& objectiveJson : *objectivesIt)
            {
                if (!objectiveJson.is_object())
                    continue;

                TarkovObjective objective{};
                objective.id = ReadString(objectiveJson, "id");
                objective.type = ReadString(objectiveJson, "type");

                if (objective.type.empty())
                    objective.type = ReadString(objectiveJson, "__typename");

                const auto itemIt = objectiveJson.find("item");
                if (itemIt != objectiveJson.end())
                    objective.itemId = ReadReferenceId(*itemIt);

                if (objective.itemId.empty())
                {
                    const auto itemsIt = objectiveJson.find("items");
                    if (itemsIt != objectiveJson.end() &&
                        itemsIt->is_array() &&
                        !itemsIt->empty())
                    {
                        objective.itemId = ReadReferenceId(itemsIt->front());
                    }
                }

                const auto questItemIt = objectiveJson.find("questItem");
                if (questItemIt != objectiveJson.end())
                    objective.questItemId = ReadReferenceId(*questItemIt);

                const auto mapsIt = objectiveJson.find("maps");
                if (mapsIt != objectiveJson.end() && mapsIt->is_array())
                {
                    objective.maps.reserve(mapsIt->size());

                    for (const auto& mapJson : *mapsIt)
                    {
                        const std::string mapId = ReadReferenceId(mapJson);
                        if (!mapId.empty())
                            objective.maps.emplace_back(mapId);
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
                                ReadFloat(*positionIt, "x"),
                                ReadFloat(*positionIt, "y"),
                                ReadFloat(*positionIt, "z")
                            };
                        }

                        const auto mapIt = zoneJson.find("map");
                        if (mapIt != zoneJson.end())
                            zone.mapNameId = ReadReferenceId(*mapIt);

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

void TarkovDev::buildItemList()
{
    marketList.clear();

    if (!tarkovDevDataItems.is_array())
    {
        LOGS.logError("[MARKET][BUILD] Item JSON is not an array");
        return;
    }

    marketList.reserve(tarkovDevDataItems.size());

    for (const auto& itemJson : tarkovDevDataItems)
    {
        if (!itemJson.is_object())
            continue;

        gameItemList item{};
        item.bsgid = ReadString(itemJson, "id");

        if (item.bsgid.empty())
            continue;

        if (item.bsgid == "mosinscopedbarter0000001" ||
            item.bsgid == "5648b2414bdc2d3b4c8b4578" ||
            item.bsgid == "5648b6ff4bdc2d3d1c8b4581" ||
            item.bsgid == "59984b4286f77445bd2d4a07")
        {
            continue;
        }

        item.name = ReadString(itemJson, "name");
        item.shortName = ReadString(itemJson, "shortName");
        item.traderPrice = ReadLong(itemJson, "basePrice", 0);
        item.marketPrice = ReadMarketPrice(itemJson);

        const auto categoriesIt = itemJson.find("categories");
        if (categoriesIt != itemJson.end() && categoriesIt->is_array())
        {
            for (const auto& categoryJson : *categoriesIt)
            {
                std::string categoryName = ReadCategoryName(categoryJson);

                if (!categoryName.empty() &&
                    std::find(
                        item.bsgCategory.begin(),
                        item.bsgCategory.end(),
                        categoryName) == item.bsgCategory.end())
                {
                    item.bsgCategory.emplace_back(std::move(categoryName));
                }
            }
        }

        const auto categoryIt = itemJson.find("category");
        if (categoryIt != itemJson.end())
        {
            std::string categoryName = ReadCategoryName(*categoryIt);

            if (!categoryName.empty() &&
                std::find(
                    item.bsgCategory.begin(),
                    item.bsgCategory.end(),
                    categoryName) == item.bsgCategory.end())
            {
                item.bsgCategory.emplace_back(std::move(categoryName));
            }
        }

        marketList.emplace_back(std::move(item));
    }

    std::sort(
        marketList.begin(),
        marketList.end(),
        [](const gameItemList& lhs, const gameItemList& rhs)
        {
            return lhs.name < rhs.name;
        });

    if (!marketList.empty())
    {
        LOGS.logInfo("[MARKET][BUILD] Market data updated (" + std::to_string(marketList.size()) + ")");
    }
    else
    {
        LOGS.logError("[MARKET][BUILD] No item data parsed");
    }
}

void TarkovDev::buildCatList()
{
    catList.clear();

    std::set<std::string> uniqueCategories;

    for (const auto& item : marketList)
    {
        for (const auto& category : item.bsgCategory)
        {
            if (!category.empty())
                uniqueCategories.insert(category);
        }
    }

    catList.reserve(uniqueCategories.size() + 1);
    catList.push_back(gameCatList{ 0, "None" });

    long id = 1;
    for (const auto& category : uniqueCategories)
        catList.push_back(gameCatList{ id++, category });

    if (!catList.empty())
    {
        LOGS.logInfo("[MARKET][BUILD] Category list created (" + std::to_string(catList.size()) + ")");
    }
    else
    {
        LOGS.logError("[MARKET][BUILD] Category list failed");
    }
}

std::string TarkovDev::BSGidToName(const std::string& bsgid) const
{
    if (bsgid.empty())
        return "ERR";

    if (bsgid.find("5d52cc5ba4b9367408500062") != std::string::npos)
        return "AGS-30";

    if (bsgid.find("5cdeb229d7f00c000e7ce174") != std::string::npos)
        return "NSV";

    const auto itemIt = std::find_if(
        marketList.begin(),
        marketList.end(),
        [&bsgid](const gameItemList& item)
        {
            return item.bsgid == bsgid;
        });

    if (itemIt != marketList.end())
        return itemIt->shortName;

    return "NoData";
}

long TarkovDev::MarketPrice(const std::string& bsgid) const
{
    const auto itemIt = std::find_if(
        marketList.begin(),
        marketList.end(),
        [&bsgid](const gameItemList& item)
        {
            return item.bsgid == bsgid;
        });

    if (itemIt == marketList.end())
        return 0;

    return std::max(0L, itemIt->marketPrice);
}

size_t TarkovDev::data_write(void* buf, size_t size, size_t nmemb, void* userp)
{
    if (!userp)
        return 0;

    std::ostream& stream = *static_cast<std::ostream*>(userp);
    const std::streamsize length =
        static_cast<std::streamsize>(size * nmemb);

    if (!stream.write(static_cast<char*>(buf), length))
        return 0;

    return static_cast<size_t>(length);
}

CURLcode TarkovDev::curl_read(const std::string& url, std::ostream& os, long timeout, long* httpStatus)
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
            curl_slist* updated = curl_slist_append(headers, value);
            if (!updated)
                return false;

            headers = updated;
            return true;
        };

    do
    {
        if (!appendHeader("Accept: application/json"))
        {
            code = CURLE_OUT_OF_MEMORY;
            break;
        }

        code = curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, TarkovDev::data_write);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &os);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_USERAGENT, "Meaty/1.0");
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        if (code != CURLE_OK) break;

        code = curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        if (code != CURLE_OK) break;

        code = curl_easy_perform(curl);

        long responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        if (httpStatus)
            *httpStatus = responseCode;

        if (code != CURLE_OK && errorBuffer[0] != '\0')
            LOGS.logError(std::string("[TDEV][CURL] ") + errorBuffer);
    } while (false);

    if (headers)
        curl_slist_free_all(headers);

    curl_easy_cleanup(curl);
    return code;
}

std::string loadjson(bool forceRefresh)
{
    return tarkovDev.loadJsonItems(forceRefresh);
}

void buildCatList()
{
    tarkovDev.buildCatList();
}

void buildItemList()
{
    tarkovDev.buildItemList();
}

std::string BSGidToName(const std::string& bsgid)
{
    return tarkovDev.BSGidToName(bsgid);
}

int Marketprice(const std::string& bsgid)
{
    const long price = tarkovDev.MarketPrice(bsgid);

    if (price > static_cast<long>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();

    return static_cast<int>(price);
}