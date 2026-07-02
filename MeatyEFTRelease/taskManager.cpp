#include "app/taskManager.h"
#include "app/globals.h"
#include "app/debug.h"
#include "app/perfMonitor.h"

namespace {

bool IsHeavyDmaTask(const std::string& name)
{
    return name == "playersTask" ||
        name == "playersBoneTask" ||
        name == "lootTask" ||
        name == "questTask" ||
        name == "ExplosiveManagerTask" ||
        name == "PlayerEquipmentTask" ||
        name == "cameraTask";
}

} // namespace

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

void TaskManager::run() {
    // Initialize the previous time
    previousTime = std::chrono::high_resolution_clock::now();

    while (appGlobals::runThreads.load(std::memory_order_acquire)) {
        try
        {
            // Calculate delta time
            auto currentTime = std::chrono::high_resolution_clock::now();
            double deltaTime = std::chrono::duration<double, std::milli>(currentTime - previousTime).count();
            previousTime = currentTime;

            // Update tasks
            updateTasks(deltaTime);

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        catch (const std::exception& e) {
            LOGS.logError("Exception caught in TaskManager: " + std::string(e.what()) + ". Retrying...");
            continue;
        }
        catch (...) {
            LOGS.logError("Unknown exception caught in TaskManager. Retrying...");
            continue;
        }
    }
}

void TaskManager::updateTasks(double deltaTime) {
    std::string heaviestTask;
    double heaviestOverdue = 0.0;

    for (auto& [name, task] : tasks) {
        task.elapsedTime += deltaTime;
        if (task.elapsedTime < *(task.interval))
            continue;

        const double overdue = task.elapsedTime - *(task.interval);
        if (overdue > heaviestOverdue) {
            heaviestOverdue = overdue;
            heaviestTask = name;
        }
    }

    for (auto& [name, task] : tasks) {
        if (task.elapsedTime < *(task.interval))
            continue;

        // Avoid stacking the two heaviest DMA tasks in one scheduler tick.
        if (!heaviestTask.empty() && name != heaviestTask &&
            IsHeavyDmaTask(name) && IsHeavyDmaTask(heaviestTask))
        {
            continue;
        }

        const auto started = std::chrono::steady_clock::now();
        task.function();
        const double durationMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

        PerfMonitor::Instance().Record("task." + name, durationMs);

        task.elapsedTime -= *(task.interval);
        if (task.elapsedTime < 0.0)
            task.elapsedTime = 0.0;
    }
}
