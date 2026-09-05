#pragma once

#include "services/social/application/FriendApplicationTypes.h"
#include "services/social/application/FriendRepositoryPort.h"

#include <cstdint>

namespace tinyimx::social {

class FriendApplicationService final {
public:
    explicit FriendApplicationService(
        FriendRepositoryPort* repository
    );

    FriendApplicationService(const FriendApplicationService&) = delete;
    FriendApplicationService& operator=(const FriendApplicationService&) = delete;

    [[nodiscard]] ListFriendsApplicationResult ListFriends(
        std::uint64_t actor_user_id,
        std::uint32_t limit
    );

private:
    FriendRepositoryPort* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::social
