#pragma once

#include "services/social/application/FriendRequestApplicationTypes.h"
#include "services/social/application/FriendRequestRepositoryPort.h"

#include <cstdint>
#include <string>

namespace tinyimx::social {

class FriendRequestApplicationService final {
public:
    explicit FriendRequestApplicationService(
        FriendRequestRepositoryPort* repository
    );

    FriendRequestApplicationService(const FriendRequestApplicationService&) = delete;
    FriendRequestApplicationService& operator=(const FriendRequestApplicationService&) = delete;

    [[nodiscard]] CreateFriendRequestApplicationResult Create(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& request_message
    );

    [[nodiscard]] ListPendingFriendRequestsApplicationResult
    ListPendingIncoming(
        std::uint64_t receiver_user_id,
        const std::string& before_created_at,
        std::uint64_t before_request_id,
        std::uint32_t limit
    );

    [[nodiscard]] AcceptFriendRequestApplicationResult Accept(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    );

    [[nodiscard]] RejectFriendRequestApplicationResult Reject(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    );

private:
    FriendRequestRepositoryPort* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::social
