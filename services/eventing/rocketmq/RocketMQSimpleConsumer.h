#pragma once

#include "services/eventing/EventConsumer.h"

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

    [[nodiscard]] EventReceiveResult Receive(
        std::size_t limit,
        int invisible_duration_ms
    ) override;

    [[nodiscard]] EventAckResult Ack(
        const ConsumedEvent& event
    ) override;

private:
    class Impl;

private:
    RocketMQSimpleConsumerOptions options_;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
};

}  // namespace tinyimx::eventing::rocketmq_transport
