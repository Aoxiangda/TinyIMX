#pragma once

#include "services/social/application/FriendApplicationTypes.h"

#include <cstddef>
#include <cstdint>

namespace tinyimx::social {

class FriendRepositoryPort {
public:
    virtual ~FriendRepositoryPort() = default;

    FriendRepositoryPort() = default;
    FriendRepositoryPort(const FriendRepositoryPort&) = delete;
    FriendRepositoryPort& operator=(const FriendRepositoryPort&) = delete;

    [[nodiscard]] virtual FriendRepositoryListResult ListFriends(
        std::uint64_t user_id,
        std::size_t limit
    ) = 0;
};

}  // namespace tinyimx::social
