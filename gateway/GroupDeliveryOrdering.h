#pragma once
#include "gateway/DeliveryIdentity.h"
#include "gateway/business/BusinessRuntimeTypes.h"
#include <cstdlib>
#include <cstring>
namespace tinyimx {
inline bool GroupDeliveryRecipientOrderingEnabled() noexcept {
    static const bool enabled = [] {
        const char* value = std::getenv("TINYIMX_GROUP_DELIVERY_RECIPIENT_ORDER_ENABLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}
// Peer validation and receiver ACK use the identical delivery identity.
// The ACK recipient is the authenticated session user, never packet-supplied.
inline BusinessOrderingKey GroupDeliveryOrderingKey(
    std::uint64_t message_id, std::uint64_t recipient_user_id) noexcept {
    if (!GroupDeliveryRecipientOrderingEnabled()) return message_id;
    return static_cast<BusinessOrderingKey>(
        DeliveryIdentityHash{}(GroupDeliveryIdentity(message_id, recipient_user_id)));
}
} // namespace tinyimx
