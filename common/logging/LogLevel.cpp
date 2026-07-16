#include "common/logging/LogLevel.h"

#include <algorithm>

namespace tinyimx {
namespace {

std::string ToLower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) {
                       return static_cast<char>(std::tolower(ch));
                   });
    return result;
}

}  // namespace

bool ParseLogLevel(std::string_view level_text, LogLevel* level) {
    if (level == nullptr) {
        return false;
    }

    const std::string lower_level = ToLower(level_text);

    if (lower_level == "trace") {
        *level = LogLevel::kTrace;
        return true;
    }

    if (lower_level == "debug") {
        *level = LogLevel::kDebug;
        return true;
    }

    if (lower_level == "info") {
        *level = LogLevel::kInfo;
        return true;
    }

    if (lower_level == "warn" || lower_level == "warning") {
        *level = LogLevel::kWarn;
        return true;
    }

    if (lower_level == "error") {
        *level = LogLevel::kError;
        return true;
    }

    if (lower_level == "fatal" || lower_level == "critical") {
        *level = LogLevel::kFatal;
        return true;
    }

    if (lower_level == "off") {
        *level = LogLevel::kOff;
        return true;
    }

    return false;
}

std::string LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::kTrace:
            return "TRACE";
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kFatal:
            return "FATAL";
        case LogLevel::kOff:
            return "OFF";
        default:
            return "UNKNOWN";
    }
}

}  // namespace tinyimx