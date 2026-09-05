#include "services/social/service/SocialServiceImpl.h"

#include <grpcpp/grpcpp.h>

namespace tinyimx::social {
namespace {

grpc::Status MapApplicationFailure(
    FriendApplicationStatus status,
    const std::string& message
) {
    switch (status) {
        case FriendApplicationStatus::kInvalidArgument:
            return grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                message
            );

        case FriendApplicationStatus::kInvalidRecord:
            return grpc::Status(
                grpc::StatusCode::DATA_LOSS,
                message
            );

        case FriendApplicationStatus::kStorageError:
            return grpc::Status(
                grpc::StatusCode::UNAVAILABLE,
                message
            );

        case FriendApplicationStatus::kSucceeded:
            return grpc::Status::OK;
    }

    return grpc::Status(
        grpc::StatusCode::INTERNAL,
        "unknown SocialService application status"
    );
}

}  // namespace

SocialServiceImpl::SocialServiceImpl(
    FriendApplicationService* application_service
)
    : application_service_(application_service) {
}

grpc::Status SocialServiceImpl::ListFriends(
    grpc::ServerContext* context,
    const tinyimx::social::v1::ListFriendsRequest* request,
    tinyimx::social::v1::ListFriendsResponse* response
) {
    if (context == nullptr ||
        request == nullptr ||
        response == nullptr) {
        return grpc::Status(
            grpc::StatusCode::INVALID_ARGUMENT,
            "invalid ListFriends RPC arguments"
        );
    }

    if (application_service_ == nullptr) {
        return grpc::Status(
            grpc::StatusCode::UNAVAILABLE,
            "SocialService application service is unavailable"
        );
    }

    // request.meta() is deliberately observability-only in M14. Actor
    // authority is carried by actor_user_id, which Gateway must source from
    // its authenticated SessionManager rather than the Internet request body.
    auto result =
        application_service_->ListFriends(
            request->actor_user_id(),
            request->limit()
        );

    if (!result.Succeeded()) {
        return MapApplicationFailure(
            result.status,
            result.message
        );
    }

    response->set_has_more(result.has_more);

    for (const auto& friend_view : result.friends) {
        auto* proto_friend = response->add_friends();
        proto_friend->set_friend_user_id(
            friend_view.friend_user_id
        );
        proto_friend->set_username(friend_view.username);
        proto_friend->set_nickname(friend_view.nickname);
        proto_friend->set_avatar_url(friend_view.avatar_url);
        proto_friend->set_user_status(friend_view.user_status);
        proto_friend->set_relation_status(
            friend_view.relation_status
        );
        proto_friend->set_relation_created_at(
            friend_view.relation_created_at
        );
        proto_friend->set_relation_updated_at(
            friend_view.relation_updated_at
        );
    }

    return grpc::Status::OK;
}

}  // namespace tinyimx::social
