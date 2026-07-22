#include "app/FileUpdater.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace fs = std::filesystem;

namespace
{
    bool NtSuccess(NTSTATUS status)
    {
        return status >= 0;
    }

    std::string ToLower(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(
                    std::tolower(character)
                    );
            }
        );

        return value;
    }

    std::string NormaliseHash(std::string hash)
    {
        hash.erase(
            std::remove_if(
                hash.begin(),
                hash.end(),
                [](unsigned char character)
                {
                    return std::isspace(character) != 0;
                }
            ),
            hash.end()
        );

        return ToLower(std::move(hash));
    }

    bool IsValidSha256(const std::string& hash)
    {
        if (hash.size() != 64)
            return false;

        return std::all_of(
            hash.begin(),
            hash.end(),
            [](unsigned char character)
            {
                return std::isxdigit(character) != 0;
            }
        );
    }

    void ReportStatus(const FileUpdater::StatusCallback& callback, const std::wstring& message)
    {
        if (callback)
            callback(message);
    }

    void EnsureCurlInitialised()
    {
        static std::once_flag initialisationFlag;
        static CURLcode initialisationResult = CURLE_OK;

        std::call_once(
            initialisationFlag,
            []()
            {
                initialisationResult =
                    curl_global_init(CURL_GLOBAL_DEFAULT);
            }
        );

        if (initialisationResult != CURLE_OK)
        {
            throw std::runtime_error(
                std::string("curl_global_init failed: ") +
                curl_easy_strerror(initialisationResult)
            );
        }
    }

    size_t WriteStringCallback(void* data, size_t size, size_t count, void* userData)
    {
        const size_t byteCount = size * count;

        auto* output = static_cast<std::string*>(userData);

        output->append(static_cast<const char*>(data), byteCount);

        return byteCount;
    }

    size_t WriteFileCallback(void* data, size_t size, size_t count, void* userData)
    {
        const size_t byteCount = size * count;

        auto* output = static_cast<std::ofstream*>(userData);

        output->write(
            static_cast<const char*>(data),
            static_cast<std::streamsize>(byteCount)
        );

        if (!output->good())
            return 0;

        return byteCount;
    }

    void ConfigureCurl(CURL* curl, const std::string& url, std::array<char, CURL_ERROR_SIZE>& errorBuffer)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Meaty-Updater/1.0");
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer.data());
    }

    void ValidateHttpResult(CURL* curl, CURLcode curlResult, const std::array<char, CURL_ERROR_SIZE>& errorBuffer)
    {
        if (curlResult != CURLE_OK)
        {
            std::string errorMessage;

            if (errorBuffer[0] != '\0')
                errorMessage = errorBuffer.data();
            else
                errorMessage = curl_easy_strerror(curlResult);

            throw std::runtime_error("HTTP download failed: " + errorMessage);
        }

        long responseCode = 0;

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        if (responseCode < 200 || responseCode >= 300)
        {
            throw std::runtime_error("Server returned HTTP status " + std::to_string(responseCode));
        }
    }

    std::string DownloadText(const std::string& url)
    {
        EnsureCurlInitialised();

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);

        if (!curl)
            throw std::runtime_error("curl_easy_init failed");

        std::string response;
        std::array<char, CURL_ERROR_SIZE> errorBuffer{};

        ConfigureCurl(curl.get(), url, errorBuffer);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &WriteStringCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

        const CURLcode curlResult = curl_easy_perform(curl.get());

        ValidateHttpResult(curl.get(), curlResult, errorBuffer);

        if (response.empty())
        {
            throw std::runtime_error("Downloaded manifest was empty");
        }

        return response;
    }

    void DownloadFile(const std::string& url, const fs::path& destination)
    {
        EnsureCurlInitialised();

        std::ofstream output(destination, std::ios::binary | std::ios::trunc);

        if (!output.is_open())
        {
            throw std::runtime_error("Failed to create temporary file: " + destination.string());
        }

        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), &curl_easy_cleanup);

        if (!curl)
        {
            output.close();

            throw std::runtime_error("curl_easy_init failed");
        }

        std::array<char, CURL_ERROR_SIZE> errorBuffer{};

        ConfigureCurl(curl.get(), url, errorBuffer);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, &WriteFileCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &output);

        const CURLcode curlResult = curl_easy_perform(curl.get());

        output.flush();
        const bool fileWriteSuccessful = output.good();
        output.close();

        try
        {
            ValidateHttpResult(curl.get(), curlResult, errorBuffer);
        }
        catch (...)
        {
            std::error_code removeError;
            fs::remove(destination, removeError);
            throw;
        }

        if (!fileWriteSuccessful)
        {
            std::error_code removeError;
            fs::remove(destination, removeError);

            throw std::runtime_error("Failed while writing downloaded file: " + destination.string());
        }
    }

    std::string CalculateSha256(const fs::path& filePath)
    {
        BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
        BCRYPT_HASH_HANDLE hashHandle = nullptr;

        const auto cleanup = [&]()
            {
                if (hashHandle)
                {
                    BCryptDestroyHash(hashHandle);
                    hashHandle = nullptr;
                }

                if (algorithmHandle)
                {
                    BCryptCloseAlgorithmProvider(
                        algorithmHandle,
                        0
                    );

                    algorithmHandle = nullptr;
                }
            };

        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithmHandle,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0
        );

        if (!NtSuccess(status))
        {
            cleanup();

            throw std::runtime_error(
                "BCryptOpenAlgorithmProvider failed. Status: " +
                std::to_string(status)
            );
        }

        DWORD hashObjectSize = 0;
        DWORD hashSize = 0;
        DWORD bytesCopied = 0;

        status = BCryptGetProperty(
            algorithmHandle,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&hashObjectSize),
            sizeof(hashObjectSize),
            &bytesCopied,
            0
        );

        if (!NtSuccess(status))
        {
            cleanup();

            throw std::runtime_error(
                "BCryptGetProperty(BCRYPT_OBJECT_LENGTH) failed. Status: " +
                std::to_string(status)
            );
        }

        status = BCryptGetProperty(
            algorithmHandle,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashSize),
            sizeof(hashSize),
            &bytesCopied,
            0
        );

        if (!NtSuccess(status))
        {
            cleanup();

            throw std::runtime_error(
                "BCryptGetProperty(BCRYPT_HASH_LENGTH) failed. Status: " +
                std::to_string(status)
            );
        }

        std::vector<UCHAR> hashObject(hashObjectSize);
        std::vector<UCHAR> hashBytes(hashSize);

        status = BCryptCreateHash(
            algorithmHandle,
            &hashHandle,
            hashObject.data(),
            static_cast<ULONG>(hashObject.size()),
            nullptr,
            0,
            0
        );

        if (!NtSuccess(status))
        {
            cleanup();

            throw std::runtime_error(
                "BCryptCreateHash failed. Status: " +
                std::to_string(status)
            );
        }

        std::ifstream input(
            filePath,
            std::ios::binary
        );

        if (!input.is_open())
        {
            cleanup();

            throw std::runtime_error(
                "Failed to open file for hashing: " +
                filePath.string()
            );
        }

        constexpr std::size_t bufferSize = 64 * 1024;
        std::vector<UCHAR> buffer(bufferSize);

        while (input)
        {
            input.read(
                reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size())
            );

            const std::streamsize bytesRead =
                input.gcount();

            if (bytesRead <= 0)
                break;

            status = BCryptHashData(
                hashHandle,
                buffer.data(),
                static_cast<ULONG>(bytesRead),
                0
            );

            if (!NtSuccess(status))
            {
                input.close();
                cleanup();

                throw std::runtime_error(
                    "BCryptHashData failed while hashing: " +
                    filePath.string() +
                    ". Status: " +
                    std::to_string(status)
                );
            }
        }

        if (!input.eof() && input.fail())
        {
            input.close();
            cleanup();

            throw std::runtime_error(
                "Failed while reading file for hashing: " +
                filePath.string()
            );
        }

        input.close();

        status = BCryptFinishHash(
            hashHandle,
            hashBytes.data(),
            static_cast<ULONG>(hashBytes.size()),
            0
        );

        if (!NtSuccess(status))
        {
            cleanup();

            throw std::runtime_error(
                "BCryptFinishHash failed for: " +
                filePath.string() +
                ". Status: " +
                std::to_string(status)
            );
        }

        cleanup();

        std::ostringstream output;

        output
            << std::hex
            << std::setfill('0');

        for (const UCHAR byte : hashBytes)
        {
            output
                << std::setw(2)
                << static_cast<unsigned int>(byte);
        }

        return output.str();
    }

    fs::path ValidateRelativePath(const std::string& manifestPath)
    {
        if (manifestPath.empty())
        {
            throw std::runtime_error("Manifest contains an empty file path");
        }

        fs::path relativePath = fs::path(manifestPath).lexically_normal();

        if (relativePath.empty() ||
            relativePath.is_absolute() ||
            relativePath.has_root_name() ||
            relativePath.has_root_directory())
        {
            throw std::runtime_error("Manifest contains an invalid absolute path: " + manifestPath);
        }

        for (const fs::path& component : relativePath)
        {
            if (component == L"..")
            {
                throw std::runtime_error("Manifest path attempts to leave the application directory: " + manifestPath);
            }
        }

        if (relativePath.filename().empty())
        {
            throw std::runtime_error("Manifest path does not identify a file: " + manifestPath);
        }

        return relativePath;
    }

    void ReplaceFileAtomically(const fs::path& temporaryPath, const fs::path& destinationPath)
    {
        if (!MoveFileExW(
            temporaryPath.c_str(),
            destinationPath.c_str(),
            MOVEFILE_REPLACE_EXISTING |
            MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();

            throw std::runtime_error(
                "Failed to install file: " + destinationPath.string() + ". Win32 error: " + std::to_string(error)
            );
        }
    }

    std::wstring MakeStatus(const wchar_t* prefix, const fs::path& relativePath)
    {
        std::wstring message = prefix;
        message += relativePath.generic_wstring();
        message += L"...";

        return message;
    }
}

