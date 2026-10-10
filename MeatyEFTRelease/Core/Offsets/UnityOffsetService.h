#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct UnityOffsetView
{
    std::string group;
    std::string name;
    std::string module;
    std::string kind;
    std::string method;
    std::uint64_t value{};
    bool runtimeValidated{};
};

class UnityOffsetService
{
public:
    static UnityOffsetService& Instance();

    UnityOffsetService(const UnityOffsetService&) = delete;
    UnityOffsetService& operator=(const UnityOffsetService&) = delete;

    bool Load();
    bool StartResolve();
    bool ResolveForFullScan();

    [[nodiscard]] bool IsResolving() const noexcept;
    [[nodiscard]] std::string Status() const;
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::filesystem::path FilePath() const;
    [[nodiscard]] std::vector<UnityOffsetView> Entries() const;

private:
    UnityOffsetService() = default;
    ~UnityOffsetService();

    void ResolveWorker();
    bool ResolveNow();
    void SetStatus(std::string status, std::string error = {});

    std::atomic_bool resolving_{false};
    mutable std::mutex mutex_;
    std::thread worker_;
    std::string status_ = "Using built-in Unity offsets";
    std::string lastError_;
    std::vector<UnityOffsetView> entries_;
};
