#pragma once

#include "common/logging/LogFormatter.h"
#include "common/logging/LogSink.h"

#include <memory>

namespace tinyimx {

class ConsoleSink : public LogSink {
public:
    ConsoleSink();
    ~ConsoleSink() override = default;

    bool Log(const LogMessage& message) override;
    void Flush() override;

private:
    std::unique_ptr<LogFormatter> formatter_;
};

}  // namespace tinyimx