#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct UpdateResult
{
    bool success = false;

    std::size_t filesChecked = 0;
    std::size_t filesUpdated = 0;
    std::size_t filesSkipped = 0;
    std::size_t filesFailed = 0;

    std::string error;
    std::vector<std::string> warnings;
};

class FileUpdater
{
public:
    using StatusCallback = std::function<void(const std::wstring&)>;

    UpdateResult Synchronise(const std::filesystem::path& applicationDirectory, const std::string& manifestUrl, const StatusCallback& statusCallback = {});

private:
    struct ManifestEntry
    {
        std::string path;
        std::string url;
        std::string sha256;
        std::string mode;

        bool required = true;
    };

    static void ProcessEntry(const ManifestEntry& entry, const std::filesystem::path& applicationDirectory, UpdateResult& result, const StatusCallback& statusCallback);

};