#pragma once
#include <cstdlib>
#include <string_view>
namespace tinyimx::message {
struct MessageRpcPollerPolicy {bool enabled;int queues,min_pollers,max_pollers;};
inline MessageRpcPollerPolicy ParseMessageRpcPollerPolicy(const char* value) noexcept {
    const bool enabled=value && value[0]=='1' && value[1]=='\0';
    return {enabled,1,enabled?4:1,enabled?8:2};
}
// Experimental retention profile, off unless an exact supported value is set.
// gRPC MAX_POLLERS limits idle pollers, not active handlers or SQL concurrency.
// Keep MIN_POLLERS at1: raising both min/max equally can still create a new
// poller whenever an existing poller leaves polling to execute a request.
inline MessageRpcPollerPolicy ParseMessageRpcPollerRetention(const char* value) noexcept {
    const std::string_view text=value?value:"";
    if(text=="32")return {true,1,1,32};
    if(text=="64")return {true,1,1,64};
    return {false,1,1,2};
}
inline MessageRpcPollerPolicy ConfiguredMessageRpcPollerPolicy() noexcept {
    const auto retention=ParseMessageRpcPollerRetention(
        std::getenv("TINYIMX_MESSAGE_RPC_RETAIN_POLLERS"));
    if(retention.enabled)return retention;
    return ParseMessageRpcPollerPolicy(std::getenv("TINYIMX_MESSAGE_RPC_POLLERS_ENABLE"));
}
} // namespace tinyimx::message
