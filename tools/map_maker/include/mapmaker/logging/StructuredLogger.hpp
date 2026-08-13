#pragma once

#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace pr::mapmaker {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

struct LogRecord {
    std::string timestamp_utc;
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
};

struct LoggerOptions {
    std::filesystem::path directory;
    std::string file_name = "map_maker.log";
    std::size_t maximum_file_bytes = 2U * 1024U * 1024U;
    std::size_t retained_files = 3;
    std::size_t memory_records = 500;
};

class StructuredLogger {
public:
    explicit StructuredLogger(LoggerOptions options);
    ~StructuredLogger();

    StructuredLogger(const StructuredLogger&) = delete;
    StructuredLogger& operator=(const StructuredLogger&) = delete;

    bool log(LogLevel level, std::string category, std::string message);
    void flush();
    std::vector<LogRecord> recent() const;
    std::string lastError() const;
    std::filesystem::path currentPath() const;

private:
    bool ensureOpenLocked();
    bool rotateLocked(std::size_t incoming_bytes);
    std::filesystem::path rotatedPath(std::size_t index) const;

    LoggerOptions options_;
    mutable std::mutex mutex_;
    std::ofstream stream_;
    std::size_t current_bytes_ = 0;
    std::deque<LogRecord> recent_;
    std::string last_error_;
};

const char* logLevelName(LogLevel level);

} // namespace pr::mapmaker
