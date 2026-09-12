#pragma once

#include "services/eventing/EventPublisher.h"

#include <memory>
#include <string>

namespace tinyimx::eventing::rocketmq_transport {

struct RocketMQProducerOptions {
    std::string endpoint{"127.0.0.1:8081"};
    std::string topic{"tinyimx-message-events"};
    int request_timeout_ms{3000};
    bool tls{false};
    std::string access_key;
    std::string access_secret;
};

class RocketMQProducer final : public EventPublisher {
public:
    explicit RocketMQProducer(RocketMQProducerOptions options);
    ~RocketMQProducer() override;

    RocketMQProducer(const RocketMQProducer&) = delete;
    RocketMQProducer& operator=(const RocketMQProducer&) = delete;

    [[nodiscard]] bool Start();
    void Stop() noexcept;
    [[nodiscard]] bool IsStarted() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

    [[nodiscard]] PublishResult Publish(
        const PublishMessage& message
    ) override;

private:
    class Impl;

private:
    RocketMQProducerOptions options_;
    std::unique_ptr<Impl> impl_;
    std::string last_error_;
};

}  // namespace tinyimx::eventing::rocketmq_transport
