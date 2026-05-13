#include "app/taskManager.h"
#include "app/globals.h"
#include "app/debug.h"

void TaskManager::addTask(const std::string& name, std::function<void()> func, const double* interval) {
    tasks[name] = { func, interval, 0.0 };
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
    for (auto& [name, task] : tasks) {
        task.elapsedTime += deltaTime;

        if (task.elapsedTime >= *(task.interval)) { 
            task.function(); 
            task.elapsedTime -= *(task.interval);
        }
    }
}