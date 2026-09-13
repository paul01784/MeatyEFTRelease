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
#include <optional>
#include <utility>

enum class MessageLevel
{
    INFO,
    NOTICE,
    WARN,
    ERR
};

enum class NoticeColour
{
    WHITE,
    RED,
    GREEN
};

struct Message
{
    std::string text;
    MessageLevel level;
    NoticeColour noticeColour;
    double timestamp;

    Message(const std::string& msg, MessageLevel lvl, double ts, NoticeColour colour = NoticeColour::WHITE)
        : text(msg), level(lvl), noticeColour(colour), timestamp(ts)
    {
    }
};

struct RadarNotice
{
    std::string text;
    NoticeColour colour;
    double ageSeconds;
};

class Debug
{
  public:
    Debug();
    ~Debug();

    Debug(const Debug&) = delete;
    Debug& operator=(const Debug&) = delete;

    template <typename... Args> void logInfo(Args&&... args)
    {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::INFO);
    }

    template <typename... Args> void logWarn(Args&&... args)
    {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::WARN);
    }

    template <typename... Args> void logNotice(Args&&... args)
    {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::NOTICE, NoticeColour::WHITE);
    }

    template <typename... Args> void logNotice(NoticeColour colour, Args&&... args)
    {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::NOTICE, colour);
    }

    template <typename... Args> void logError(Args&&... args)
    {
        addMessage(buildString(std::forward<Args>(args)...), MessageLevel::ERR);
    }

    // Thread-safe copy for console/UI rendering
    std::vector<Message> getMessages() const;
    std::optional<RadarNotice> getRadarNotice(double lifetimeSeconds);

    void clearLog();

    std::filesystem::path getErrorLogPath() const
    {
        return errorLogPath;
    }

  private:
    static constexpr size_t MaxStoredMessages = 1000;
    static constexpr size_t MaxFileQueue = 4096;
    static constexpr size_t MaxQueuedRadarNotices = 8;

    mutable std::mutex messagesMutex;
    std::vector<Message> messages;
    std::deque<std::pair<std::string, NoticeColour>> radarNoticeQueue;
    std::string radarNoticeText;
    NoticeColour radarNoticeColour = NoticeColour::WHITE;
    std::chrono::steady_clock::time_point radarNoticeStartedAt{};

    std::chrono::steady_clock::time_point start_time;

    std::atomic_bool workerRunning{false};
    std::thread fileThread;

    mutable std::mutex fileQueueMutex;
    std::condition_variable fileQueueCv;
    std::deque<std::string> fileQueue;

    std::filesystem::path logsDirectory;
    std::filesystem::path errorLogPath;
    std::string logFileStem;

    static constexpr size_t MaxLogFileBytes = 5 * 1024 * 1024; // 5 MB per file
    static constexpr size_t MaxLogFilesToKeep = 20;

    std::filesystem::path makeLogPath(int part) const;
    void cleanupOldLogFiles() noexcept;

    static std::string currentDateTimeFileString();

  private:
    template <typename... Args> static std::string buildString(Args&&... args)
    {
        static_assert(sizeof...(Args) > 0, "Logger requires at least one argument");

        std::ostringstream oss;
        (oss << ... << std::forward<Args>(args));
        return oss.str();
    }

    void addMessage(const std::string& message, MessageLevel level, NoticeColour noticeColour = NoticeColour::WHITE) noexcept;

    void enqueueFileWrite(std::string line) noexcept;
    void fileWorkerLoop() noexcept;
    void shutdownWorker() noexcept;

    std::string formatFileLine(const std::string& message, MessageLevel level, double timestamp) const;

    static const char* levelToString(MessageLevel level);
    static std::string currentDateTimeString();
};

extern Debug LOGS;
