#pragma once
#include "includes.h"

#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <utility>

enum class MessageLevel {
    INFO,
    WARN,
    ERR
};

struct Message {
    std::string text;
    MessageLevel level;
    double timestamp;

    Message(const std::string& msg, MessageLevel lvl, double ts)
        : text(msg), level(lvl), timestamp(ts) {
    }
};

class Debug {
public:
    Debug();
    ~Debug();

    Debug(const Debug&) = delete;
    Debug& operator=(const Debug&) = delete;

    template<typename... Args>
    void logInfo(Args&&... args) {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::INFO);
    }

    template<typename... Args>
    void logWarn(Args&&... args) {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::WARN);
    }

    template<typename... Args>
    void logError(Args&&... args) {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::ERR);
    }

    // Thread-safe copy for console/UI rendering
    std::vector<Message> getMessages() const;

    void clearLog();

    std::filesystem::path getErrorLogPath() const {
        return errorLogPath;
    }

private:
    static constexpr size_t MaxStoredMessages = 1000;
    static constexpr size_t MaxFileQueue = 4096;

    mutable std::mutex messagesMutex;
    std::vector<Message> messages;

    std::chrono::steady_clock::time_point start_time;

    std::atomic_bool workerRunning{ false };
    std::thread fileThread;

    mutable std::mutex fileQueueMutex;
    std::condition_variable fileQueueCv;
    std::deque<std::string> fileQueue;

    std::filesystem::path errorLogPath;

private:
    template<typename... Args>
    static std::string buildString(Args&&... args) {
        static_assert(sizeof...(Args) > 0, "Logger requires at least one argument");

        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return oss.str();
    }

    void addMessage(const std::string& message, MessageLevel level) noexcept;

    void enqueueFileWrite(std::string line) noexcept;
    void fileWorkerLoop() noexcept;
    void shutdownWorker() noexcept;

    std::string formatFileLine(
        const std::string& message,
        MessageLevel level,
        double timestamp
    ) const;

    static const char* levelToString(MessageLevel level);
    static std::string currentDateTimeString();
};

extern Debug LOGS;