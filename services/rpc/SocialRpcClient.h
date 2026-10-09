#pragma once

#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/SocialRpcTypes.h"

#include "tinyimx/social/v1/social_service.grpc.pb.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace grpc {
class Channel;
}

namespace tinyimx::rpc {

class SocialRpcClient {
public:
    explicit SocialRpcClient(
        std::shared_ptr<const ServiceEndpointProvider> endpoint_provider
    );

    SocialRpcClient(const SocialRpcClient&) = delete;
    SocialRpcClient& operator=(const SocialRpcClient&) = delete;

    [[nodiscard]] RpcResult<ListFriendsRpcResponse> ListFriends(
        const ListFriendsRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<CheckPrivateChatPermissionRpcResponse>
    CheckPrivateChatPermission(
        const CheckPrivateChatPermissionRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] CreateFriendRequestRpcCallResult CreateFriendRequest(
        const CreateFriendRequestRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<ListPendingIncomingFriendRequestsRpcResponse>
    ListPendingIncomingFriendRequests(
        const ListPendingIncomingFriendRequestsRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] AcceptFriendRequestRpcCallResult AcceptFriendRequest(
        const AcceptFriendRequestRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RejectFriendRequestRpcCallResult RejectFriendRequest(
        const RejectFriendRequestRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] std::size_t CachedTargetCountForTest() const;
    [[nodiscard]] std::uint64_t StubCreationCountForTest() const;

private:
    using SocialStubInterface =
        tinyimx::social::v1::SocialService::StubInterface;

    [[nodiscard]] std::shared_ptr<SocialStubInterface>
    GetOrCreateStub(const ServiceEndpoint& endpoint) const;

    [[nodiscard]] static RpcStatus MapGrpcStatus(
        const grpc::Status& status
    );

private:
    const std::shared_ptr<const ServiceEndpointProvider> endpoint_provider_;

    struct CachedStubEntry {
        std::shared_ptr<grpc::Channel> channel;
        std::shared_ptr<SocialStubInterface> stub;
        std::uint64_t last_used{0};
    };

    static constexpr std::size_t kMaxCachedTargets = 16;

    mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::string, CachedStubEntry> stub_cache_;
    mutable std::uint64_t cache_use_sequence_{0};
    mutable std::uint64_t stub_creation_count_{0};
};

}  // namespace tinyimx::rpc
