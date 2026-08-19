#pragma once

#include "../memory/DmaScheduler.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

enum class TaskPriority : std::uint8_t
{
    Critical = 0,
    High = 1,
    Normal = 2,
    Background = 3
};

struct TaskOptions
{
    TaskPriority priority = TaskPriority::Normal;
    DmaPriority dmaPriority = DmaPriority::Normal;
    bool deferWhenDmaBusy = false;
    double phaseOffsetMs = 0.0;
};

struct TimedTask
{
    std::string name;
    std::function<void()> function;
    const double* interval = nullptr;
    TaskOptions options{};
    std::chrono::steady_clock::time_point nextRun{};
    std::size_t insertionOrder = 0;
};

class TaskManager
{
public:
    void addTask(
        std::string name,
        std::function<void()> function,
        const double* interval,
        TaskOptions options = {});

    void removeTask(const std::string& name);
    void run(std::stop_token stopToken = {});

private:
    using Clock = std::chrono::steady_clock;

    [[nodiscard]] static std::chrono::duration<double, std::milli>
        GetInterval(const TimedTask& task);

    void RunTask(TimedTask& task);
    void ScheduleNextRun(TimedTask& task, Clock::time_point completedAt);

    std::vector<TimedTask> tasks;
    std::size_t nextInsertionOrder = 0;
};
