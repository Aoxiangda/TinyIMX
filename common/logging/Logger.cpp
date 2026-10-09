#include "common/logging/Logger.h"

#include "common/logging/ConsoleSink.h"
#include "common/logging/FileSink.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <cstdint>


namespace tinyimx {

Logger& Logger::Instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    Shutdown();
}

bool Logger::Init(const LoggerConfig& config) {
    LogLevel parsed_level = LogLevel::kInfo;
    if (!ParseLogLevel(config.level, &parsed_level)) {
        std::cerr << "invalid logger level: " << config.level << std::endl;
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    sinks_.clear();

    min_level_value_.store(static_cast<int>(parsed_level),
                           std::memory_order_relaxed
                        );

    if (config.async) {
        std::cerr << "logger async mode is not implemented yet, "
                      << "ignoring async setting" << std::endl;
    }

    if (config.console) {
        sinks_.push_back(std::make_unique<ConsoleSink>());
    }

    if (!config.file.empty()) {
        const std::uintmax_t max_file_size_bytes =
            static_cast<std::uintmax_t>(config.max_file_size_mb) * 1024 * 1024;

        auto file_sink = std::make_unique<FileSink>(
            config.file,
            max_file_size_bytes,
            config.max_backup_files,
            config.flush_each_log
        );

        if (!file_sink->Open()) {
            std::cerr << "failed to open log file: "
                      << config.file << std::endl;
            sinks_.clear();
            initialized_ = false;
            return false;
        }

        sinks_.push_back(std::move(file_sink));
    }

    if (sinks_.empty()) {
        std::cerr << "logger must have at least one sink" << std::endl;
        initialized_ = false;
        return false;
    }

    written_log_count_.store(0, std::memory_order_relaxed);
    filtered_log_count_.store(0, std::memory_order_relaxed);
    failed_log_count_.store(0, std::memory_order_relaxed);

    initialized_ = true;
    return true;
}

bool Logger::Init(const std::string& level,
                  const std::string& file_path,
                  bool enable_console) {
    LoggerConfig config;
    config.level = level;
    config.file = file_path;
    config.console = enable_console;
    return Init(config);
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ && sinks_.empty()) {
        return;
    }

    for (auto& sink : sinks_) {
        if (sink) {
            sink->Flush();
        }
    }

    sinks_.clear();
    initialized_ = false;
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& sink : sinks_) {
        if (sink) {
            sink->Flush();
        }
    }
}

void Logger::Log(LogLevel level,
                 SourceLocation source,
                 const std::string& message) {
    if (!ShouldLog(level)) {
        filtered_log_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    LogMessage log_message;
    log_message.timestamp = std::chrono::system_clock::now();
    log_message.level = level;
    log_message.source = source;
    log_message.thread_id = std::this_thread::get_id();
    log_message.message = message;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        failed_log_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    bool success = true;
    for (auto& sink : sinks_) {
        if (sink && !sink->Log(log_message)) {
            success = false;
        }
    }

    if (level == LogLevel::kFatal) {
        for (auto& sink : sinks_) {
            if (sink) {
                sink->Flush();
            }
        }
    }

    if (success) {
        written_log_count_.fetch_add(1, std::memory_order_relaxed);
    } else {
        failed_log_count_.fetch_add(1, std::memory_order_relaxed);
    }
}

void Logger::Log(LogLevel level,
                 const char* file,
                 int line,
                 const std::string& message) {
    Log(level, SourceLocation{file, line, ""}, message);
}

bool Logger::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

LogLevel Logger::MinLevel() const {
    return static_cast<LogLevel>(
        min_level_value_.load(std::memory_order_relaxed)
    );
}

std::uint64_t Logger::WrittenLogCount() const {
    return written_log_count_.load(std::memory_order_relaxed);
}

std::uint64_t Logger::FilteredLogCount() const {
    return filtered_log_count_.load(std::memory_order_relaxed);
}

std::uint64_t Logger::FailedLogCount() const {
    return failed_log_count_.load(std::memory_order_relaxed);
}

bool Logger::ShouldLog(LogLevel level) const {
    if (level == LogLevel::kOff) {
        return false;
    }

    const int current_level = static_cast<int>(level);

    const int min_level = min_level_value_.load(std::memory_order_relaxed);

    return current_level >= min_level;
}

}  // namespace tinyimx