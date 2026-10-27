#pragma once

#include <chrono>
#include <cstdlib>
#include <string_view>

namespace tinyimx::message {
// Diagnostic candidate only. The existing coalesce-enable gate still decides
// whether a caller uses batching at all; malformed/unset windows preserve0us.
// This is collection time per batch, not an end-to-end queue deadline.
inline std::chrono::microseconds ReceiverConfirmationCollectWindow() noexcept {
    const char* value=std::getenv("TINYIMX_PRIVATE_CONFIRM_COLLECT_US");
    const std::string_view text=value?value:"";
    if(text=="2000")return std::chrono::microseconds(2000);
    if(text=="5000")return std::chrono::microseconds(5000);
    return std::chrono::microseconds(0);
}
} // namespace tinyimx::message
