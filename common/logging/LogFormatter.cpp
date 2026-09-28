#include "common/logging/LogFormatter.h"

#include "common/observability/Trace.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace tinyimx {

std::string DefaultLogFormatter::Format(const LogMessage& message) {
    const auto time_value =
        std::chrono::system_clock::to_time_t(message.timestamp);

    std::tm tm_time{};
    localtime_r(&time_value, &tm_time);

    const auto trace_ids = observability::CurrentTraceIds();

    std::ostringstream stream;
    stream << '[' << std::put_time(&tm_time, "%Y-%m-%d %H:%M:%S") << ']'
           << " [" << LogLevelToString(message.level) << ']'
           << " [thread=" << message.thread_id << ']';

    if (trace_ids.Valid()) {
        stream << " [trace_id=" << trace_ids.trace_id
               << " span_id=" << trace_ids.span_id << ']';
    }

    stream
           << " [" << BaseFileName(message.source.file_name)
           << ':' << message.source.line
           << " " << message.source.function_name << ']'
           << ' ' << message.message;

    return stream.str();
}

std::string DefaultLogFormatter::BaseFileName(const char* file_path) {
    if (file_path == nullptr) {
        return "";
    }

    std::string path(file_path);

    std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }

    return path.substr(pos + 1);
}

}  // namespace tinyimx