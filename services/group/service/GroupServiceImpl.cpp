#include "services/group/service/GroupServiceImpl.h"

#include <grpcpp/grpcpp.h>

#include <optional>
#include <string>
#include <utility>

namespace tinyimx::group {
namespace {

grpc::Status MapApplicationFailure(GroupApplicationStatus status, const std::string& message) {
    switch (status) {
        case GroupApplicationStatus::kInvalidArgument: return {grpc::StatusCode::INVALID_ARGUMENT, message};
        case GroupApplicationStatus::kNotFound: return {grpc::StatusCode::NOT_FOUND, message};
        case GroupApplicationStatus::kPermissionDenied: return {grpc::StatusCode::PERMISSION_DENIED, message};
        case GroupApplicationStatus::kFailedPrecondition: return {grpc::StatusCode::FAILED_PRECONDITION, message};
        case GroupApplicationStatus::kAlreadyExists: return {grpc::StatusCode::ALREADY_EXISTS, message};
        case GroupApplicationStatus::kResourceExhausted: return {grpc::StatusCode::RESOURCE_EXHAUSTED, message};
        case GroupApplicationStatus::kAborted: return {grpc::StatusCode::ABORTED, message};
        case GroupApplicationStatus::kInvalidRecord: return {grpc::StatusCode::DATA_LOSS, message};
        case GroupApplicationStatus::kStorageError: return {grpc::StatusCode::UNAVAILABLE, message};
        case GroupApplicationStatus::kSucceeded: return grpc::Status::OK;
    }
    return {grpc::StatusCode::INTERNAL, "unknown GroupService application status"};
}

std::optional<GroupJoinPolicy> FromProtoJoinPolicy(tinyimx::group::v1::GroupJoinPolicy policy) {
    switch (policy) {
        case tinyimx::group::v1::GROUP_JOIN_POLICY_INVITE_ONLY: return GroupJoinPolicy::kInviteOnly;
        case tinyimx::group::v1::GROUP_JOIN_POLICY_OPEN: return GroupJoinPolicy::kOpen;
        default: return std::nullopt;
    }
}

std::optional<GroupRole> FromProtoRole(tinyimx::group::v1::GroupRole role) {
    switch (role) {
        case tinyimx::group::v1::GROUP_ROLE_OWNER: return GroupRole::kOwner;
        case tinyimx::group::v1::GROUP_ROLE_ADMIN: return GroupRole::kAdmin;
        case tinyimx::group::v1::GROUP_ROLE_MEMBER: return GroupRole::kMember;
        default: return std::nullopt;
    }
}

tinyimx::group::v1::GroupStatus ToProtoStatus(GroupStatus status) {
    return status == GroupStatus::kActive ? tinyimx::group::v1::GROUP_STATUS_ACTIVE
                                          : tinyimx::group::v1::GROUP_STATUS_DISBANDED;
}

tinyimx::group::v1::GroupJoinPolicy ToProtoJoinPolicy(GroupJoinPolicy policy) {
    return policy == GroupJoinPolicy::kInviteOnly
        ? tinyimx::group::v1::GROUP_JOIN_POLICY_INVITE_ONLY
        : tinyimx::group::v1::GROUP_JOIN_POLICY_OPEN;
}

tinyimx::group::v1::GroupRole ToProtoRole(GroupRole role) {
    switch (role) {
        case GroupRole::kOwner: return tinyimx::group::v1::GROUP_ROLE_OWNER;
        case GroupRole::kAdmin: return tinyimx::group::v1::GROUP_ROLE_ADMIN;
        case GroupRole::kMember: return tinyimx::group::v1::GROUP_ROLE_MEMBER;
    }
    return tinyimx::group::v1::GROUP_ROLE_UNSPECIFIED;
}

tinyimx::group::v1::GroupMemberStatus ToProtoMemberStatus(GroupMemberStatus status) {
    switch (status) {
        case GroupMemberStatus::kActive: return tinyimx::group::v1::GROUP_MEMBER_STATUS_ACTIVE;
        case GroupMemberStatus::kLeft: return tinyimx::group::v1::GROUP_MEMBER_STATUS_LEFT;
        case GroupMemberStatus::kKicked: return tinyimx::group::v1::GROUP_MEMBER_STATUS_KICKED;
    }
    return tinyimx::group::v1::GROUP_MEMBER_STATUS_UNSPECIFIED;
}

tinyimx::group::v1::GroupMutationResult ToProtoOutcome(GroupMutationOutcome outcome) {
    switch (outcome) {
        case GroupMutationOutcome::kApplied: return tinyimx::group::v1::GROUP_MUTATION_RESULT_APPLIED;
        case GroupMutationOutcome::kReused: return tinyimx::group::v1::GROUP_MUTATION_RESULT_REUSED;
        case GroupMutationOutcome::kIdempotencyConflict: return tinyimx::group::v1::GROUP_MUTATION_RESULT_IDEMPOTENCY_CONFLICT;
    }
    return tinyimx::group::v1::GROUP_MUTATION_RESULT_UNSPECIFIED;
}

void FillProtoGroup(const GroupView& group, tinyimx::group::v1::GroupRecord* output) {
    if (output == nullptr) return;
    output->set_group_id(group.group_id);
    output->set_name(group.name);
    output->set_description(group.description);
    output->set_avatar_url(group.avatar_url);
    output->set_owner_user_id(group.owner_user_id);
    output->set_status(ToProtoStatus(group.status));
    output->set_join_policy(ToProtoJoinPolicy(group.join_policy));
    output->set_max_members(group.max_members);
    output->set_version(group.version);
    output->set_member_version(group.member_version);
    output->set_created_at(group.created_at);
    output->set_updated_at(group.updated_at);
    output->set_disbanded_at(group.disbanded_at);
    output->set_disbanded_by_user_id(group.disbanded_by_user_id);
}

void FillProtoMember(const GroupMemberView& member, tinyimx::group::v1::GroupMemberRecord* output) {
    if (output == nullptr) return;
    output->set_group_id(member.group_id);
    output->set_user_id(member.user_id);
    output->set_role(ToProtoRole(member.role));
    output->set_status(ToProtoMemberStatus(member.status));
    output->set_membership_epoch(member.membership_epoch);
    output->set_muted_until(member.muted_until);
    output->set_joined_at(member.joined_at);
    output->set_left_at(member.left_at);
    output->set_updated_at(member.updated_at);
}

grpc::Status FillMutationResponse(
    const GroupRepositoryMutationResult& result,
    tinyimx::group::v1::GroupMutationResponse* response
) {
    if (!result.Completed()) return MapApplicationFailure(result.status, result.message);
    response->set_result(ToProtoOutcome(result.outcome));
    response->set_message(result.message);
    if (result.group.has_value()) FillProtoGroup(*result.group, response->mutable_group());
    return grpc::Status::OK;
}

bool InvalidRpcArgs(grpc::ServerContext* context, const void* request, const void* response) {
    return context == nullptr || request == nullptr || response == nullptr;
}

}  // namespace

GroupServiceImpl::GroupServiceImpl(GroupApplicationService* application_service)
    : application_service_(application_service) {}

grpc::Status GroupServiceImpl::CreateGroup(grpc::ServerContext* c, const tinyimx::group::v1::CreateGroupRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c, r, o)) return {grpc::StatusCode::INVALID_ARGUMENT, "invalid CreateGroup RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "GroupService application service is unavailable"};
    const auto policy = FromProtoJoinPolicy(r->join_policy());
    if (!policy) return {grpc::StatusCode::INVALID_ARGUMENT, "invalid group join policy"};
    CreateGroupCommand cmd{r->actor_user_id(), r->client_operation_id(), r->name(), r->description(), r->avatar_url(), *policy, r->max_members()};
    return FillMutationResponse(application_service_->CreateGroup(std::move(cmd)), o);
}

