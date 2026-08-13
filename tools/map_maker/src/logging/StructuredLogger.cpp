#include "mapmaker/logging/StructuredLogger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace pr::mapmaker {
namespace {

std::string timestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
        << std::setw(3) << std::setfill('0') << milliseconds.count() << 'Z';
    return out.str();
}

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (byte < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(byte) << std::dec;
                } else {
                    out << static_cast<char>(byte);
                }
        }
    }
    return out.str();
}

std::string encodeRecord(const LogRecord& record) {
    std::ostringstream out;
    out << "{\"timestamp\":\"" << escapeJson(record.timestamp_utc)
        << "\",\"level\":\"" << logLevelName(record.level)
        << "\",\"category\":\"" << escapeJson(record.category)
        << "\",\"message\":\"" << escapeJson(record.message) << "\"}\n";
    return out.str();
}

} // namespace

const char* logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error: return "error";
    }
    return "unknown";
}

StructuredLogger::StructuredLogger(LoggerOptions options) : options_(std::move(options)) {
    if (options_.directory.empty()) options_.directory = std::filesystem::current_path();
    const std::filesystem::path name(options_.file_name);
    if (options_.file_name.empty() || name.filename().string() != options_.file_name) {
        throw std::invalid_argument("Logger file_name must be a plain file name");
    }
    if (options_.maximum_file_bytes == 0U) options_.maximum_file_bytes = 1U;
}

StructuredLogger::~StructuredLogger() { flush(); }

bool StructuredLogger::ensureOpenLocked() {
    if (stream_.is_open()) return true;
    std::error_code error;
    std::filesystem::create_directories(options_.directory, error);
    if (error) {
        last_error_ = "Could not create log directory: " + error.message();
        return false;
    }
    const std::filesystem::path path = currentPath();
    current_bytes_ = std::filesystem::is_regular_file(path, error)
        ? static_cast<std::size_t>(std::filesystem::file_size(path, error))
        : 0U;
    if (error) current_bytes_ = 0U;
    stream_.open(path, std::ios::binary | std::ios::app);
    if (!stream_) {
        last_error_ = "Could not open log file: " + path.string();
        return false;
    }
    return true;
}

std::filesystem::path StructuredLogger::rotatedPath(std::size_t index) const {
    const std::filesystem::path name(options_.file_name);
    return options_.directory /
        (name.stem().string() + "." + std::to_string(index) + name.extension().string());
}

bool StructuredLogger::rotateLocked(std::size_t incoming_bytes) {
    if (current_bytes_ == 0U || current_bytes_ + incoming_bytes <= options_.maximum_file_bytes) {
        return true;
    }
    stream_.flush();
    stream_.close();

    std::error_code error;
    if (options_.retained_files == 0U) {
        std::filesystem::remove(currentPath(), error);
    } else {
        std::filesystem::remove(rotatedPath(options_.retained_files), error);
        error.clear();
        for (std::size_t index = options_.retained_files; index > 1U; --index) {
            const std::filesystem::path older = rotatedPath(index - 1U);
            if (!std::filesystem::exists(older, error) || error) {
                error.clear();
                continue;
            }
            std::filesystem::rename(older, rotatedPath(index), error);
            if (error) {
                last_error_ = "Could not rotate log file: " + error.message();
                return false;
            }
        }
        if (std::filesystem::exists(currentPath(), error) && !error) {
            std::filesystem::rename(currentPath(), rotatedPath(1), error);
        }
    }
    if (error) {
        last_error_ = "Could not rotate log file: " + error.message();
        return false;
    }
    current_bytes_ = 0U;
    return ensureOpenLocked();
}

bool StructuredLogger::log(LogLevel level, std::string category, std::string message) {
    LogRecord record{timestampUtc(), level, std::move(category), std::move(message)};
    const std::string encoded = encodeRecord(record);
    std::lock_guard lock(mutex_);

    if (options_.memory_records > 0U) {
        recent_.push_back(record);
        while (recent_.size() > options_.memory_records) recent_.pop_front();
    }
    if (!ensureOpenLocked() || !rotateLocked(encoded.size())) return false;
    stream_ << encoded;
    if (!stream_) {
        last_error_ = "Could not write log record: " + currentPath().string();
        return false;
    }
    current_bytes_ += encoded.size();
    last_error_.clear();
    return true;
}

void StructuredLogger::flush() {
    std::lock_guard lock(mutex_);
    if (stream_.is_open()) stream_.flush();
}

std::vector<LogRecord> StructuredLogger::recent() const {
    std::lock_guard lock(mutex_);
    return {recent_.begin(), recent_.end()};
}

std::string StructuredLogger::lastError() const {
    std::lock_guard lock(mutex_);
    return last_error_;
}

std::filesystem::path StructuredLogger::currentPath() const {
    return options_.directory / options_.file_name;
}

} // namespace pr::mapmaker
