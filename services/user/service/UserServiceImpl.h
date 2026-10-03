#pragma once

#include "services/user/application/UserApplicationService.h"
#include "common/concurrency/ThreadPool.h"
#include "tinyimx/user/v1/user_service.grpc.pb.h"

namespace tinyimx::user {

class UserServiceImpl final
    : public tinyimx::user::v1::UserService::Service {
public:
    explicit UserServiceImpl(
        UserApplicationService* application_service,
        tinyimx::ThreadPool* metadata_executor = nullptr
    );

    grpc::Status Authenticate(
        grpc::ServerContext* context,
        const tinyimx::user::v1::AuthenticateRequest* request,
        tinyimx::user::v1::AuthenticateResponse* response
    ) override;

    grpc::Status GetUserProfile(
        grpc::ServerContext* context,
        const tinyimx::user::v1::GetUserProfileRequest* request,
        tinyimx::user::v1::GetUserProfileResponse* response
    ) override;

private:
    UserApplicationService* application_service_{nullptr};  // non-owning
    tinyimx::ThreadPool* metadata_executor_{nullptr};        // non-owning
};

}  // namespace tinyimx::user