grpc::Status GroupServiceImpl::GetGroup(grpc::ServerContext* c, const tinyimx::group::v1::GetGroupRequest* r, tinyimx::group::v1::GetGroupResponse* o) {
    if (InvalidRpcArgs(c, r, o)) return {grpc::StatusCode::INVALID_ARGUMENT, "invalid GetGroup RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "GroupService application service is unavailable"};
    auto result = application_service_->GetGroup(r->actor_user_id(), r->group_id());
    if (!result.Found()) return MapApplicationFailure(result.status, result.message);
    FillProtoGroup(*result.group, o->mutable_group());
    return grpc::Status::OK;
}

grpc::Status GroupServiceImpl::UpdateGroup(grpc::ServerContext* c, const tinyimx::group::v1::UpdateGroupRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c, r, o)) return {grpc::StatusCode::INVALID_ARGUMENT, "invalid UpdateGroup RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "GroupService application service is unavailable"};
    UpdateGroupCommand cmd;
    cmd.actor_user_id=r->actor_user_id(); cmd.client_operation_id=r->client_operation_id(); cmd.group_id=r->group_id(); cmd.expected_version=r->expected_version();
    if (r->has_name()) cmd.name=r->name();
    if (r->has_description()) cmd.description=r->description();
    if (r->has_avatar_url()) cmd.avatar_url=r->avatar_url();
    if (r->has_join_policy()) { const auto p=FromProtoJoinPolicy(r->join_policy()); if(!p) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid group join policy"}; cmd.join_policy=*p; }
    return FillMutationResponse(application_service_->UpdateGroup(std::move(cmd)), o);
}

grpc::Status GroupServiceImpl::DisbandGroup(grpc::ServerContext* c, const tinyimx::group::v1::DisbandGroupRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c, r, o)) return {grpc::StatusCode::INVALID_ARGUMENT, "invalid DisbandGroup RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE, "GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->DisbandGroup({r->actor_user_id(), r->client_operation_id(), r->group_id(), r->expected_version()}), o);
}

