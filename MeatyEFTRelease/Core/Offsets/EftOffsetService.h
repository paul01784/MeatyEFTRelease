#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct EftOffsetView
{
    std::string group;
    std::string name;
    std::string className;
    std::string fieldName;
    std::uint64_t value{};
    bool typeIndex{};
    bool staticField{};
};

class EftOffsetService
{
public:
    static EftOffsetService& Instance();

    EftOffsetService(const EftOffsetService&) = delete;
    EftOffsetService& operator=(const EftOffsetService&) = delete;

    bool Load();
    void RefreshJsonText();
    bool StartResolve();

    [[nodiscard]] bool IsResolving() const noexcept;
    [[nodiscard]] std::string Status() const;
    [[nodiscard]] std::string LastError() const;
    [[nodiscard]] std::string JsonText() const;
    [[nodiscard]] std::filesystem::path FilePath() const;
    [[nodiscard]] std::vector<EftOffsetView> Entries() const;

private:
    EftOffsetService() = default;
    ~EftOffsetService();

    void ResolveWorker();
    void SetStatus(std::string status, std::string error = {});

    std::atomic_bool resolving_{false};
    mutable std::mutex mutex_;
    std::thread worker_;
    std::string status_ = "Using built-in offsets";
    std::string lastError_;
    std::string jsonText_;
};

