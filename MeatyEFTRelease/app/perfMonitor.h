#pragma once

#include "debug.h"

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct PerfSample
{
    std::string name;
    std::string detail;
    double durationMs = 0.0;
    double timestampSec = 0.0;
};

class PerfMonitor
{
public:
    static constexpr double kSlowTaskMs = 25.0;
    static constexpr double kSlowScatterMs = 35.0;

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
        }

        if (durationMs < kSlowTaskMs)
            return;

        static std::mutex logMutex;
        static std::chrono::steady_clock::time_point lastLog{};
        const auto now = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock(logMutex);
            if (lastLog != std::chrono::steady_clock::time_point{} &&
                (now - lastLog) < std::chrono::milliseconds(750))
            {
                return;
            }

            lastLog = now;
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
    }

private:
    PerfMonitor() = default;

    static double ElapsedSeconds()
    {
        using Clock = std::chrono::steady_clock;
        static const Clock::time_point start = Clock::now();
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    static constexpr size_t kMaxSamples = 80;

    mutable std::mutex mutex_;
    std::deque<PerfSample> samples_;
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
