#include "taskManager.h"

#include "debug.h"
#include "globals.h"
#include "perfMonitor.h"
#include "../memory/Memory.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace
{
    constexpr auto kMaximumIdleSleep = std::chrono::milliseconds(5);
    constexpr auto kDeferredDmaRetry = std::chrono::milliseconds(5);
    constexpr auto kNonCriticalWorkBudget = std::chrono::milliseconds(4);

    [[nodiscard]] int PriorityRank(TaskPriority priority) noexcept
    {
        return static_cast<int>(priority);
    }
}

void TaskManager::addTask(std::string name, std::function<void()> function, const double* interval, TaskOptions options)
{
    auto existing = std::find_if(
        tasks.begin(),
        tasks.end(),
        [&](const TimedTask& task)
        {
            return task.name == name;
        });

    TimedTask task{};
    task.name = std::move(name);
    task.function = std::move(function);
    task.interval = interval;
    task.options = options;

    if (existing != tasks.end())
    {
        task.insertionOrder = existing->insertionOrder;
        *existing = std::move(task);
        return;
    }

    task.insertionOrder = nextInsertionOrder++;
    tasks.emplace_back(std::move(task));
}

void TaskManager::removeTask(const std::string& name)
{
    std::erase_if(
        tasks,
        [&](const TimedTask& task)
        {
            return task.name == name;
        });
}

std::chrono::duration<double, std::milli> TaskManager::GetInterval(const TimedTask& task)
{
    if (!task.interval || !std::isfinite(*task.interval))
        return std::chrono::duration<double, std::milli>::zero();

    return std::chrono::duration<double, std::milli>(
        (std::max)(0.0, *task.interval));
}

void TaskManager::RunTask(TimedTask& task)
{
    const auto started = Clock::now();
    const ScopedDmaPriority dmaPriority(task.options.dmaPriority);

    try
    {
        task.function();
    }
    catch (const std::exception& exception)
    {
        LOGS.logError(
            "[TaskManager] Task '" + task.name +
            "' threw exception: " + exception.what());
    }
    catch (...)
    {
        LOGS.logError(
            "[TaskManager] Task '" + task.name +
            "' threw an unknown exception.");
    }

    const double durationMs =
        std::chrono::duration<double, std::milli>(
            Clock::now() - started).count();

    PerfMonitor::Instance().Record("task." + task.name, durationMs);
}

void TaskManager::ScheduleNextRun(TimedTask& task, Clock::time_point completedAt)
{
    const auto interval = GetInterval(task);

    if (interval <= decltype(interval)::zero())
    {
        task.nextRun = completedAt + kMaximumIdleSleep;
        return;
    }

    const auto clockInterval =
        std::chrono::duration_cast<Clock::duration>(interval);

    // Preserve the task's phase while deliberately skipping missed cycles
    do
    {
        task.nextRun += clockInterval;
    }
    while (task.nextRun <= completedAt);
}

void TaskManager::run(std::stop_token stopToken)
{
    const auto startedAt = Clock::now();

    for (TimedTask& task : tasks)
    {
        const auto interval = GetInterval(task);
        const auto phase = std::chrono::duration<double, std::milli>(
            (std::max)(0.0, task.options.phaseOffsetMs));

        task.nextRun = startedAt +
            std::chrono::duration_cast<Clock::duration>(interval + phase);
    }

    while (!stopToken.stop_requested() && appGlobals::runThreads.load(std::memory_order_acquire))
    {
        auto now = Clock::now();
        std::vector<TimedTask*> dueTasks;
        dueTasks.reserve(tasks.size());

        for (TimedTask& task : tasks)
        {
            if (!task.function || GetInterval(task).count() <= 0.0)
                continue;

            if (task.nextRun <= now)
                dueTasks.emplace_back(&task);
        }

        std::stable_sort(
            dueTasks.begin(),
            dueTasks.end(),
            [](const TimedTask* left, const TimedTask* right)
            {
                const int leftPriority = PriorityRank(left->options.priority);
                const int rightPriority = PriorityRank(right->options.priority);

                if (leftPriority != rightPriority)
                    return leftPriority < rightPriority;

                if (left->nextRun != right->nextRun)
                    return left->nextRun < right->nextRun;

                return left->insertionOrder < right->insertionOrder;
            });

        bool backgroundTaskRan = false;
        bool nonCriticalTaskRan = false;
        Clock::time_point nonCriticalWorkStarted{};

        for (TimedTask* task : dueTasks)
        {
            if (stopToken.stop_requested() ||
                !appGlobals::runThreads.load(std::memory_order_acquire))
            {
                break;
            }

            now = Clock::now();

            if (task->options.priority != TaskPriority::Critical)
            {
                if (nonCriticalTaskRan &&
                    now - nonCriticalWorkStarted >= kNonCriticalWorkBudget)
                {
                    
                    break;
                }

                if (!nonCriticalTaskRan)
                {
                    nonCriticalTaskRan = true;
                    nonCriticalWorkStarted = now;
                }
            }

            if (task->options.priority == TaskPriority::Background)
            {
                if (backgroundTaskRan)
                {
                    task->nextRun = now + kDeferredDmaRetry;
                    continue;
                }

                if (task->options.deferWhenDmaBusy &&
                    mem.ShouldDeferDmaWork(task->options.dmaPriority))
                {
                    task->nextRun = now + kDeferredDmaRetry;
                    continue;
                }

                backgroundTaskRan = true;
            }

            RunTask(*task);
            ScheduleNextRun(*task, Clock::now());
        }

        now = Clock::now();
        auto wakeAt = now + kMaximumIdleSleep;

        for (const TimedTask& task : tasks)
        {
            if (!task.function || GetInterval(task).count() <= 0.0)
                continue;

            wakeAt = (std::min)(wakeAt, task.nextRun);
        }

        if (wakeAt > now)
            std::this_thread::sleep_until(wakeAt);
        else
            std::this_thread::yield();
    }
}
