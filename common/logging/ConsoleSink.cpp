#include "common/logging/ConsoleSink.h"

#include <iostream>

namespace tinyimx {

ConsoleSink::ConsoleSink()
    : formatter_(std::make_unique<DefaultLogFormatter>()) {}

bool ConsoleSink::Log(const LogMessage& message) {
    const std::string formatted_message = formatter_->Format(message);

    if (static_cast<int>(message.level) >= static_cast<int>(LogLevel::kError)) {
        std::cerr << formatted_message << std::endl;
    } else {
        std::cout << formatted_message << std::endl;
    }

    return true;
}

void ConsoleSink::Flush() {
    std::cout.flush();
    std::cerr.flush();
}

}  // namespace tinyimx