grpc::Status GroupServiceImpl::JoinGroup(grpc::ServerContext* c, const tinyimx::group::v1::JoinGroupRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid JoinGroup RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->JoinGroup({r->actor_user_id(),r->client_operation_id(),r->group_id()}),o);
}

grpc::Status GroupServiceImpl::LeaveGroup(grpc::ServerContext* c, const tinyimx::group::v1::LeaveGroupRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid LeaveGroup RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->LeaveGroup({r->actor_user_id(),r->client_operation_id(),r->group_id()}),o);
}

grpc::Status GroupServiceImpl::InviteMember(grpc::ServerContext* c, const tinyimx::group::v1::InviteMemberRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid InviteMember RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->InviteMember({r->actor_user_id(),r->client_operation_id(),r->group_id(),r->target_user_id()}),o);
}

grpc::Status GroupServiceImpl::KickMember(grpc::ServerContext* c, const tinyimx::group::v1::KickMemberRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid KickMember RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->KickMember({r->actor_user_id(),r->client_operation_id(),r->group_id(),r->target_user_id()}),o);
}

grpc::Status GroupServiceImpl::SetMemberRole(grpc::ServerContext* c, const tinyimx::group::v1::SetMemberRoleRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid SetMemberRole RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    const auto role=FromProtoRole(r->role());
    if (!role) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid group role"};
    return FillMutationResponse(application_service_->SetMemberRole({r->actor_user_id(),r->client_operation_id(),r->group_id(),r->target_user_id(),*role}),o);
}

grpc::Status GroupServiceImpl::SetMemberMute(grpc::ServerContext* c, const tinyimx::group::v1::SetMemberMuteRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid SetMemberMute RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->SetMemberMute({r->actor_user_id(),r->client_operation_id(),r->group_id(),r->target_user_id(),r->muted_until()}),o);
}

grpc::Status GroupServiceImpl::TransferOwnership(grpc::ServerContext* c, const tinyimx::group::v1::TransferOwnershipRequest* r, tinyimx::group::v1::GroupMutationResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid TransferOwnership RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    return FillMutationResponse(application_service_->TransferOwnership({r->actor_user_id(),r->client_operation_id(),r->group_id(),r->target_user_id()}),o);
}

grpc::Status GroupServiceImpl::ListGroupMembers(grpc::ServerContext* c, const tinyimx::group::v1::ListGroupMembersRequest* r, tinyimx::group::v1::ListGroupMembersResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid ListGroupMembers RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    auto result=application_service_->ListGroupMembers({r->actor_user_id(),r->group_id(),r->after_user_id(),r->limit()});
    if (!result.Succeeded()) return MapApplicationFailure(result.status,result.message);
    for (const auto& member: result.members) FillProtoMember(member,o->add_members());
    o->set_has_more(result.has_more);
    return grpc::Status::OK;
}

grpc::Status GroupServiceImpl::ListMyGroups(grpc::ServerContext* c, const tinyimx::group::v1::ListMyGroupsRequest* r, tinyimx::group::v1::ListMyGroupsResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid ListMyGroups RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    auto result=application_service_->ListMyGroups({r->actor_user_id(),r->after_group_id(),r->limit()});
    if (!result.Succeeded()) return MapApplicationFailure(result.status,result.message);
    for (const auto& group: result.groups) FillProtoGroup(group,o->add_groups());
    o->set_has_more(result.has_more);
    return grpc::Status::OK;
}

grpc::Status GroupServiceImpl::CheckGroupSendPermission(grpc::ServerContext* c, const tinyimx::group::v1::CheckGroupSendPermissionRequest* r, tinyimx::group::v1::CheckGroupSendPermissionResponse* o) {
    if (InvalidRpcArgs(c,r,o)) return {grpc::StatusCode::INVALID_ARGUMENT,"invalid CheckGroupSendPermission RPC arguments"};
    if (!application_service_) return {grpc::StatusCode::UNAVAILABLE,"GroupService application service is unavailable"};
    const auto result=application_service_->CheckGroupSendPermission(r->actor_user_id(),r->group_id());
    if (!result.Succeeded()) return MapApplicationFailure(result.status,result.message);
    o->set_allowed(result.allowed);
    o->set_role(ToProtoRole(result.role));
    o->set_membership_epoch(result.membership_epoch);
    o->set_member_version(result.member_version);
    o->set_message(result.message);
    return grpc::Status::OK;
}

}  // namespace tinyimx::group
