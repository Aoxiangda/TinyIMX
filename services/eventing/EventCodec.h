#pragma once

#include "services/eventing/DomainEvent.h"

#include <string>

namespace tinyimx::eventing {

struct EventEncodeResult {
    bool success{false};
    std::string encoded;
    std::string message;
};

struct EventDecodeResult {
    bool success{false};
    DomainEvent event;
    std::string message;
};

class EventCodec final {
public:
    [[nodiscard]] static EventEncodeResult Encode(
        const DomainEvent& event
    );

    [[nodiscard]] static EventDecodeResult Decode(
        const std::string& encoded
    );
};

}  // namespace tinyimx::eventing
