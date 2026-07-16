#pragma once

#include "common/config/ConfigTypes.h"
#include "common/logging/LogLevel.h"
#include "common/logging/LogMessage.h"
#include "common/logging/LogSink.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tinyimx {

class Logger {
public:
    static Logger& Instance();

    bool Init(const LoggerConfig& config);
    bool Init(const std::string& level,
              const std::string& file_path,
              bool enable_console);

    void Shutdown();
    void Flush();

    void Log(LogLevel level,
             SourceLocation source,
             const std::string& message);
    void Log(LogLevel level,
             const char* file,
             int line,
             const std::string& message);

    bool IsInitialized() const;
    LogLevel MinLevel() const;

    std::uint64_t WrittenLogCount() const;
    std::uint64_t FilteredLogCount() const;
    std::uint64_t FailedLogCount() const;

private:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool ShouldLog(LogLevel level) const;

private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<LogSink>> sinks_;

    //LogLevel min_level_{LogLevel::kInfo};
    std::atomic<int> min_level_value_{static_cast<int>(LogLevel::kInfo)};
    bool initialized_{false};

    std::atomic<std::uint64_t> written_log_count_{0};
    std::atomic<std::uint64_t> filtered_log_count_{0};
    std::atomic<std::uint64_t> failed_log_count_{0};
};

}  // namespace tinyimx