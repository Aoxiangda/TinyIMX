#pragma once

#include "common/logging/LogLevel.h"

#include <chrono>
#include <string>
#include <thread>

namespace tinyimx {

struct SourceLocation {
    const char* file_name{""};
    int line{0};
    const char* function_name{""};
};

struct LogMessage {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level{LogLevel::kInfo};
    SourceLocation source;
    std::thread::id thread_id;
    std::string message;
};

}  // namespace tinyimx