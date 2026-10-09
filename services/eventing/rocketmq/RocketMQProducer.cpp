#include "services/eventing/rocketmq/RocketMQProducer.h"

#include "common/logging/LogMacros.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <rocketmq/Configuration.h>
#include <rocketmq/CredentialsProvider.h>
#include <rocketmq/Message.h>
#include <rocketmq/Producer.h>

namespace tinyimx::eventing::rocketmq_transport {
namespace {

::ROCKETMQ_NAMESPACE::CredentialsProviderPtr MakeCredentials(
    const RocketMQProducerOptions& options
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

::ROCKETMQ_NAMESPACE::Producer BuildProducer(
    const RocketMQProducerOptions& options
) {
    auto configuration = ::ROCKETMQ_NAMESPACE::Configuration::newBuilder()
        .withEndpoints(options.endpoint)
        .withCredentialsProvider(MakeCredentials(options))
        .withRequestTimeout(std::chrono::milliseconds(options.request_timeout_ms))
        .withSsl(options.tls)
        .build();

    return ::ROCKETMQ_NAMESPACE::Producer::newBuilder()
        .withConfiguration(std::move(configuration))
        .withTopics(std::vector<std::string>{options.topic})
        .build();
}

}  // namespace

class RocketMQProducer::Impl final {
public:
    explicit Impl(const RocketMQProducerOptions& options)
        : producer(BuildProducer(options)) {
    }

    ::ROCKETMQ_NAMESPACE::Producer producer;
};

RocketMQProducer::RocketMQProducer(RocketMQProducerOptions options)
    : options_(std::move(options)) {
}

RocketMQProducer::~RocketMQProducer() {
    Stop();
}

bool RocketMQProducer::Start() {
    if (impl_) {
        return true;
    }
    if (options_.endpoint.empty() || options_.topic.empty() ||
        options_.request_timeout_ms <= 0) {
        last_error_ = "RocketMQ producer configuration is invalid";
        return false;
    }

    try {
        impl_ = std::make_unique<Impl>(options_);
        last_error_.clear();
        LOG_INFO(
            "RocketMQ producer started"
            << ", endpoint=" << options_.endpoint
            << ", topic=" << options_.topic
            << ", request_timeout_ms=" << options_.request_timeout_ms
            << ", tls=" << options_.tls
        );
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("RocketMQ producer start failed: ") + e.what();
        impl_.reset();
        return false;
    } catch (...) {
        last_error_ = "RocketMQ producer start failed: unknown exception";
        impl_.reset();
        return false;
    }
}

void RocketMQProducer::Stop() noexcept {
    impl_.reset();
}

bool RocketMQProducer::IsStarted() const noexcept {
    return impl_ != nullptr;
}

const std::string& RocketMQProducer::LastError() const noexcept {
    return last_error_;
}

PublishResult RocketMQProducer::Publish(
    const PublishMessage& message
) {
    PublishResult result;
    if (!impl_) {
        result.status = PublishStatus::kRetryableFailure;
        result.message = "RocketMQ producer is not started";
        return result;
    }
    if (message.topic != options_.topic || message.event_id.empty() ||
        message.message_key.empty() || message.body.empty()) {
        result.status = PublishStatus::kPermanentFailure;
        result.message = "RocketMQ publish rejected invalid message metadata";
        return result;
    }

    try {
        auto rocket_message = ::ROCKETMQ_NAMESPACE::Message::newBuilder()
            .withTopic(message.topic)
            .withTag(message.tag)
            .withKeys(std::vector<std::string>{message.message_key})
            .withBody(message.body)
            .build();

        std::error_code ec;
        const auto receipt = impl_->producer.send(std::move(rocket_message), ec);
        if (ec) {
            // A transport error can be ambiguous: the broker may already have
            // durably accepted the message while the producer lost the ACK.
            result.status = PublishStatus::kRetryableFailure;
            result.message = "RocketMQ send failed/ambiguous: " + ec.message();
            return result;
        }

        result.status = PublishStatus::kPublished;
        result.broker_message_id = receipt.message_id;
        result.message = "RocketMQ message published";
        return result;
    } catch (const std::exception& e) {
        result.status = PublishStatus::kRetryableFailure;
        result.message = std::string("RocketMQ send threw exception: ") + e.what();
        return result;
    } catch (...) {
        result.status = PublishStatus::kRetryableFailure;
        result.message = "RocketMQ send threw unknown exception";
        return result;
    }
}

}  // namespace tinyimx::eventing::rocketmq_transport
