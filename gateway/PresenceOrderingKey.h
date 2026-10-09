#pragma once
#include <cstdint>

namespace tinyimx {
// Use the same user domain for refresh, missing-record restore and offline
// cleanup. XOR is a bijection over uint64_t; it does not serialize all users.
inline constexpr std::uint64_t PresenceUserOrderingKey(
    std::uint64_t user_id
) noexcept {
    return user_id ^ UINT64_C(0x70726573656e6365);
}
} // namespace tinyimx
