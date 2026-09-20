#pragma once

#include "services/rpc/GroupRpcTypes.h"
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "tinyimx/group/v1/group_service.grpc.pb.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace grpc { class Channel; }

namespace tinyimx::rpc {

class GroupRpcClient final {
public:
    explicit GroupRpcClient(std::shared_ptr<const ServiceEndpointProvider> endpoint_provider);
    GroupRpcClient(const GroupRpcClient&) = delete;
    GroupRpcClient& operator=(const GroupRpcClient&) = delete;

    [[nodiscard]] RpcResult<GroupMutationRpcResponse> CreateGroup(const CreateGroupRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GetGroupRpcResponse> GetGroup(const GetGroupRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> UpdateGroup(const UpdateGroupRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> DisbandGroup(const DisbandGroupRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> JoinGroup(const JoinGroupRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> LeaveGroup(const LeaveGroupRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> InviteMember(const InviteMemberRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> KickMember(const KickMemberRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> SetMemberRole(const SetMemberRoleRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> SetMemberMute(const SetMemberMuteRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GroupMutationRpcResponse> TransferOwnership(const TransferOwnershipRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<ListGroupMembersRpcResponse> ListGroupMembers(const ListGroupMembersRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<ListMyGroupsRpcResponse> ListMyGroups(const ListMyGroupsRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<CheckGroupSendPermissionRpcResponse> CheckGroupSendPermission(const CheckGroupSendPermissionRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<PrepareGroupMessageSendRpcResponse> PrepareGroupMessageSend(const PrepareGroupMessageSendRpcRequest&, const RpcCallOptions&) const;

    [[nodiscard]] std::size_t CachedTargetCountForTest() const;
    [[nodiscard]] std::uint64_t StubCreationCountForTest() const;

private:
    using GroupStubInterface = tinyimx::group::v1::GroupService::StubInterface;
    struct CachedStubEntry {
        std::shared_ptr<grpc::Channel> channel;
        std::shared_ptr<GroupStubInterface> stub;
        std::uint64_t last_used{0};
    };

    [[nodiscard]] std::shared_ptr<GroupStubInterface> GetOrCreateStub(const ServiceEndpoint&) const;
    [[nodiscard]] std::shared_ptr<GroupStubInterface> PrepareStub(const RpcCallOptions&, const char* operation, RpcStatus* failure) const;
    [[nodiscard]] static RpcStatus MapGrpcStatus(const grpc::Status& status);

    const std::shared_ptr<const ServiceEndpointProvider> endpoint_provider_;
    static constexpr std::size_t kMaxCachedTargets = 16;
    mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::string, CachedStubEntry> stub_cache_;
    mutable std::uint64_t cache_use_sequence_{0};
    mutable std::uint64_t stub_creation_count_{0};
};

}  // namespace tinyimx::rpc
