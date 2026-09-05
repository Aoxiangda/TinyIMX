#pragma once

#include "services/social/application/FriendRepositoryPort.h"

namespace tinyimx {
class FriendRepository;
}

namespace tinyimx::social {

class FriendRepositoryAdapter final
    : public FriendRepositoryPort {
public:
    explicit FriendRepositoryAdapter(
        tinyimx::FriendRepository* repository
    );

    [[nodiscard]] FriendRepositoryListResult ListFriends(
        std::uint64_t user_id,
        std::size_t limit
    ) override;

private:
    tinyimx::FriendRepository* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::social
