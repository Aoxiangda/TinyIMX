#pragma once

#include "services/social/application/FriendRequestApplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyimx::social {

class FriendRequestRepositoryPort {
public:
    virtual ~FriendRequestRepositoryPort() = default;

    FriendRequestRepositoryPort() = default;
    FriendRequestRepositoryPort(const FriendRequestRepositoryPort&) = delete;
    FriendRequestRepositoryPort& operator=(const FriendRequestRepositoryPort&) = delete;

    [[nodiscard]] virtual CreateFriendRequestApplicationResult Create(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& request_message
    ) = 0;

    [[nodiscard]] virtual ListPendingFriendRequestsApplicationResult
    ListPendingIncoming(
        std::uint64_t receiver_user_id,
        const std::string& before_created_at,
        std::uint64_t before_request_id,
        std::size_t limit
    ) = 0;

    [[nodiscard]] virtual AcceptFriendRequestApplicationResult Accept(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) = 0;

    [[nodiscard]] virtual RejectFriendRequestApplicationResult Reject(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) = 0;
};

}  // namespace tinyimx::social
