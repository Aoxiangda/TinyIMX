#pragma once

#include "services/social/application/FriendApplicationService.h"
#include "services/social/application/FriendRequestApplicationService.h"
#include "tinyimx/social/v1/social_service.grpc.pb.h"

namespace tinyimx::social {

class SocialServiceImpl final
    : public tinyimx::social::v1::SocialService::Service {
public:
    SocialServiceImpl(
        FriendApplicationService* friend_application_service,
        FriendRequestApplicationService* friend_request_application_service = nullptr
    );

    grpc::Status ListFriends(
        grpc::ServerContext* context,
        const tinyimx::social::v1::ListFriendsRequest* request,
        tinyimx::social::v1::ListFriendsResponse* response
    ) override;

    grpc::Status CheckPrivateChatPermission(
        grpc::ServerContext* context,
        const tinyimx::social::v1::CheckPrivateChatPermissionRequest* request,
        tinyimx::social::v1::CheckPrivateChatPermissionResponse* response
    ) override;

    grpc::Status CreateFriendRequest(
        grpc::ServerContext* context,
        const tinyimx::social::v1::CreateFriendRequestRequest* request,
        tinyimx::social::v1::CreateFriendRequestResponse* response
    ) override;

    grpc::Status ListPendingIncomingFriendRequests(
        grpc::ServerContext* context,
        const tinyimx::social::v1::ListPendingIncomingFriendRequestsRequest* request,
        tinyimx::social::v1::ListPendingIncomingFriendRequestsResponse* response
    ) override;

    grpc::Status AcceptFriendRequest(
        grpc::ServerContext* context,
        const tinyimx::social::v1::AcceptFriendRequestRequest* request,
        tinyimx::social::v1::AcceptFriendRequestResponse* response
    ) override;

    grpc::Status RejectFriendRequest(
        grpc::ServerContext* context,
        const tinyimx::social::v1::RejectFriendRequestRequest* request,
        tinyimx::social::v1::RejectFriendRequestResponse* response
    ) override;

private:
    FriendApplicationService* friend_application_service_{nullptr};  // non-owning
    FriendRequestApplicationService* friend_request_application_service_{nullptr};  // non-owning
};

}  // namespace tinyimx::social
