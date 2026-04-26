#include "app/debug.h"

Debug LOGS;

Debug::Debug() {
    start_time = std::chrono::steady_clock::now();
}

void Debug::logWarn(const std::string& message) {
    addMessage(message, MessageLevel::WARN);
}

void Debug::logError(const std::string& message) {
    addMessage(message, MessageLevel::ERR);
}

const std::vector<Message>& Debug::getMessages() const {
    return messages;
}

void Debug::addMessage(const std::string& message, MessageLevel level) {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds = now - start_time;
    messages.emplace_back(message, level, elapsed_seconds.count());
}

void Debug::clearLog() {
    messages.clear();
}