#pragma once

#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/UserRpcTypes.h"

#include "tinyimx/user/v1/user_service.grpc.pb.h"

#include <memory>
#include <mutex>
#include <string>

namespace grpc {
class Channel;
}

namespace tinyimx::rpc {

class UserRpcClient final {
public:
    explicit UserRpcClient(
        std::shared_ptr<const ServiceEndpointProvider>
            endpoint_provider
    );

    UserRpcClient(const UserRpcClient&) = delete;
    UserRpcClient& operator=(const UserRpcClient&) = delete;

    [[nodiscard]] RpcResult<AuthenticateRpcResponse> Authenticate(
        const AuthenticateRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<GetUserProfileRpcResponse> GetUserProfile(
        const GetUserProfileRpcRequest& request,
        const RpcCallOptions& options
    ) const;

private:
    using UserStubInterface =
        tinyimx::user::v1::UserService::StubInterface;

    [[nodiscard]] std::shared_ptr<UserStubInterface>
    GetOrCreateStub(
        const ServiceEndpoint& endpoint
    ) const;

    [[nodiscard]] static RpcStatus MapGrpcStatus(
        const grpc::Status& status
    );

private:
    const std::shared_ptr<const ServiceEndpointProvider>
        endpoint_provider_;

    // Protects cache replacement only. Never held across a network RPC.
    mutable std::mutex cache_mutex_;
    mutable std::string cached_target_;
    mutable std::shared_ptr<grpc::Channel> cached_channel_;
    mutable std::shared_ptr<UserStubInterface> cached_stub_;
};

}  // namespace tinyimx::rpc
