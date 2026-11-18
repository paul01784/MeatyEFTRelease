#include "RuntimeDependencies.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    std::mutex gLoadMutex;
    std::vector<HMODULE> gModules;
    DLL_DIRECTORY_COOKIE gDllDirectory{};
    constexpr char kDiscordUrl[] = "https://discord.gg/GXmBagdP5s";
    constexpr char kMemProcFsReleaseUrl[] = "https://github.com/ufrisk/MemProcFS/releases/tag/";

    std::string WindowsError(DWORD error)
    {
        LPSTR message = nullptr;
        const DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
                                            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&message), 0, nullptr);
        std::string result = length && message ? std::string(message, length) : "Windows error " + std::to_string(error);
        if (message)
            LocalFree(message);
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
            result.pop_back();
        return result;
    }

    std::string CalculateSha256(const std::filesystem::path& path)
    {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        DWORD objectSize = 0;
        DWORD hashSize = 0;
        DWORD bytesReturned = 0;

        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("BCryptOpenAlgorithmProvider failed");

        auto close = [&]()
            {
                if (hash)
                    BCryptDestroyHash(hash);
                if (algorithm)
                    BCryptCloseAlgorithmProvider(algorithm, 0);
            };

        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &bytesReturned, 0) < 0 ||
            BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize), &bytesReturned, 0) < 0)
        {
            close();
            throw std::runtime_error("Could not prepare file verification");
        }

        std::vector<UCHAR> object(objectSize);
        std::vector<UCHAR> digest(hashSize);
        if (BCryptCreateHash(algorithm, &hash, object.data(), static_cast<ULONG>(object.size()), nullptr, 0, 0) < 0)
        {
            close();
            throw std::runtime_error("Could not start file verification");
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            close();
            throw std::runtime_error("Could not open " + path.filename().string());
        }

        std::array<char, 64 * 1024> buffer{};
        while (input)
        {
            input.read(buffer.data(), buffer.size());
            const std::streamsize count = input.gcount();
            if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0)
            {
                close();
                throw std::runtime_error("File verification failed for " + path.filename().string());
            }
        }

        if (!input.eof() || BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        {
            close();
            throw std::runtime_error("Could not finish verifying " + path.filename().string());
        }

        close();
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (const UCHAR byte : digest)
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        return stream.str();
    }

    std::string RecoveryInstructions()
    {
        std::ostringstream message;
        message << "How to get the correct files:\n"
                << " 1. Recommended: visit the MeatyEFT Discord for the current ready-to-use application files:\n"
                << "    " << kDiscordUrl << "\n\n"
                << " 2. Manual option: download the Windows x64 bundle for the required MemProcFS release from:\n"
                << "    " << kMemProcFsReleaseUrl << MemProcFSBundle::Tag << "\n\n"
                << "Replace vmm.dll, leechcore.dll, FTD3XX.dll, FTD3XXWU.dll and info.db together, then restart MeatyEFT. "
                << "Do not mix files from different releases.";
        return message.str();
    }

    std::string RecoveryMessage(const std::vector<std::string>& problems)
    {
        std::ostringstream message;
        message << "The required native memory files are missing, outdated or incompatible.\n\n"
                << "Required bundle: " << RuntimeDependencies::BundleName << "\n\nProblems:\n";
        for (const auto& problem : problems)
            message << " - " << problem << "\n";
        message << "\nStartup has been stopped before DMA was loaded.\n\n"
                << RecoveryInstructions();
        return message.str();
    }
}

RuntimeDependencies::ValidationResult RuntimeDependencies::Validate(const std::filesystem::path& applicationDirectory)
{
    std::vector<std::string> problems;
    if (applicationDirectory.empty())
        problems.emplace_back("The application directory could not be resolved");

    for (const auto& expected : MemProcFSBundle::Files)
    {
        const std::filesystem::path path = applicationDirectory / expected.name;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            problems.emplace_back(path.filename().string() + " is missing");
            continue;
        }

        try
        {
            const std::string actualHash = CalculateSha256(path);
            if (actualHash != expected.sha256)
                problems.emplace_back(path.filename().string() + " does not match the required release");
        }
        catch (const std::exception&)
        {
            problems.emplace_back(path.filename().string() + " could not be verified");
        }
    }

    if (!problems.empty())
        return {false, RecoveryMessage(problems)};
    return {true, std::string("Validated ") + BundleName};
}

bool RuntimeDependencies::Load(const std::filesystem::path& applicationDirectory, std::string& error)
{
    std::lock_guard lock(gLoadMutex);
    if (!gModules.empty())
        return true;

    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
    gDllDirectory = AddDllDirectory(applicationDirectory.c_str());

    constexpr std::array<const wchar_t*, 4> loadOrder{L"FTD3XX.dll", L"FTD3XXWU.dll", L"leechcore.dll", L"vmm.dll"};
    for (const wchar_t* name : loadOrder)
    {
        const std::filesystem::path path = applicationDirectory / name;
        HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS);
        if (!module)
        {
            const DWORD lastError = GetLastError();
            error = "Could not load " + path.filename().string() + ": " + WindowsError(lastError) +
                    "\n\nA missing Microsoft Visual C++ runtime may also cause this error.\n\n" + RecoveryInstructions();
            for (auto iterator = gModules.rbegin(); iterator != gModules.rend(); ++iterator)
                FreeLibrary(*iterator);
            gModules.clear();
            if (gDllDirectory)
            {
                RemoveDllDirectory(gDllDirectory);
                gDllDirectory = {};
            }
            return false;
        }
        gModules.push_back(module);
    }
    return true;
}

bool RuntimeDependencies::ValidateVmmHandle(VMM_HANDLE handle, std::string& error)
{
    ULONG64 major = 0;
    ULONG64 minor = 0;
    ULONG64 revision = 0;
    if (!handle || !VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_VMM_VERSION_MAJOR, &major) ||
        !VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_VMM_VERSION_MINOR, &minor) ||
        !VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_VMM_VERSION_REVISION, &revision))
    {
        error = "The loaded vmm.dll did not report its runtime version";
        return false;
    }

    if (major != MemProcFSBundle::VmmMajor || minor != MemProcFSBundle::VmmMinor || revision != MemProcFSBundle::VmmRevision)
    {
        error = "Loaded VMM runtime " + std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision) +
                " does not match the required runtime " + std::to_string(MemProcFSBundle::VmmMajor) + "." +
                std::to_string(MemProcFSBundle::VmmMinor) + "." + std::to_string(MemProcFSBundle::VmmRevision);
        return false;
    }
    return true;
}

std::string RuntimeDependencies::ExpectedHashForUpdaterPath(const std::string& relativePath)
{
    std::string normalised = relativePath;
    std::replace(normalised.begin(), normalised.end(), '\\', '/');
    std::transform(normalised.begin(), normalised.end(), normalised.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    if (normalised.find('/') != std::string::npos)
        return {};

    for (const auto& expected : MemProcFSBundle::Files)
    {
        std::string name;
        for (const wchar_t* character = expected.name; *character; ++character)
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*character))));
        if (normalised == name)
            return expected.sha256;
    }
    return {};
}
