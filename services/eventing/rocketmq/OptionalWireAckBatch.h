#pragma once

#include "patches/rocketmq-cpp-5.1.1/TinyIMXBatchAckApi.h"
#include <rocketmq/SimpleConsumer.h>

#include <string_view>

namespace tinyimx::eventing::rocketmq_transport::detail {

enum class WireAckMode { Disabled, Enabled, Invalid };

inline WireAckMode ParseWireAckMode(const char* value) noexcept {
    if (!value || std::string_view(value) == "0") return WireAckMode::Disabled;
    if (std::string_view(value) == "1") return WireAckMode::Enabled;
    return WireAckMode::Invalid;
}

inline bool CompatibleWireAckApi(
    const ::ROCKETMQ_NAMESPACE::TinyIMXBatchAckApiV1* api
) noexcept {
#if defined(__linux__) && defined(_GLIBCXX_USE_CXX11_ABI)
    return api && api->magic == ::ROCKETMQ_NAMESPACE::kTinyIMXBatchAckMagic &&
           api->version == 1 && api->structure_bytes == sizeof(*api) &&
           api->gnu_cxx11_abi == _GLIBCXX_USE_CXX11_ABI &&
           api->maximum_entries == 256 &&
           api->consumer_bytes == sizeof(::ROCKETMQ_NAMESPACE::SimpleConsumer) &&
           api->message_pointer_bytes == sizeof(::ROCKETMQ_NAMESPACE::MessageConstSharedPtr) &&
           api->ack_batch && api->read_stats;
#else
    (void)api;
    return false;
#endif
}

// Synchronous, count-bounded ABI call. Never retry an ambiguous ACK through
// the legacy path: failed entries remain available for Broker redelivery.
inline std::vector<std::error_code> InvokeWireAckBatch(
    const ::ROCKETMQ_NAMESPACE::TinyIMXBatchAckApiV1* api,
    ::ROCKETMQ_NAMESPACE::SimpleConsumer* consumer,
    const std::vector<::ROCKETMQ_NAMESPACE::MessageConstSharedPtr>& handles
) {
    const auto failure = [&](std::errc code) {
        return std::vector<std::error_code>(handles.size(), std::make_error_code(code));
    };
    if (handles.size() > 256) return failure(std::errc::invalid_argument);
    if (handles.empty()) return {};
    if (!CompatibleWireAckApi(api)) return failure(std::errc::not_supported);
    std::vector<std::error_code> errors;
    try {
        if (api->ack_batch(consumer, &handles, &errors) != 0 || errors.size() != handles.size()) {
            return failure(std::errc::protocol_error);
        }
    } catch (...) {
        return failure(std::errc::io_error);
    }
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (!handles[i]) errors[i] = std::make_error_code(std::errc::invalid_argument);
    }
    return errors;
}

}  // namespace tinyimx::eventing::rocketmq_transport::detail