void FileUpdater::ProcessEntry(const ManifestEntry& entry, const fs::path& applicationDirectory, UpdateResult& result, const StatusCallback& statusCallback)
{
    const fs::path relativePath = ValidateRelativePath(entry.path);

    const fs::path localPath = applicationDirectory / relativePath;

    const std::string mode = ToLower(entry.mode);

    if (mode != "replace" &&  mode != "missing_only")
    {
        throw std::runtime_error("Unknown update mode for " + entry.path + ": " + entry.mode);
    }

    std::error_code fileError;

    const bool localPathExists = fs::exists(localPath, fileError);

    if (fileError)
    {
        throw std::runtime_error("Failed to inspect local file: " + entry.path);
    }

    if (localPathExists && !fs::is_regular_file(localPath, fileError))
    {
        throw std::runtime_error("Manifest file path exists but is not a regular file: " + entry.path);
    }

    if (mode == "missing_only" && localPathExists)
    {
        result.filesSkipped++;
        return;
    }

    const std::string expectedHash = NormaliseHash(entry.sha256);

    if (!IsValidSha256(expectedHash))
    {
        throw std::runtime_error("Missing or invalid SHA-256 for file: " + entry.path);
    }

    
    if (mode == "replace" && localPathExists)
    {
        ReportStatus(statusCallback, MakeStatus(L"Checking ", relativePath));

        const std::string localHash = NormaliseHash( CalculateSha256(localPath));

        if (localHash == expectedHash)
        {
            result.filesSkipped++;
            return;
        }
    }

    if (entry.url.empty())
    {
        throw std::runtime_error("No download URL supplied for file: " + entry.path);
    }

    const fs::path parentDirectory = localPath.parent_path();

    if (!parentDirectory.empty())
    {
        fs::create_directories(parentDirectory, fileError);

        if (fileError)
        {
            throw std::runtime_error("Failed to create directory for file: " + entry.path);
        }
    }

    fs::path temporaryPath = localPath;
    temporaryPath += L".update.tmp";

    fs::remove(temporaryPath, fileError);

    ReportStatus(statusCallback, MakeStatus(L"Downloading ", relativePath));

    try
    {
        DownloadFile(entry.url, temporaryPath);

        ReportStatus(statusCallback, MakeStatus(L"Verifying ", relativePath));

        const std::string downloadedHash = NormaliseHash(CalculateSha256(temporaryPath));

        if (downloadedHash != expectedHash)
        {
            throw std::runtime_error("SHA-256 verification failed for file: " + entry.path);
        }

        ReportStatus(statusCallback, MakeStatus(L"Installing ", relativePath));

        ReplaceFileAtomically(temporaryPath, localPath);

        result.filesUpdated++;
    }
    catch (...)
    {
        std::error_code removeError;

        fs::remove(temporaryPath, removeError);

        throw;
    }
}

