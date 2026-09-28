#pragma once

#include "services/intelligence/mcp/McpRegistry.h"
#include "services/rpc/FileRpcTypes.h"
#include "services/rpc/GroupRpcTypes.h"
#include "services/rpc/MessageRpcTypes.h"
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/SocialRpcTypes.h"
#include "services/rpc/UserRpcTypes.h"

#include <cstdint>
#include <memory>
#include <string>

namespace tinyimx::rpc {
class ServiceEndpointProvider;
}

namespace tinyimx::mcp {

class DomainBackend {
public:
    virtual ~DomainBackend() = default;

    virtual rpc::RpcResult<rpc::GetUserProfileRpcResponse> GetSelfProfile(
        std::uint64_t actor_user_id, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::ListFriendsRpcResponse> ListFriends(
        const rpc::ListFriendsRpcRequest&, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::ListConversationsRpcResponse> ListConversations(
        const rpc::ListConversationsRpcRequest&, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::ListHistoryRpcResponse> ListHistory(
        const rpc::ListHistoryRpcRequest&, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::GetGroupRpcResponse> GetGroup(
        const rpc::GetGroupRpcRequest&, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::ListMyGroupsRpcResponse> ListMyGroups(
        const rpc::ListMyGroupsRpcRequest&, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::ListGroupMembersRpcResponse> ListGroupMembers(
        const rpc::ListGroupMembersRpcRequest&, const rpc::RpcCallOptions&) const = 0;
    virtual rpc::RpcResult<rpc::GetDownloadInfoRpcResponse> GetFileMetadata(
        const rpc::GetDownloadInfoRpcRequest&, const rpc::RpcCallOptions&) const = 0;
};

std::shared_ptr<DomainBackend> MakeRpcDomainBackend(
    std::shared_ptr<const rpc::ServiceEndpointProvider> endpoint_provider
);

bool RegisterReadOnlyDomainTools(
    Registry* registry,
    std::shared_ptr<DomainBackend> backend,
    std::string* error
);

}  // namespace tinyimx::mcp
