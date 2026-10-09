#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tinyimx {

enum class DeliveryDomain : std::uint8_t {
    kPrivateMessage = 1,
    kGroupMessage = 2,
};

struct DeliveryIdentity {
    DeliveryDomain domain{DeliveryDomain::kPrivateMessage};
    std::uint64_t message_id{0};
    std::uint64_t recipient_user_id{0};

    [[nodiscard]] bool Valid() const noexcept {
        return message_id != 0 && recipient_user_id != 0;
    }

    friend bool operator==(const DeliveryIdentity&, const DeliveryIdentity&) = default;
};

struct DeliveryIdentityHash {
    std::size_t operator()(const DeliveryIdentity& value) const noexcept {
        const auto a = static_cast<std::size_t>(value.domain);
        const auto b = std::hash<std::uint64_t>{}(value.message_id);
        const auto c = std::hash<std::uint64_t>{}(value.recipient_user_id);
        return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6U) + (a >> 2U)) ^
               (c + 0x9e3779b97f4a7c15ULL + (b << 6U) + (b >> 2U));
    }
};

[[nodiscard]] inline DeliveryIdentity PrivateDeliveryIdentity(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id
) noexcept {
    return {DeliveryDomain::kPrivateMessage, message_id, recipient_user_id};
}

[[nodiscard]] inline DeliveryIdentity GroupDeliveryIdentity(
    std::uint64_t message_id,
    std::uint64_t recipient_user_id
) noexcept {
    return {DeliveryDomain::kGroupMessage, message_id, recipient_user_id};
}

}  // namespace tinyimx
