#pragma once

#include "common/logging/LogMessage.h"

namespace tinyimx {

class LogSink {
public:
    virtual ~LogSink() = default;

    virtual bool Log(const LogMessage& message) = 0;
    virtual void Flush() = 0;
};

}  // namespace tinyimx