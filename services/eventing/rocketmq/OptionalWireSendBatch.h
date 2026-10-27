#pragma once

#include "patches/rocketmq-cpp-5.1.1/TinyIMXBatchSendApi.h"
#include <rocketmq/Producer.h>
#include <string_view>

namespace tinyimx::eventing::rocketmq_transport::detail {

enum class WireSendMode { Disabled, Enabled, Invalid };
inline WireSendMode ParseWireSendMode(const char* value) noexcept {
    if (!value || std::string_view(value) == "0") return WireSendMode::Disabled;
    if (std::string_view(value) == "1") return WireSendMode::Enabled;
    return WireSendMode::Invalid;
}
inline bool CompatibleWireSendApi(
    const ::ROCKETMQ_NAMESPACE::TinyIMXBatchSendApiV1* api
) noexcept {
#if defined(__linux__) && defined(_GLIBCXX_USE_CXX11_ABI)
    return api && api->magic == ::ROCKETMQ_NAMESPACE::kTinyIMXBatchSendMagic &&
           api->version == 1 && api->structure_bytes == sizeof(*api) &&
           api->gnu_cxx11_abi == _GLIBCXX_USE_CXX11_ABI &&
           api->maximum_entries == 32 && api->maximum_body_bytes == 256 * 1024 &&
           api->producer_bytes == sizeof(::ROCKETMQ_NAMESPACE::Producer) &&
           api->message_pointer_bytes == sizeof(::ROCKETMQ_NAMESPACE::MessageConstSharedPtr) &&
           api->send_batch && api->read_stats;
#else
    (void)api;
    return false;
#endif
}
}  // namespace tinyimx::eventing::rocketmq_transport::detail
