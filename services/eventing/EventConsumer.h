#pragma once

#include <cstddef>
#include <exception>
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

    // Concurrent Receive/Ack is required only by opt-in multi-worker consumers.
    // Start/Stop must be externally sequenced before/after all worker calls.
    [[nodiscard]] virtual EventReceiveResult Receive(
        std::size_t limit,
        int invisible_duration_ms
    ) = 0;

    [[nodiscard]] virtual EventAckResult Ack(
        const ConsumedEvent& event
    ) = 0;

    // One result per input in input order. Successful business processing must
    // precede this call. Default transport behavior remains sequential.
    [[nodiscard]] virtual std::vector<EventAckResult> AckBatch(
        const std::vector<ConsumedEvent>& events
    ) {
        std::vector<EventAckResult> results;
        results.reserve(events.size());
        for (const auto& event : events) {
            try {
                results.push_back(Ack(event));
            } catch (const std::exception& e) {
                results.push_back({false, e.what()});
            } catch (...) {
                results.push_back({false, "transport ACK threw"});
            }
        }
        return results;
    }

protected:
    EventConsumer() = default;
};

}  // namespace tinyimx::eventing
