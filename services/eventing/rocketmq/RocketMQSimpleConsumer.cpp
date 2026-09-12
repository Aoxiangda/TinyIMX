#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"

#include "common/logging/LogMacros.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <rocketmq/Configuration.h>
#include <rocketmq/CredentialsProvider.h>
#include <rocketmq/FilterExpression.h>
#include <rocketmq/Message.h>
#include <rocketmq/SimpleConsumer.h>

namespace tinyimx::eventing::rocketmq_transport {
namespace {

::ROCKETMQ_NAMESPACE::CredentialsProviderPtr MakeCredentials(
    const RocketMQSimpleConsumerOptions& options
) {
    if (options.access_key.empty() && options.access_secret.empty()) {
        return {};
    }
    if (options.access_key.empty() || options.access_secret.empty()) {
        throw std::invalid_argument(
            "RocketMQ access_key and access_secret must be configured together"
        );
    }
    return std::make_shared<::ROCKETMQ_NAMESPACE::StaticCredentialsProvider>(
        options.access_key,
        options.access_secret
    );
}

::ROCKETMQ_NAMESPACE::SimpleConsumer BuildConsumer(
    const RocketMQSimpleConsumerOptions& options
) {
    auto configuration = ::ROCKETMQ_NAMESPACE::Configuration::newBuilder()
        .withEndpoints(options.endpoint)
        .withCredentialsProvider(MakeCredentials(options))
        .withRequestTimeout(std::chrono::milliseconds(options.request_timeout_ms))
        .withSsl(options.tls)
        .build();

    return ::ROCKETMQ_NAMESPACE::SimpleConsumer::newBuilder()
        .withGroup(options.consumer_group)
        .withConfiguration(std::move(configuration))
        .subscribe(
            options.topic,
            ::ROCKETMQ_NAMESPACE::FilterExpression("*")
        )
        .withAwaitDuration(std::chrono::milliseconds(options.await_duration_ms))
        .build();
}

}  // namespace

class RocketMQSimpleConsumer::Impl final {
public:
    explicit Impl(const RocketMQSimpleConsumerOptions& options)
        : consumer(BuildConsumer(options)) {
    }

    ::ROCKETMQ_NAMESPACE::SimpleConsumer consumer;
};

RocketMQSimpleConsumer::RocketMQSimpleConsumer(
    RocketMQSimpleConsumerOptions options
)
    : options_(std::move(options)) {
}

RocketMQSimpleConsumer::~RocketMQSimpleConsumer() {
    Stop();
}

bool RocketMQSimpleConsumer::Start() {
    if (impl_) {
        return true;
    }
    if (options_.endpoint.empty() || options_.topic.empty() ||
        options_.consumer_group.empty() || options_.request_timeout_ms <= 0 ||
        options_.await_duration_ms <= 0) {
        last_error_ = "RocketMQ SimpleConsumer configuration is invalid";
        return false;
    }

    try {
        impl_ = std::make_unique<Impl>(options_);
        last_error_.clear();
        LOG_INFO(
            "RocketMQ SimpleConsumer started"
            << ", endpoint=" << options_.endpoint
            << ", topic=" << options_.topic
            << ", group=" << options_.consumer_group
        );
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("RocketMQ SimpleConsumer start failed: ") + e.what();
        impl_.reset();
        return false;
    } catch (...) {
        last_error_ = "RocketMQ SimpleConsumer start failed: unknown exception";
        impl_.reset();
        return false;
    }
}

void RocketMQSimpleConsumer::Stop() noexcept {
    impl_.reset();
}

bool RocketMQSimpleConsumer::IsStarted() const noexcept {
    return impl_ != nullptr;
}

const std::string& RocketMQSimpleConsumer::LastError() const noexcept {
    return last_error_;
}

EventReceiveResult RocketMQSimpleConsumer::Receive(
    std::size_t limit,
    int invisible_duration_ms
) {
    EventReceiveResult result;
    if (!impl_) {
        result.message = "RocketMQ SimpleConsumer is not started";
        return result;
    }
    if (limit == 0 || invisible_duration_ms <= 0) {
        result.message = "RocketMQ receive parameters are invalid";
        return result;
    }

    try {
        std::vector<::ROCKETMQ_NAMESPACE::MessageConstSharedPtr> messages;
        std::error_code ec;
        impl_->consumer.receive(
            limit,
            std::chrono::milliseconds(invisible_duration_ms),
            ec,
            messages
        );
        if (ec) {
            result.message = "RocketMQ receive failed: " + ec.message();
            return result;
        }

        result.events.reserve(messages.size());
        for (const auto& message : messages) {
            if (!message) {
                continue;
            }
            ConsumedEvent event;
            event.broker_message_id = message->id();
            event.topic = message->topic();
            event.tag = message->tag();
            event.keys = message->keys();
            event.body = message->body();
            event.native_handle =
                std::static_pointer_cast<const void>(message);
            result.events.push_back(std::move(event));
        }

        result.success = true;
        result.message = "RocketMQ receive completed";
        return result;
    } catch (const std::exception& e) {
        result.message = std::string("RocketMQ receive threw exception: ") + e.what();
        return result;
    } catch (...) {
        result.message = "RocketMQ receive threw unknown exception";
        return result;
    }
}

EventAckResult RocketMQSimpleConsumer::Ack(
    const ConsumedEvent& event
) {
    EventAckResult result;
    if (!impl_) {
        result.message = "RocketMQ SimpleConsumer is not started";
        return result;
    }
    if (!event.native_handle) {
        result.message = "RocketMQ ACK rejected missing native handle";
        return result;
    }

    try {
        const auto message =
            std::static_pointer_cast<const ::ROCKETMQ_NAMESPACE::Message>(
                event.native_handle
            );
        if (!message) {
            result.message = "RocketMQ ACK rejected invalid native handle";
            return result;
        }

        std::error_code ec;
        impl_->consumer.ack(*message, ec);
        if (ec) {
            result.message = "RocketMQ ACK failed: " + ec.message();
            return result;
        }

        result.success = true;
        result.message = "RocketMQ message acknowledged";
        return result;
    } catch (const std::exception& e) {
        result.message = std::string("RocketMQ ACK threw exception: ") + e.what();
        return result;
    } catch (...) {
        result.message = "RocketMQ ACK threw unknown exception";
        return result;
    }
}

}  // namespace tinyimx::eventing::rocketmq_transport
