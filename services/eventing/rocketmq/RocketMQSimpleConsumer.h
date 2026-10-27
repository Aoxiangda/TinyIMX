#pragma once

#include "services/eventing/EventConsumer.h"

#include <cstdint>
#include <memory>
#include <string>

namespace tinyimx::eventing::rocketmq_transport {

struct RocketMQSimpleConsumerOptions {
    std::string endpoint{"127.0.0.1:8081"};
    std::string topic{"tinyimx-message-events"};
    std::string consumer_group{"tinyimx-unread-projector-v1"};
    // TAG filter expression. "*" preserves the historical all-tags behavior.
    // Consumers sharing the same consumer_group must use the same expression.
    std::string filter_expression{"*"};
    int request_timeout_ms{3000};
    int await_duration_ms{5000};
    bool tls{false};
    std::string access_key;
    std::string access_secret;
};

// The pinned SDK counts only calls through its optional batch API, across the
// entire process. These are not totals for legacy ACKs or Broker requests.
struct RocketMQWireAckStats {
    bool started{false};
    bool wire_batch_enabled{false};
    bool capability_available{false};
    bool snapshot_available{false};
    std::uint64_t api_calls{0};
    std::uint64_t rpc_submissions{0};
    std::uint64_t submitted_entries{0};
    std::uint64_t rpc_terminals{0};
};

class RocketMQSimpleConsumer final : public EventConsumer {
public:
    explicit RocketMQSimpleConsumer(RocketMQSimpleConsumerOptions options);
    ~RocketMQSimpleConsumer() override;

    RocketMQSimpleConsumer(const RocketMQSimpleConsumer&) = delete;
    RocketMQSimpleConsumer& operator=(const RocketMQSimpleConsumer&) = delete;

    [[nodiscard]] bool Start();
    void Stop() noexcept;
    [[nodiscard]] bool IsStarted() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    // Like Receive/Ack, externally sequence this with Start/Stop. For a final
    // quiescent snapshot, join all users before reading, then call Stop().
    [[nodiscard]] RocketMQWireAckStats ReadWireAckStats() const noexcept;

    [[nodiscard]] EventReceiveResult Receive(
        std::size_t limit,
        int invisible_duration_ms
    ) override;

    [[nodiscard]] EventAckResult Ack(
        const ConsumedEvent& event
    ) override;

    [[nodiscard]] std::vector<EventAckResult> AckBatch(
        const std::vector<ConsumedEvent>& events
    ) override;

private:
    class Impl;

private:
    RocketMQSimpleConsumerOptions options_;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
};

}  // namespace tinyimx::eventing::rocketmq_transport
