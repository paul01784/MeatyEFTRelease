#pragma once
#include "includes.h"
#include <string>
#include <vector>
#include <chrono>

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

    void logWarn(const std::string& message);
    void logError(const std::string& message);

    template<typename T>
    void logInfo(const T& msg) {
        std::ostringstream oss;
        oss << msg;
        addMessage(oss.str(), MessageLevel::INFO);
    }

    template<typename... Args>
    void logInfo(const Args&... args) {
        std::ostringstream oss;
        (oss << ... << args); // fold expression
        addMessage(oss.str(), MessageLevel::INFO);
    }

    const std::vector<Message>& getMessages() const;

    void clearLog();

private:
    std::vector<Message> messages;
    void addMessage(const std::string& message, MessageLevel level);
    std::chrono::steady_clock::time_point start_time;
};

extern Debug LOGS;