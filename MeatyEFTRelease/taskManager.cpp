#include "app/taskManager.h"
#include "app/globals.h"
#include "app/debug.h"
#include "app/perfMonitor.h"

bool IsHeavyDmaTask(const std::string& name)
{
    return name == "lootTask" ||
        name == "questTask" ||
        name == "ExplosiveManagerTask" ||
        name == "PlayerEquipmentTask";
}

void TaskManager::addTask(const std::string& name, std::function<void()> func, const double* interval, double phaseOffsetMs) {
    TimedTask task;
    task.function = std::move(func);
    task.interval = interval;
    task.elapsedTime = -phaseOffsetMs;
    task.phaseOffsetMs = phaseOffsetMs;
    tasks[name] = std::move(task);
}

void TaskManager::removeTask(const std::string& name) {
    tasks.erase(name);
}

void TaskManager::run()
{
    previousTime = std::chrono::steady_clock::now();

    while (appGlobals::runThreads.load(std::memory_order_acquire))
    {
        try
        {
            const auto currentTime = std::chrono::steady_clock::now();

            double deltaTime =
                std::chrono::duration<double, std::milli>(
                    currentTime - previousTime
                ).count();

            previousTime = currentTime;

            deltaTime = std::clamp(deltaTime, 0.0, 100.0);

            updateTasks(deltaTime);

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        catch (const std::exception& e)
        {
            LOGS.logError(
                "Exception caught in TaskManager: " +
                std::string(e.what()) +
                ". Retrying..."
            );

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        catch (...)
        {
            LOGS.logError("Unknown exception caught in TaskManager. Retrying...");

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void TaskManager::updateTasks(double deltaTime)
{
    auto IsDue = [](const TimedTask& task) -> bool
        {
            return task.interval &&
                task.function &&
                *task.interval > 0.0 &&
                task.elapsedTime >= *task.interval;
        };

    auto RunTask = [&](const std::string& name, TimedTask& task)
        {
            const auto started = std::chrono::steady_clock::now();

            try
            {
                task.function();
            }
            catch (const std::exception& e)
            {
                LOGS.logError(
                    "[TaskManager] Task '" + name +
                    "' threw exception: " + e.what()
                );
            }
            catch (...)
            {
                LOGS.logError(
                    "[TaskManager] Task '" + name +
                    "' threw an unknown exception."
                );
            }

            const double durationMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started
                ).count();

            PerfMonitor::Instance().Record("task." + name, durationMs);

            // Skip missed cycles rather than running back-to-back trying to catch up
            task.elapsedTime = 0.0;
        };

    // First, advance every valid task timer
    for (auto& [name, task] : tasks)
    {
        if (!task.interval || !task.function || *task.interval <= 0.0)
            continue;

        task.elapsedTime += deltaTime;
    }

    // Camera is critical: run it first whenever it is due
    auto cameraIt = tasks.find("cameraTask");

    if (cameraIt != tasks.end() && IsDue(cameraIt->second))
        RunTask(cameraIt->first, cameraIt->second);

    // Find the single most overdue real heavy task
    std::string selectedHeavyTask;
    double highestOverdue = -1.0;

    for (auto& [name, task] : tasks)
    {
        if (!IsHeavyDmaTask(name) || !IsDue(task))
            continue;

        const double overdue = task.elapsedTime - *task.interval;

        if (overdue > highestOverdue)
        {
            highestOverdue = overdue;
            selectedHeavyTask = name;
        }
    }

    // Run all due non-heavy tasks, plus only one due heavy task
    for (auto& [name, task] : tasks)
    {
        if (name == "cameraTask")
            continue;

        if (!IsDue(task))
            continue;

        if (IsHeavyDmaTask(name) &&
            !selectedHeavyTask.empty() &&
            name != selectedHeavyTask)
        {
            continue;
        }

        RunTask(name, task);
    }
}
