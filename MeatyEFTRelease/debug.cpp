#include "app/debug.h"

Debug LOGS;

Debug::Debug() {
    start_time = std::chrono::steady_clock::now();

    try {
        logsDirectory = std::filesystem::current_path() / "logs";
        std::filesystem::create_directories(logsDirectory);

        logFileStem = "errors_" + currentDateTimeFileString();
        errorLogPath = makeLogPath(0);

        cleanupOldLogFiles();
    }
    catch (...) {
        logsDirectory = ".";
        logFileStem = "errors_" + currentDateTimeFileString();
        errorLogPath = makeLogPath(0);
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

std::string Debug::currentDateTimeFileString() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto nowTimeT = system_clock::to_time_t(now);

    std::tm tm{};

    localtime_s(&tm, &nowTimeT);

    std::ostringstream oss;

    oss << std::put_time(&tm, "%d-%m-%Y_%H-%M-%S");

    return oss.str();
}

std::string Debug::currentDateTimeString() {
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto nowTimeT = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};

    localtime_s(&tm, &nowTimeT);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%d-%m-%Y %H:%M:%S")
        << '.'
        << std::setfill('0')
        << std::setw(3)
        << ms.count();

    return oss.str();
}

std::filesystem::path Debug::makeLogPath(int part) const {
    if (part <= 0) {
        return logsDirectory / (logFileStem + ".log");
    }

    std::ostringstream oss;
    oss << logFileStem << "_part" << std::setw(2) << std::setfill('0') << part << ".log";

    return logsDirectory / oss.str();
}

void Debug::cleanupOldLogFiles() noexcept {
    try {
        if (logsDirectory.empty() || !std::filesystem::exists(logsDirectory)) {
            return;
        }

        struct LogFileInfo {
            std::filesystem::path path;
            std::filesystem::file_time_type writeTime;
        };

        std::vector<LogFileInfo> logFiles;

        for (const auto& entry : std::filesystem::directory_iterator(logsDirectory)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto path = entry.path();
            const auto filename = path.filename().string();

            if (filename.rfind("errors_", 0) != 0) {
                continue;
            }

            if (path.extension() != ".log") {
                continue;
            }

            logFiles.push_back({
                path,
                std::filesystem::last_write_time(path)
                });
        }

        std::sort(logFiles.begin(), logFiles.end(),
            [](const LogFileInfo& a, const LogFileInfo& b) {
                return a.writeTime > b.writeTime;
            });

        if (logFiles.size() <= MaxLogFilesToKeep) {
            return;
        }

        for (size_t i = MaxLogFilesToKeep; i < logFiles.size(); ++i) {
            std::error_code ec;
            std::filesystem::remove(logFiles[i].path, ec);
        }
    }
    catch (...) {

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
        std::deque<std::string> localQueue;

        int logPart = 0;
        size_t currentFileBytes = 0;

        auto refreshFileSize = [&]() {
            currentFileBytes = 0;

            try {
                if (std::filesystem::exists(errorLogPath)) {
                    currentFileBytes = static_cast<size_t>(std::filesystem::file_size(errorLogPath));
                }
            }
            catch (...) {
                currentFileBytes = 0;
            }
            };

        auto openFile = [&]() -> bool {
            if (file.is_open()) {
                return true;
            }

            try {
                if (!logsDirectory.empty()) {
                    std::filesystem::create_directories(logsDirectory);
                }

                file.open(errorLogPath, std::ios::out | std::ios::app);

                if (file.is_open()) {
                    refreshFileSize();
                }
            }
            catch (...) {
                return false;
            }

            return file.is_open();
            };

        auto rotateFile = [&]() -> bool {
            try {
                if (file.is_open()) {
                    file.flush();
                    file.close();
                }

                ++logPart;
                errorLogPath = makeLogPath(logPart);
                currentFileBytes = 0;

                return openFile();
            }
            catch (...) {
                return false;
            }
            };

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
                    const size_t lineBytes = line.size() + 1;

                    if (currentFileBytes > 0 &&
                        currentFileBytes + lineBytes > MaxLogFileBytes) {
                        if (!rotateFile()) {
                            break;
                        }
                    }

                    file << line << '\n';
                    currentFileBytes += lineBytes;
                }

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