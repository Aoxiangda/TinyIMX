#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace tinyimx::eventing {

struct ConsumedEvent {
    std::string broker_message_id;
    std::string topic;
    std::string tag;
    std::vector<std::string> keys;
    std::string body;

    // Transport-owned message handle. Business consumers must never inspect it.
    std::shared_ptr<const void> native_handle;
};

struct EventReceiveResult {
    bool success{false};
    std::vector<ConsumedEvent> events;
    std::string message;
};

struct EventAckResult {
    bool success{false};
    std::string message;
};

class EventConsumer {
public:
    virtual ~EventConsumer() = default;

    EventConsumer(const EventConsumer&) = delete;
    EventConsumer& operator=(const EventConsumer&) = delete;

    [[nodiscard]] virtual EventReceiveResult Receive(
        std::size_t limit,
        int invisible_duration_ms
    ) = 0;

    [[nodiscard]] virtual EventAckResult Ack(
        const ConsumedEvent& event
    ) = 0;

protected:
    EventConsumer() = default;
};

}  // namespace tinyimx::eventing
