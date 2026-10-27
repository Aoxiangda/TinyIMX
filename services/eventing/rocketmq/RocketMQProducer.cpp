#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/OptionalWireSendBatch.h"

#include "common/logging/LogMacros.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <dlfcn.h>
#endif

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
    Impl(const RocketMQProducerOptions& options,
         const ::ROCKETMQ_NAMESPACE::TinyIMXBatchSendApiV1* api)
        : producer(BuildProducer(options)), batch_api(api) {
    }

    ::ROCKETMQ_NAMESPACE::Producer producer;
    const ::ROCKETMQ_NAMESPACE::TinyIMXBatchSendApiV1* batch_api;
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

    const auto mode = detail::ParseWireSendMode(
        std::getenv("TINYIMX_ROCKETMQ_WIRE_SEND_BATCH_ENABLE")
    );
    if (mode == detail::WireSendMode::Invalid) {
        last_error_ = "RocketMQ wire send batch flag must be 0 or 1";
        return false;
    }
    const ::ROCKETMQ_NAMESPACE::TinyIMXBatchSendApiV1* api = nullptr;
    if (mode == detail::WireSendMode::Enabled) {
#if defined(__linux__)
        using Getter = const ::ROCKETMQ_NAMESPACE::TinyIMXBatchSendApiV1* (*)() noexcept;
        const auto getter = reinterpret_cast<Getter>(
            dlsym(RTLD_DEFAULT, "tinyimx_rocketmq_batch_send_api_v1")
        );
        if (getter) api = getter();
#endif
        if (!detail::CompatibleWireSendApi(api)) {
            last_error_ = "RocketMQ bounded batch send capability is unavailable or incompatible";
            return false;
        }
    }
    try {
        impl_ = std::make_unique<Impl>(options_, api);
        last_error_.clear();
        LOG_INFO(
            "RocketMQ producer started"
            << ", endpoint=" << options_.endpoint
            << ", topic=" << options_.topic
            << ", request_timeout_ms=" << options_.request_timeout_ms
            << ", tls=" << options_.tls
            << ", wire_send_batch=" << (api != nullptr)
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
    const auto* api = impl_ ? impl_->batch_api : nullptr;
    impl_.reset();
    if (api) {
        try {
            ::ROCKETMQ_NAMESPACE::TinyIMXBatchSendStatsV1 stats{};
            api->read_stats(&stats);
            LOG_WARN("RocketMQ bounded batch send process stopped"
                     << ", calls=" << stats.api_calls
                     << ", rpc_submissions=" << stats.rpc_submissions
                     << ", submitted_entries=" << stats.submitted_entries
                     << ", rpc_terminals=" << stats.rpc_terminals
                     << ", malformed_responses=" << stats.malformed_responses
                     << ", synchronous_timeouts=" << stats.synchronous_timeouts);
        } catch (...) {
            // Diagnostics cannot turn a normal producer shutdown into a crash.
        }
    }
}

bool RocketMQProducer::IsStarted() const noexcept {
    return impl_ != nullptr;
}

const std::string& RocketMQProducer::LastError() const noexcept {
    return last_error_;
}

std::size_t RocketMQProducer::MaxBatchMessages() const noexcept {
    return impl_ && impl_->batch_api ? 32 : 1;
}

std::vector<PublishResult> RocketMQProducer::PublishBatch(
    const std::vector<PublishMessage>& messages
) {
    if (messages.size() > 32) {
        throw std::invalid_argument("RocketMQ publication batch exceeds 32 messages");
    }
    if (!impl_ || !impl_->batch_api) {
        return EventPublisher::PublishBatch(messages);
    }
    std::vector<PublishResult> results(messages.size());
    const auto valid = [&](const PublishMessage& message) {
        return message.topic == options_.topic && !message.event_id.empty() &&
               !message.message_key.empty() && !message.body.empty();
    };
    const auto maximum_body = impl_->batch_api->maximum_body_bytes;
    std::size_t position = 0;
    while (position < messages.size()) {
        const auto start = position;
        if (!valid(messages[start])) {
            results[start].status = PublishStatus::kPermanentFailure;
            results[start].message = "RocketMQ publish rejected invalid message metadata";
            ++position;
            continue;
        }
        // A valid oversized single event keeps the original send path. This
        // decision happens before any batch RPC, never after an ambiguous send.
        if (messages[start].body.size() > maximum_body) {
            results[start] = Publish(messages[start]);
            ++position;
            continue;
        }
        std::size_t bytes = 0;
        while (position < messages.size() && valid(messages[position]) &&
               messages[position].tag == messages[start].tag &&
               messages[position].body.size() <= maximum_body - bytes) {
            bytes += messages[position].body.size();
            ++position;
        }
        // Fix this group before allocations/submission so exceptions cannot
        // skip an input or cause an infinite preparation loop.
        const auto count = position - start;
        try {
            std::vector<::ROCKETMQ_NAMESPACE::MessageConstSharedPtr> handles;
            handles.reserve(count);
            for (std::size_t i = start; i < position; ++i) {
                const auto& message = messages[i];
                auto handle = ::ROCKETMQ_NAMESPACE::Message::newBuilder()
                    .withTopic(message.topic).withTag(message.tag)
                    .withKeys(std::vector<std::string>{message.message_key})
                    .withBody(message.body).build();
                handles.emplace_back(std::move(handle));
            }
            std::vector<std::error_code> errors;
            std::vector<std::string> ids;
            const int code = impl_->batch_api->send_batch(
                &impl_->producer, &handles, &errors, &ids
            );
            if (code != 0 || errors.size() != count || ids.size() != count) {
                continue;  // Entire ambiguous group stays retryable.
            }
            bool malformed = false;
            for (std::size_t i = 0; i < count; ++i) {
                if (!errors[i] && ids[i] != handles[i]->id()) malformed = true;
            }
            if (malformed) continue;
            for (std::size_t i = 0; i < count; ++i) {
                auto& result = results[start + i];
                if (!errors[i]) {
                    result.status = PublishStatus::kPublished;
                    result.broker_message_id = std::move(ids[i]);
                    result.message = "RocketMQ batch entry published";
                } else {
                    result.message = "RocketMQ batch entry failed/ambiguous: " + errors[i].message();
                }
            }
        } catch (...) {
            // Already confirmed earlier groups remain confirmed. Every input
            // in this uncertain group is retried through its original event ID.
        }
    }
    return results;
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
