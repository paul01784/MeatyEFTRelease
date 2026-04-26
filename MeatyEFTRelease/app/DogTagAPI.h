#pragma once

#include <string>
#include <vector>
#include <optional>

struct DogTagEntry
{
    int id = 0;
    std::string profileId;
    std::string accountId;
    std::string nickname;
    long long createdAt = 0;
    long long updatedAt = 0;
};

struct DogTagKeyStatus
{
    bool valid = false;
    std::string userId;
    std::string email;
    std::string keyPrefix;
    std::string error;
};

class DogTagAPI
{
public:
    explicit DogTagAPI(const std::string& baseUrl);

    void setApiKey(const std::string& key);
    void clearApiKey();
    bool hasApiKey() const;

    std::optional<DogTagKeyStatus> getKeyStatus();

    std::optional<DogTagEntry> getByProfile(const std::string& profileId);
    std::optional<DogTagEntry> getByAccount(const std::string& accountId);

    bool post(const std::string& profileId,
        const std::string& accountId,
        const std::string& nickname);

private:
    std::string baseUrl;
    std::string apiKey;

    std::optional<std::string> httpGet(const std::string& url);
    std::optional<std::string> httpPostJson(const std::string& url, const std::string& payload);

    static void secureClear(std::string& s);
};

extern DogTagAPI g_DogTagAPI;