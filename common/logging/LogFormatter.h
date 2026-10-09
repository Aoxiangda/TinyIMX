#pragma once

#include "common/logging/LogMessage.h"

#include <string>

namespace tinyimx {

class LogFormatter {
public:
    virtual ~LogFormatter() = default;

    virtual std::string Format(const LogMessage& message) = 0;
};

class DefaultLogFormatter : public LogFormatter {
public:
    std::string Format(const LogMessage& message) override;

private:
    static std::string BaseFileName(const char* file_path);
};

}  // namespace tinyimx