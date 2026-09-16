#pragma once

#include "services/group/application/GroupApplicationService.h"
#include "tinyimx/group/v1/group_service.grpc.pb.h"

namespace tinyimx::group {

class GroupServiceImpl final : public tinyimx::group::v1::GroupService::Service {
public:
    explicit GroupServiceImpl(GroupApplicationService* application_service);

    grpc::Status CreateGroup(grpc::ServerContext*, const tinyimx::group::v1::CreateGroupRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status GetGroup(grpc::ServerContext*, const tinyimx::group::v1::GetGroupRequest*, tinyimx::group::v1::GetGroupResponse*) override;
    grpc::Status UpdateGroup(grpc::ServerContext*, const tinyimx::group::v1::UpdateGroupRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status DisbandGroup(grpc::ServerContext*, const tinyimx::group::v1::DisbandGroupRequest*, tinyimx::group::v1::GroupMutationResponse*) override;

    grpc::Status JoinGroup(grpc::ServerContext*, const tinyimx::group::v1::JoinGroupRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status LeaveGroup(grpc::ServerContext*, const tinyimx::group::v1::LeaveGroupRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status InviteMember(grpc::ServerContext*, const tinyimx::group::v1::InviteMemberRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status KickMember(grpc::ServerContext*, const tinyimx::group::v1::KickMemberRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status SetMemberRole(grpc::ServerContext*, const tinyimx::group::v1::SetMemberRoleRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status SetMemberMute(grpc::ServerContext*, const tinyimx::group::v1::SetMemberMuteRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status TransferOwnership(grpc::ServerContext*, const tinyimx::group::v1::TransferOwnershipRequest*, tinyimx::group::v1::GroupMutationResponse*) override;
    grpc::Status ListGroupMembers(grpc::ServerContext*, const tinyimx::group::v1::ListGroupMembersRequest*, tinyimx::group::v1::ListGroupMembersResponse*) override;
    grpc::Status ListMyGroups(grpc::ServerContext*, const tinyimx::group::v1::ListMyGroupsRequest*, tinyimx::group::v1::ListMyGroupsResponse*) override;
    grpc::Status CheckGroupSendPermission(grpc::ServerContext*, const tinyimx::group::v1::CheckGroupSendPermissionRequest*, tinyimx::group::v1::CheckGroupSendPermissionResponse*) override;

private:
    GroupApplicationService* application_service_{nullptr};  // non-owning
};

}  // namespace tinyimx::group