UpdateResult FileUpdater::Synchronise(const fs::path& applicationDirectory, const std::string& manifestUrl, const StatusCallback& statusCallback)
{
    UpdateResult result;

    try
    {
        if (applicationDirectory.empty())
        {
            throw std::runtime_error("Application directory is empty");
        }

        if (manifestUrl.empty())
        {
            throw std::runtime_error("Manifest URL is empty");
        }

        ReportStatus(statusCallback, L"Downloading update manifest...");

        const std::string manifestText = DownloadText(manifestUrl);

        nlohmann::json manifest;

        try
        {
            manifest = nlohmann::json::parse(manifestText);
        }
        catch (const std::exception& exception)
        {
            throw std::runtime_error(std::string("Failed to parse manifest.json: ") + exception.what());
        }

        const int manifestVersion = manifest.value("manifestVersion", 0);

        if (manifestVersion != 1)
        {
            throw std::runtime_error("Unsupported manifest version: " + std::to_string(manifestVersion));
        }

        if (!manifest.contains("files") || !manifest["files"].is_array())
        {
            throw std::runtime_error("Manifest does not contain a files array");
        }

        std::unordered_set<std::string> processedPaths;

        for (const auto& jsonEntry : manifest["files"])
        {
            ManifestEntry entry;

            entry.path =
                jsonEntry.at("path").get<std::string>();

            entry.url =
                jsonEntry.value(
                    "url",
                    std::string{}
                );

            entry.sha256 =
                jsonEntry.value(
                    "sha256",
                    std::string{}
                );

            entry.mode =
                jsonEntry.value(
                    "mode",
                    std::string("replace")
                );

            entry.required =
                jsonEntry.value(
                    "required",
                    true
                );

            std::string duplicateKey = ToLower(entry.path);

            std::replace(duplicateKey.begin(), duplicateKey.end(), '\\', '/');

            if (!processedPaths.insert(duplicateKey).second)
            {
                throw std::runtime_error("Manifest contains a duplicate path: " + entry.path);
            }

            result.filesChecked++;

            try
            {
                ProcessEntry(entry, applicationDirectory, result, statusCallback);
            }
            catch (const std::exception& exception)
            {
                result.filesFailed++;

                if (entry.required)
                    throw;

                result.warnings.emplace_back(entry.path + ": " + exception.what());
            }
        }

        result.success = true;

        ReportStatus(statusCallback, L"Application files are ready");
    }
    catch (const std::exception& exception)
    {
        result.success = false;
        result.error = exception.what();
    }
    catch (...)
    {
        result.success = false;
        result.error = "Unknown file updater error";
    }

    return result;
}