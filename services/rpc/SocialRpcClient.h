#pragma once

#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/SocialRpcTypes.h"

#include "tinyimx/social/v1/social_service.grpc.pb.h"

#include <memory>
#include <mutex>
#include <string>

namespace grpc {
class Channel;
}

namespace tinyimx::rpc {

class SocialRpcClient {
public:
    explicit SocialRpcClient(
        std::shared_ptr<const ServiceEndpointProvider>
            endpoint_provider
    );

    SocialRpcClient(const SocialRpcClient&) = delete;
    SocialRpcClient& operator=(const SocialRpcClient&) = delete;

    [[nodiscard]] RpcResult<ListFriendsRpcResponse> ListFriends(
        const ListFriendsRpcRequest& request,
        const RpcCallOptions& options
    ) const;

private:
    using SocialStubInterface =
        tinyimx::social::v1::SocialService::StubInterface;

    [[nodiscard]] std::shared_ptr<SocialStubInterface>
    GetOrCreateStub(
        const ServiceEndpoint& endpoint
    ) const;

    [[nodiscard]] static RpcStatus MapGrpcStatus(
        const grpc::Status& status
    );

private:
    const std::shared_ptr<const ServiceEndpointProvider>
        endpoint_provider_;

    // Protects only endpoint/channel/stub cache replacement. The lock is
    // never held while a network RPC is in progress.
    mutable std::mutex cache_mutex_;
    mutable std::string cached_target_;
    mutable std::shared_ptr<grpc::Channel> cached_channel_;
    mutable std::shared_ptr<SocialStubInterface> cached_stub_;
};

}  // namespace tinyimx::rpc
