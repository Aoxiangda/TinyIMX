#include "services/intelligence/mcp/McpDomainTools.h"

#include "services/rpc/FileRpcClient.h"
#include "services/rpc/GroupRpcClient.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/UserRpcClient.h"

#include <memory>
#include <utility>

namespace tinyimx::mcp {
namespace {

class RpcDomainBackend final : public DomainBackend {
public:
    explicit RpcDomainBackend(
        std::shared_ptr<const rpc::ServiceEndpointProvider> endpoint_provider
    )
        : user_(endpoint_provider),
          social_(endpoint_provider),
          message_(endpoint_provider),
          group_(endpoint_provider),
          file_(std::move(endpoint_provider)) {}

    rpc::RpcResult<rpc::GetUserProfileRpcResponse> GetSelfProfile(
        std::uint64_t actor_user_id, const rpc::RpcCallOptions& options
    ) const override {
        return user_.GetUserProfile({actor_user_id}, options);
    }

    rpc::RpcResult<rpc::ListFriendsRpcResponse> ListFriends(
        const rpc::ListFriendsRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return social_.ListFriends(request, options);
    }

    rpc::RpcResult<rpc::ListConversationsRpcResponse> ListConversations(
        const rpc::ListConversationsRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return message_.ListConversations(request, options);
    }

    rpc::RpcResult<rpc::ListHistoryRpcResponse> ListHistory(
        const rpc::ListHistoryRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return message_.ListHistory(request, options);
    }

    rpc::RpcResult<rpc::GetGroupRpcResponse> GetGroup(
        const rpc::GetGroupRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return group_.GetGroup(request, options);
    }

    rpc::RpcResult<rpc::ListMyGroupsRpcResponse> ListMyGroups(
        const rpc::ListMyGroupsRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return group_.ListMyGroups(request, options);
    }

    rpc::RpcResult<rpc::ListGroupMembersRpcResponse> ListGroupMembers(
        const rpc::ListGroupMembersRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return group_.ListGroupMembers(request, options);
    }

    rpc::RpcResult<rpc::GetDownloadInfoRpcResponse> GetFileMetadata(
        const rpc::GetDownloadInfoRpcRequest& request,
        const rpc::RpcCallOptions& options
    ) const override {
        return file_.GetDownloadInfo(request, options);
    }

private:
    rpc::UserRpcClient user_;
    rpc::SocialRpcClient social_;
    rpc::MessageRpcClient message_;
    rpc::GroupRpcClient group_;
    rpc::FileRpcClient file_;
};

}  // namespace

std::shared_ptr<DomainBackend> MakeRpcDomainBackend(
    std::shared_ptr<const rpc::ServiceEndpointProvider> endpoint_provider
) {
    return std::make_shared<RpcDomainBackend>(std::move(endpoint_provider));
}

}  // namespace tinyimx::mcp
