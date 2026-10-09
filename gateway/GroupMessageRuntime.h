#pragma once
#include <cstdlib>
#include <cstring>
namespace tinyimx {
class BusinessExecutor;
inline bool GroupDeliveryMessageRuntimeEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_DELIVERY_MESSAGE_RUNTIME_ENABLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
// Both group peer validation and ACK must select the same executor. An installed
// executor that is draining is never replaced by a different ordering domain.
inline BusinessExecutor* SelectGroupDeliveryExecutor(
    BusinessExecutor* control, BusinessExecutor* message) noexcept {
    return GroupDeliveryMessageRuntimeEnabled() && message ? message : control;
}
} // namespace tinyimx
