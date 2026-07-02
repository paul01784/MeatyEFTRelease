#pragma once
#include <iostream>
#include <chrono>
#include <thread>
#include <functional>
#include <unordered_map>

struct TimedTask {
    std::function<void()> function;
    const double* interval;       
    double elapsedTime = 0.0;
    double phaseOffsetMs = 0.0;
};

class TaskManager {
public:
    void addTask(const std::string& name, std::function<void()> func, const double* interval, double phaseOffsetMs = 0.0);
    void removeTask(const std::string& name);
    void run();

private:
    std::unordered_map<std::string, TimedTask> tasks;
    std::chrono::high_resolution_clock::time_point previousTime;
    void updateTasks(double deltaTime);
};