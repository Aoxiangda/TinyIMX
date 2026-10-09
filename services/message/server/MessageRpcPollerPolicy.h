#pragma once
#include <cstdlib>
namespace tinyimx::message {
struct MessageRpcPollerPolicy {bool enabled;int queues,min_pollers,max_pollers;};
inline MessageRpcPollerPolicy ParseMessageRpcPollerPolicy(const char* value) noexcept {
    const bool enabled=value && value[0]=='1' && value[1]=='\0';
    return {enabled,1,enabled?4:1,enabled?8:2};
}
inline MessageRpcPollerPolicy ConfiguredMessageRpcPollerPolicy() noexcept {
    return ParseMessageRpcPollerPolicy(std::getenv("TINYIMX_MESSAGE_RPC_POLLERS_ENABLE"));
}
} // namespace tinyimx::message
