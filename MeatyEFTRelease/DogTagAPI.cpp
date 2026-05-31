#include "app/DogTagAPI.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include "app/debug.h"

using json = nlohmann::json;

DogTagAPI g_DogTagAPI("https://api1.meatyradar.co.uk");

namespace
{
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        std::string* str = static_cast<std::string*>(userp);
        str->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }

    static void secureZeroBuffer(char* data, size_t len)
    {
        volatile char* p = data;
        while (len--)
            *p++ = 0;
    }
}

DogTagAPI::DogTagAPI(const std::string& baseUrl)
    : baseUrl(baseUrl)
{
}

void DogTagAPI::setApiKey(const std::string& key)
{
    apiKey = key;
}

void DogTagAPI::clearApiKey()
{
    secureClear(apiKey);
}

bool DogTagAPI::hasApiKey() const
{
    return !apiKey.empty();
}

void DogTagAPI::secureClear(std::string& s)
{
    if (!s.empty())
        secureZeroBuffer(&s[0], s.size());

    s.clear();
}

std::optional<std::string> DogTagAPI::httpGet(const std::string& url)
{
    if (apiKey.empty())
    {
        std::cout << "[DogTagAPI] Missing API key\n";
        return std::nullopt;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
        return std::nullopt;

    std::string response;

    struct curl_slist* headers = nullptr;
    std::string auth = "Authorization: Bearer " + apiKey;

    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    secureClear(auth);

    if (res != CURLE_OK)
    {
        std::cout << "[DogTagAPI] CURL GET error: " << curl_easy_strerror(res) << "\n";
        return std::nullopt;
    }

    if (code != 200)
    {
        std::cout << "[DogTagAPI] HTTP GET error: " << code << "\n";
        std::cout << "[DogTagAPI] Response: " << response << "\n";
        return std::nullopt;
    }

    return response;
}

std::optional<std::string> DogTagAPI::httpPostJson(
    const std::string& url,
    const std::string& payload
)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return std::nullopt;

    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        std::cout << "[DogTagAPI] CURL POST error: "
            << curl_easy_strerror(res) << "\n";
        return std::nullopt;
    }

    if (code != 200)
    {
        std::cout << "[DogTagAPI] HTTP POST error: " << code << "\n";
        std::cout << "[DogTagAPI] Response: " << response << "\n";
        return std::nullopt;
    }

    return response;
}

std::optional<DogTagKeyStatus> DogTagAPI::getKeyStatus()
{
    auto res = httpGet(baseUrl + "/key/status");

    if (!res)
        return std::nullopt;

    auto j = json::parse(*res, nullptr, false);
    if (j.is_discarded())
        return std::nullopt;

    DogTagKeyStatus status;
    status.valid = j.value("valid", false);
    status.userId = j.value("userId", "");
    status.email = j.value("email", "");
    status.keyPrefix = j.value("keyPrefix", "");
    status.error = j.value("error", "");

    return status;
}

bool DogTagAPI::post(const std::string& profileId,
    const std::string& accountId,
    const std::string& nickname)
{
    json payload = {
        {"profileId", profileId},
        {"accountId", accountId},
        {"nickname", nickname}
    };

    auto res = httpPostJson(baseUrl + "/dogtag", payload.dump());
    return res.has_value();
}

std::optional<DogTagEntry> DogTagAPI::getByProfile(const std::string& profileId)
{
    try
    {
        if (apiKey.empty())
        {
            LOGS.logWarn("[DogTagAPI] getByProfile skipped, API key not set");
            return std::nullopt;
        }

        if (profileId.empty())
        {
            LOGS.logWarn("[DogTagAPI] getByProfile skipped, profileId empty");
            return std::nullopt;
        }

        auto res = httpGet(baseUrl + "/dogtag/profile/" + profileId);

        if (!res)
            return std::nullopt;

        auto j = json::parse(*res, nullptr, false);
        if (j.is_discarded())
            return std::nullopt;

        DogTagEntry e;
        e.id = j.value("id", 0);
        e.profileId = j.value("profileId", "");
        e.accountId = j.value("accountId", "");
        e.nickname = j.value("nickname", "");
        e.createdAt = j.value("createdAt", 0LL);
        e.updatedAt = j.value("updatedAt", 0LL);

        return e;
    }
    catch (const std::exception& e)
    {
        LOGS.logError("[DogTagAPI] getByProfile exception: ", e.what());
        return std::nullopt;
    }
    catch (...)
    {
        LOGS.logError("[DogTagAPI] getByProfile unknown exception");
        return std::nullopt;
    }
}

std::optional<DogTagEntry> DogTagAPI::getByAccount(const std::string& accountId)
{
    try
    {
        if (apiKey.empty())
        {
            LOGS.logWarn("[DogTagAPI] getByAccount skipped, API key not set");
            return std::nullopt;
        }

        if (accountId.empty())
        {
            LOGS.logWarn("[DogTagAPI] getByAccount skipped, accountId empty");
            return std::nullopt;
        }

        auto res = httpGet(baseUrl + "/dogtag/account/" + accountId);

        if (!res)
            return std::nullopt;

        auto j = json::parse(*res, nullptr, false);
        if (j.is_discarded())
            return std::nullopt;

        DogTagEntry e;
        e.id = j.value("id", 0);
        e.profileId = j.value("profileId", "");
        e.accountId = j.value("accountId", "");
        e.nickname = j.value("nickname", "");
        e.createdAt = j.value("createdAt", 0LL);
        e.updatedAt = j.value("updatedAt", 0LL);

    return e;

    }
    catch (const std::exception& e)
    {
        LOGS.logError("[DogTagAPI] getByAccount exception: ", e.what());
        return std::nullopt;
    }
    catch (...)
    {
        LOGS.logError("[DogTagAPI] getByAccount unknown exception");
        return std::nullopt;
    }
}