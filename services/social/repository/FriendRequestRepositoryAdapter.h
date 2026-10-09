#pragma once

#include "services/social/application/FriendRequestRepositoryPort.h"

namespace tinyimx {
class FriendRequestRepository;
}

namespace tinyimx::social {

class FriendRequestRepositoryAdapter final
    : public FriendRequestRepositoryPort {
public:
    explicit FriendRequestRepositoryAdapter(
        tinyimx::FriendRequestRepository* repository
    );

    [[nodiscard]] CreateFriendRequestApplicationResult Create(
        std::uint64_t from_user_id,
        std::uint64_t to_user_id,
        const std::string& request_message
    ) override;

    [[nodiscard]] ListPendingFriendRequestsApplicationResult
    ListPendingIncoming(
        std::uint64_t receiver_user_id,
        const std::string& before_created_at,
        std::uint64_t before_request_id,
        std::size_t limit
    ) override;

    [[nodiscard]] AcceptFriendRequestApplicationResult Accept(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) override;

    [[nodiscard]] RejectFriendRequestApplicationResult Reject(
        std::uint64_t request_id,
        std::uint64_t handler_user_id
    ) override;

private:
    tinyimx::FriendRequestRepository* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::social
