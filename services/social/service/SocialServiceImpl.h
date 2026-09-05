#pragma once

#include "services/social/application/FriendApplicationService.h"
#include "tinyimx/social/v1/social_service.grpc.pb.h"

namespace tinyimx::social {

class SocialServiceImpl final
    : public tinyimx::social::v1::SocialService::Service {
public:
    explicit SocialServiceImpl(
        FriendApplicationService* application_service
    );

    grpc::Status ListFriends(
        grpc::ServerContext* context,
        const tinyimx::social::v1::ListFriendsRequest* request,
        tinyimx::social::v1::ListFriendsResponse* response
    ) override;

private:
    FriendApplicationService* application_service_{nullptr};  // non-owning
};

}  // namespace tinyimx::social
