#pragma once

#include "debug.h"

#include <chrono>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct PerfSample
{
    std::string name;
    std::string detail;
    double durationMs = 0.0;
    double timestampSec = 0.0;
};

struct PerfMetricSnapshot
{
    std::string name;
    std::string detail;
    double lastMs = 0.0;
    double averageMs = 0.0;
    double peakMs = 0.0;
    double lastSeenSec = 0.0;
    std::uint64_t sampleCount = 0;
};

class PerfMonitor
{
public:
    static constexpr double kSlowTaskMs = 50.0;
    static constexpr double kSlowDmaLockWaitMs = 50.0;
    static constexpr double kSlowScatterMs = 75.0;

    static PerfMonitor& Instance()
    {
        static PerfMonitor monitor;
        return monitor;
    }

    void Record(const std::string& name, double durationMs, const std::string& detail = "")
    {
        const double nowSec = ElapsedSeconds();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            samples_.push_back({ name, detail, durationMs, nowSec });
            while (samples_.size() > kMaxSamples)
                samples_.pop_front();

            if (durationMs >= peakMs_)
            {
                peakMs_ = durationMs;
                peakName_ = name;
                peakDetail_ = detail;
            }

            MetricState& metric = metrics_[name];

            if (metric.sampleCount == 0)
                metric.averageMs = durationMs;
            else
                metric.averageMs = (metric.averageMs * 0.88) + (durationMs * 0.12);

            metric.lastMs = durationMs;
            metric.peakMs = (std::max)(metric.peakMs, durationMs);
            metric.lastSeenSec = nowSec;
            metric.detail = detail;
            ++metric.sampleCount;
        }

        if (durationMs < kSlowTaskMs)
            return;

        static std::mutex logMutex;
        static std::unordered_map<
            std::string,
            std::chrono::steady_clock::time_point> lastLogByMetric;
        static std::chrono::steady_clock::time_point lastGlobalLog{};
        const auto now = std::chrono::steady_clock::now();
        constexpr auto kMetricLogCooldown = std::chrono::seconds(30);
        constexpr auto kGlobalLogCooldown = std::chrono::seconds(5);

        {
            std::lock_guard<std::mutex> lock(logMutex);
            if (lastGlobalLog != std::chrono::steady_clock::time_point{} &&
                (now - lastGlobalLog) < kGlobalLogCooldown)
            {
                return;
            }

            const auto metricLog = lastLogByMetric.find(name);
            if (metricLog != lastLogByMetric.end() &&
                (now - metricLog->second) < kMetricLogCooldown)
            {
                return;
            }

            lastGlobalLog = now;
            lastLogByMetric[name] = now;
        }

        std::string message = "[PERF] " + name + " took " + std::to_string(static_cast<int>(durationMs)) + "ms";
        if (!detail.empty())
            message += " (" + detail + ")";

        LOGS.logWarn(message);
    }

    std::vector<PerfSample> GetRecent() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<PerfSample>(samples_.begin(), samples_.end());
    }

    std::vector<PerfMetricSnapshot> GetTopMetrics(
        std::string_view prefix,
        std::size_t maximumCount) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PerfMetricSnapshot> result;
        result.reserve(metrics_.size());

        for (const auto& [name, metric] : metrics_)
        {
            if (!prefix.empty() && !std::string_view(name).starts_with(prefix))
                continue;

            result.push_back({
                name,
                metric.detail,
                metric.lastMs,
                metric.averageMs,
                metric.peakMs,
                metric.lastSeenSec,
                metric.sampleCount
                });
        }

        std::sort(
            result.begin(),
            result.end(),
            [](const PerfMetricSnapshot& left, const PerfMetricSnapshot& right)
            {
                if (left.averageMs != right.averageMs)
                    return left.averageMs > right.averageMs;

                return left.peakMs > right.peakMs;
            });

        if (result.size() > maximumCount)
            result.resize(maximumCount);

        return result;
    }

    double GetPeakMs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return peakMs_;
    }

    std::string GetPeakName() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return peakName_;
    }

    std::string GetPeakDetail() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return peakDetail_;
    }

    void ResetPeak()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        peakMs_ = 0.0;
        peakName_.clear();
        peakDetail_.clear();

        for (auto& [name, metric] : metrics_)
            metric.peakMs = metric.lastMs;
    }

    void ResetStatistics()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        metrics_.clear();
        peakMs_ = 0.0;
        peakName_.clear();
        peakDetail_.clear();
    }

private:
    PerfMonitor() = default;

    struct MetricState
    {
        std::string detail;
        double lastMs = 0.0;
        double averageMs = 0.0;
        double peakMs = 0.0;
        double lastSeenSec = 0.0;
        std::uint64_t sampleCount = 0;
    };

    static double ElapsedSeconds()
    {
        using Clock = std::chrono::steady_clock;
        static const Clock::time_point start = Clock::now();
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    static constexpr size_t kMaxSamples = 80;

    mutable std::mutex mutex_;
    std::deque<PerfSample> samples_;
    std::unordered_map<std::string, MetricState> metrics_;
    double peakMs_ = 0.0;
    std::string peakName_;
    std::string peakDetail_;
};

class PerfScope
{
public:
    explicit PerfScope(std::string name, std::string detail = {})
        : name_(std::move(name)),
        detail_(std::move(detail)),
        start_(std::chrono::steady_clock::now())
    {
    }

    ~PerfScope()
    {
        const auto end = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - start_).count();
        PerfMonitor::Instance().Record(name_, ms, detail_);
    }

private:
    std::string name_;
    std::string detail_;
    std::chrono::steady_clock::time_point start_;
};
