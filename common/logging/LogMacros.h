#pragma once

#include "common/logging/Logger.h"

#include <sstream>

#define TINYIMX_LOG(level, message)                                      \
    do {                                                                 \
        std::ostringstream tinyimx_log_stream;                           \
        tinyimx_log_stream << message;                                   \
        tinyimx::Logger::Instance().Log(                                 \
            level,                                                       \
            tinyimx::SourceLocation{__FILE__, __LINE__, __func__},        \
            tinyimx_log_stream.str());                                   \
    } while (0)

#define LOG_TRACE(message) TINYIMX_LOG(tinyimx::LogLevel::kTrace, message)
#define LOG_DEBUG(message) TINYIMX_LOG(tinyimx::LogLevel::kDebug, message)
#define LOG_INFO(message)  TINYIMX_LOG(tinyimx::LogLevel::kInfo,  message)
#define LOG_WARN(message)  TINYIMX_LOG(tinyimx::LogLevel::kWarn,  message)
#define LOG_ERROR(message) TINYIMX_LOG(tinyimx::LogLevel::kError, message)
#define LOG_FATAL(message) TINYIMX_LOG(tinyimx::LogLevel::kFatal, message)