#pragma once

#include <string>
#include <string_view>

namespace tinyimx {

enum class LogLevel {
    kTrace = 0,
    kDebug,
    kInfo,
    kWarn,
    kError,
    kFatal,
    kOff,
    DEBUG = kDebug,
    INFO = kInfo,
    WARN = kWarn,
    ERROR = kError
};

bool ParseLogLevel(std::string_view level_text, LogLevel* level);
std::string LogLevelToString(LogLevel level);

}  // namespace tinyimx