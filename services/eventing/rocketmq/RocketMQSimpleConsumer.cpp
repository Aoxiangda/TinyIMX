#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"

#include "common/logging/LogMacros.h"
#include "services/eventing/BoundedAsyncAckBatch.h"
#include "services/eventing/rocketmq/OptionalWireAckBatch.h"

#include <chrono>
#include <cstdlib>
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

#if defined(__linux__) && defined(_GLIBCXX_USE_CXX11_ABI)
#include <dlfcn.h>
#endif

namespace tinyimx::eventing::rocketmq_transport {
namespace {

using WireAckApi = ::ROCKETMQ_NAMESPACE::TinyIMXBatchAckApiV1;

const WireAckApi* FindWireAckApi() {
#if defined(__linux__) && defined(_GLIBCXX_USE_CXX11_ABI)
    using Getter = const WireAckApi* (*)() noexcept;
    const auto getter = reinterpret_cast<Getter>(
        dlsym(RTLD_DEFAULT, "tinyimx_rocketmq_batch_ack_api_v1"));
    return getter ? getter() : nullptr;
#else
    return nullptr;
#endif
}

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
            ::ROCKETMQ_NAMESPACE::FilterExpression(options.filter_expression)
        )
        .withAwaitDuration(std::chrono::milliseconds(options.await_duration_ms))
        .build();
}

}  // namespace

class RocketMQSimpleConsumer::Impl final {
public:
    explicit Impl(const RocketMQSimpleConsumerOptions& options, const WireAckApi* api)
        : consumer(BuildConsumer(options)), wire_ack_api(api) {
    }

    ::ROCKETMQ_NAMESPACE::SimpleConsumer consumer;
    const WireAckApi* wire_ack_api;
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
        options_.consumer_group.empty() || options_.filter_expression.empty() ||
        options_.request_timeout_ms <= 0 ||
        options_.await_duration_ms <= 0) {
        last_error_ = "RocketMQ SimpleConsumer configuration is invalid";
        return false;
    }

    try {
        // Sample once before creating the SDK client. An explicit request for
        // the extension must never silently fall back to N individual RPCs.
        const auto mode = detail::ParseWireAckMode(
            std::getenv("TINYIMX_ROCKETMQ_WIRE_ACK_BATCH_ENABLE"));
        if (mode == detail::WireAckMode::Invalid) {
            last_error_ = "TINYIMX_ROCKETMQ_WIRE_ACK_BATCH_ENABLE must be 0 or 1";
            return false;
        }
        const WireAckApi* api = mode == detail::WireAckMode::Enabled ? FindWireAckApi() : nullptr;
        if (mode == detail::WireAckMode::Enabled && !detail::CompatibleWireAckApi(api)) {
            last_error_ = "RocketMQ wire ACK batch requested but compatible SDK capability is unavailable";
            return false;
        }
        impl_ = std::make_unique<Impl>(options_, api);
        last_error_.clear();
        LOG_INFO(
            "RocketMQ SimpleConsumer started"
            << ", endpoint=" << options_.endpoint
            << ", topic=" << options_.topic
            << ", group=" << options_.consumer_group
            << ", filter=" << options_.filter_expression
            << ", wire_ack_batch=" << (api != nullptr)
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

RocketMQWireAckStats RocketMQSimpleConsumer::ReadWireAckStats() const noexcept {
    RocketMQWireAckStats result;
    if (!impl_) return result;
    result.started = true;
    result.wire_batch_enabled = impl_->wire_ack_api != nullptr;
    const auto* api = FindWireAckApi();
    result.capability_available = detail::CompatibleWireAckApi(api);
    if (!result.capability_available) return result;
    try {
        ::ROCKETMQ_NAMESPACE::TinyIMXBatchAckStatsV1 snapshot{};
        api->read_stats(&snapshot);
        result.api_calls = snapshot.api_calls;
        result.rpc_submissions = snapshot.rpc_submissions;
        result.submitted_entries = snapshot.submitted_entries;
        result.rpc_terminals = snapshot.rpc_terminals;
        result.snapshot_available = true;
    } catch (...) {
        // An unavailable observation must never masquerade as measured zero.
    }
    return result;
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

std::vector<EventAckResult> RocketMQSimpleConsumer::AckBatch(
    const std::vector<ConsumedEvent>& events
) {
    if (!impl_) {
        return std::vector<EventAckResult>(
            events.size(), {false, "RocketMQ SimpleConsumer is not started"});
    }
    if (events.size() > 256) {
        return std::vector<EventAckResult>(
            events.size(), {false, "RocketMQ ACK batch exceeds 256 handles"});
    }
    using Message = ::ROCKETMQ_NAMESPACE::Message;
    std::vector<std::shared_ptr<const Message>> handles;
    handles.reserve(events.size());
    for (const auto& event : events) {
        handles.push_back(std::static_pointer_cast<const Message>(event.native_handle));
    }
    // The externally sequenced transport lifetime spans all callbacks. SDK
    // request timeouts report through the callback; no early success or
    // detached ACK worker can race Stop(). Each event retains its own receipt.
    const auto errors = impl_->wire_ack_api
        ? detail::InvokeWireAckBatch(impl_->wire_ack_api, &impl_->consumer, handles)
        : AwaitBoundedAckBatch(handles,
        [this](const Message& message, auto callback) {
            impl_->consumer.asyncAck(message, std::move(callback));
        });
    std::vector<EventAckResult> results;
    results.reserve(errors.size());
    for (const auto& error : errors) {
        results.push_back({!error, error ? "RocketMQ ACK failed: " + error.message()
                                       : "RocketMQ message acknowledged"});
    }
    return results;
}

}  // namespace tinyimx::eventing::rocketmq_transport
