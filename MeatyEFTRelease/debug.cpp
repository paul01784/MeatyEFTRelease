#include "app/debug.h"

Debug LOGS;

Debug::Debug() {
    start_time = std::chrono::steady_clock::now();

    try {
        errorLogPath = std::filesystem::current_path() / "logs" / "errors.log";
    }
    catch (...) {
        errorLogPath = "errors.log";
    }

    workerRunning.store(true, std::memory_order_release);
    fileThread = std::thread(&Debug::fileWorkerLoop, this);
}

Debug::~Debug() {
    shutdownWorker();
}

void Debug::shutdownWorker() noexcept {
    workerRunning.store(false, std::memory_order_release);
    fileQueueCv.notify_all();

    if (fileThread.joinable()) {
        fileThread.join();
    }
}

std::vector<Message> Debug::getMessages() const {
    std::lock_guard<std::mutex> lock(messagesMutex);
    return messages;
}

void Debug::clearLog() {
    std::lock_guard<std::mutex> lock(messagesMutex);
    messages.clear();
}

void Debug::addMessage(const std::string& message, MessageLevel level) noexcept {
    try {
        const auto now = std::chrono::steady_clock::now();
        const std::chrono::duration<double> elapsed_seconds = now - start_time;
        const double timestamp = elapsed_seconds.count();

        {
            std::lock_guard<std::mutex> lock(messagesMutex);

            messages.emplace_back(message, level, timestamp);

            if (messages.size() > MaxStoredMessages) {
                const size_t extra = messages.size() - MaxStoredMessages;
                messages.erase(messages.begin(), messages.begin() + extra);
            }
        }

        enqueueFileWrite(formatFileLine(message, level, timestamp));

    }
    catch (...) {
        
    }
}

void Debug::enqueueFileWrite(std::string line) noexcept {
    try {
        {
            std::lock_guard<std::mutex> lock(fileQueueMutex);

            
            if (fileQueue.size() >= MaxFileQueue) {
                fileQueue.pop_front();
            }

            fileQueue.emplace_back(std::move(line));
        }

        fileQueueCv.notify_one();
    }
    catch (...) {
        
    }
}

void Debug::fileWorkerLoop() noexcept {
    try {
        std::ofstream file;

        auto openFile = [&]() -> bool {
            if (file.is_open()) {
                return true;
            }

            try {
                const auto parent = errorLogPath.parent_path();

                if (!parent.empty()) {
                    std::filesystem::create_directories(parent);
                }

                file.open(errorLogPath, std::ios::out | std::ios::app);
            }
            catch (...) {
                return false;
            }

            return file.is_open();
            };

        std::deque<std::string> localQueue;

        while (true) {
            {
                std::unique_lock<std::mutex> lock(fileQueueMutex);

                fileQueueCv.wait(lock, [&]() {
                    return !fileQueue.empty() ||
                        !workerRunning.load(std::memory_order_acquire);
                    });

                if (!workerRunning.load(std::memory_order_acquire) && fileQueue.empty()) {
                    break;
                }

                localQueue.swap(fileQueue);
            }

            if (localQueue.empty()) {
                continue;
            }

            if (openFile()) {
                for (const auto& line : localQueue) {
                    file << line << '\n';
                }

                // Flush per batch so crashes lose less data.
                file.flush();
            }

            localQueue.clear();
        }

        if (file.is_open()) {
            file.flush();
            file.close();
        }
    }
    catch (...) {
       
    }
}

std::string Debug::formatFileLine(
    const std::string& message,
    MessageLevel level,
    double timestamp
) const {
    std::ostringstream oss;

    oss << '[' << currentDateTimeString() << "] "
        << '[' << levelToString(level) << "] "
        << "[+" << std::fixed << std::setprecision(3) << timestamp << "s] "
        << message;

    return oss.str();
}

const char* Debug::levelToString(MessageLevel level) {
    switch (level) {
    case MessageLevel::INFO:
        return "INFO";
    case MessageLevel::WARN:
        return "WARN";
    case MessageLevel::ERR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

std::string Debug::currentDateTimeString() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto nowTimeT = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};

#ifdef _WIN32
    localtime_s(&tm, &nowTimeT);
#else
    localtime_r(&nowTimeT, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.'
        << std::setfill('0')
        << std::setw(3)
        << ms.count();

    return oss.str();
}