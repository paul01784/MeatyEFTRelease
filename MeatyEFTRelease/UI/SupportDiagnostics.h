#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

enum class SupportHealthLevel
{
    Good,
    Warning,
    Error,
    Inactive
};

struct SupportHealthItem
{
    std::string module;
    std::string status;
    std::string detail;
    SupportHealthLevel level{ SupportHealthLevel::Inactive };
};

struct SupportHealthSnapshot
{
    std::vector<SupportHealthItem> items;
    std::size_t warningCount{ 0 };
    std::size_t errorCount{ 0 };
};

struct SupportReportOptions
{
    bool includeRecentLogs{ true };
    bool includePlayerIdentity{ false };
};

struct SupportReportResult
{
    bool success{ false };
    std::filesystem::path textPath;
    std::filesystem::path jsonPath;
    std::string summary;
    std::string error;
};

[[nodiscard]] SupportHealthSnapshot CaptureSupportHealthSnapshot();
[[nodiscard]] std::filesystem::path GetSupportReportDirectory();
[[nodiscard]] SupportReportResult ExportSupportReport(const SupportReportOptions& options);

