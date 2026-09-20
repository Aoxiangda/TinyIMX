#include "services/rpc/GroupRpcClient.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace tinyimx::rpc {
namespace {

using ProtoGroup = tinyimx::group::v1::GroupRecord;
using ProtoMember = tinyimx::group::v1::GroupMemberRecord;

void FillMeta(const RpcCallOptions& options, tinyimx::common::v1::RequestMeta* meta) {
    if (!meta) return;
    meta->set_request_id(options.request_id);
    meta->set_trace_id(options.trace_id);
    meta->set_caller_service(options.caller_service);
    meta->set_caller_instance(options.caller_instance);
}

std::optional<GroupRpcStatus> ToStatus(tinyimx::group::v1::GroupStatus value) {
    switch (value) {
        case tinyimx::group::v1::GROUP_STATUS_ACTIVE: return GroupRpcStatus::kActive;
        case tinyimx::group::v1::GROUP_STATUS_DISBANDED: return GroupRpcStatus::kDisbanded;
        default: return std::nullopt;
    }
}
std::optional<GroupRpcJoinPolicy> ToJoinPolicy(tinyimx::group::v1::GroupJoinPolicy value) {
    switch (value) {
        case tinyimx::group::v1::GROUP_JOIN_POLICY_INVITE_ONLY: return GroupRpcJoinPolicy::kInviteOnly;
        case tinyimx::group::v1::GROUP_JOIN_POLICY_OPEN: return GroupRpcJoinPolicy::kOpen;
        default: return std::nullopt;
    }
}
std::optional<GroupRpcRole> ToRole(tinyimx::group::v1::GroupRole value) {
    switch (value) {
        case tinyimx::group::v1::GROUP_ROLE_OWNER: return GroupRpcRole::kOwner;
        case tinyimx::group::v1::GROUP_ROLE_ADMIN: return GroupRpcRole::kAdmin;
        case tinyimx::group::v1::GROUP_ROLE_MEMBER: return GroupRpcRole::kMember;
        default: return std::nullopt;
    }
}
std::optional<GroupRpcMemberStatus> ToMemberStatus(tinyimx::group::v1::GroupMemberStatus value) {
    switch (value) {
        case tinyimx::group::v1::GROUP_MEMBER_STATUS_ACTIVE: return GroupRpcMemberStatus::kActive;
        case tinyimx::group::v1::GROUP_MEMBER_STATUS_LEFT: return GroupRpcMemberStatus::kLeft;
        case tinyimx::group::v1::GROUP_MEMBER_STATUS_KICKED: return GroupRpcMemberStatus::kKicked;
        default: return std::nullopt;
    }
}
std::optional<GroupRpcMutationOutcome> ToMutationOutcome(tinyimx::group::v1::GroupMutationResult value) {
    switch (value) {
        case tinyimx::group::v1::GROUP_MUTATION_RESULT_APPLIED: return GroupRpcMutationOutcome::kApplied;
        case tinyimx::group::v1::GROUP_MUTATION_RESULT_REUSED: return GroupRpcMutationOutcome::kReused;
        case tinyimx::group::v1::GROUP_MUTATION_RESULT_IDEMPOTENCY_CONFLICT: return GroupRpcMutationOutcome::kIdempotencyConflict;
        default: return std::nullopt;
    }
}

tinyimx::group::v1::GroupJoinPolicy ToProto(GroupRpcJoinPolicy value) {
    switch (value) {
        case GroupRpcJoinPolicy::kInviteOnly: return tinyimx::group::v1::GROUP_JOIN_POLICY_INVITE_ONLY;
        case GroupRpcJoinPolicy::kOpen: return tinyimx::group::v1::GROUP_JOIN_POLICY_OPEN;
    }
    return tinyimx::group::v1::GROUP_JOIN_POLICY_UNSPECIFIED;
}
tinyimx::group::v1::GroupRole ToProto(GroupRpcRole value) {
    switch (value) {
        case GroupRpcRole::kOwner: return tinyimx::group::v1::GROUP_ROLE_OWNER;
        case GroupRpcRole::kAdmin: return tinyimx::group::v1::GROUP_ROLE_ADMIN;
        case GroupRpcRole::kMember: return tinyimx::group::v1::GROUP_ROLE_MEMBER;
    }
    return tinyimx::group::v1::GROUP_ROLE_UNSPECIFIED;
}

std::optional<GroupRpcView> ToGroup(const ProtoGroup& record) {
    const auto status = ToStatus(record.status());
    const auto policy = ToJoinPolicy(record.join_policy());
    if (!status || !policy || record.group_id() == 0 || record.owner_user_id() == 0 ||
        record.name().empty() || record.max_members() == 0 || record.version() == 0 ||
        record.member_version() == 0) {
        return std::nullopt;
    }
    GroupRpcView out;
    out.group_id = record.group_id(); out.name = record.name(); out.description = record.description();
    out.avatar_url = record.avatar_url(); out.owner_user_id = record.owner_user_id(); out.status = *status;
    out.join_policy = *policy; out.max_members = record.max_members(); out.version = record.version();
    out.member_version = record.member_version(); out.created_at = record.created_at(); out.updated_at = record.updated_at();
    out.disbanded_at = record.disbanded_at(); out.disbanded_by_user_id = record.disbanded_by_user_id();
    return out;
}

std::optional<GroupMemberRpcView> ToMember(const ProtoMember& record) {
    const auto role = ToRole(record.role());
    const auto status = ToMemberStatus(record.status());
    if (!role || !status || record.group_id() == 0 || record.user_id() == 0 || record.membership_epoch() == 0) {
        return std::nullopt;
    }
    GroupMemberRpcView out;
    out.group_id = record.group_id(); out.user_id = record.user_id(); out.role = *role; out.status = *status;
    out.membership_epoch = record.membership_epoch(); out.muted_until = record.muted_until();
    out.joined_at = record.joined_at(); out.left_at = record.left_at(); out.updated_at = record.updated_at();
    return out;
}

bool ValidOperationId(const std::string& value) { return !value.empty() && value.size() <= 64; }
bool ValidPageLimit(std::uint32_t value) { return value > 0 && value <= 100; }

template <typename T>
RpcResult<T> Failure(RpcErrorCode code, std::string message) {
    return RpcResult<T>::Failure(code, std::move(message));
}

RpcResult<GroupMutationRpcResponse> ParseMutation(const tinyimx::group::v1::GroupMutationResponse& response) {
    const auto outcome = ToMutationOutcome(response.result());
    const auto group = ToGroup(response.group());
    if (!outcome || !group) {
        return Failure<GroupMutationRpcResponse>(RpcErrorCode::kDataLoss, "GroupService returned invalid mutation response");
    }
    GroupMutationRpcResponse out;
    out.outcome = *outcome;
    out.group = *group;
    out.message = response.message();
    return RpcResult<GroupMutationRpcResponse>::Success(std::move(out));
}

}  // namespace

GroupRpcClient::GroupRpcClient(std::shared_ptr<const ServiceEndpointProvider> endpoint_provider)
    : endpoint_provider_(std::move(endpoint_provider)) {}

std::shared_ptr<GroupRpcClient::GroupStubInterface> GroupRpcClient::PrepareStub(
    const RpcCallOptions& options, const char* operation, RpcStatus* failure) const {
    if (!failure) return nullptr;
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) {
        *failure = {RpcErrorCode::kDeadlineExceeded, std::string(operation) + " remaining RPC budget is exhausted"};
        return nullptr;
    }
    if (!endpoint_provider_) {
        *failure = {RpcErrorCode::kUnavailable, "GroupService endpoint provider is not configured"};
        return nullptr;
    }
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kGroup);
    if (!endpoint || endpoint->target.empty()) {
        *failure = {RpcErrorCode::kUnavailable, "GroupService endpoint is unavailable"};
        return nullptr;
    }
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) {
        *failure = {RpcErrorCode::kUnavailable, "GroupService gRPC stub could not be created"};
        return nullptr;
    }
    *failure = RpcStatus::Ok();
    return stub;
}

#define TINYIMX_GROUP_PREPARE(OPNAME, RESPONSE_TYPE) \
    RpcStatus preflight; \
    auto stub = PrepareStub(options, OPNAME, &preflight); \
    if (!stub) return Failure<RESPONSE_TYPE>(preflight.code, preflight.message); \
    grpc::ClientContext context; \
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout)

RpcResult<GroupMutationRpcResponse> GroupRpcClient::CreateGroup(const CreateGroupRpcRequest& request, const RpcCallOptions& options) const {
    if (request.actor_user_id == 0 || !ValidOperationId(request.client_operation_id) || request.name.empty() || request.name.size() > 128 || request.description.size() > 512 || request.avatar_url.size() > 512 || request.max_members < 2 || request.max_members > 500)
        return Failure<GroupMutationRpcResponse>(RpcErrorCode::kInvalidArgument, "invalid CreateGroup request");
    TINYIMX_GROUP_PREPARE("CreateGroup", GroupMutationRpcResponse);
    tinyimx::group::v1::CreateGroupRequest in; FillMeta(options, in.mutable_meta());
    in.set_actor_user_id(request.actor_user_id); in.set_client_operation_id(request.client_operation_id); in.set_name(request.name);
    in.set_description(request.description); in.set_avatar_url(request.avatar_url); in.set_join_policy(ToProto(request.join_policy)); in.set_max_members(request.max_members);
    tinyimx::group::v1::GroupMutationResponse out; const auto status = stub->CreateGroup(&context, in, &out);
    if (!status.ok()) { const auto mapped=MapGrpcStatus(status); return Failure<GroupMutationRpcResponse>(mapped.code,mapped.message); }
    auto parsed=ParseMutation(out); if (parsed.ok() && parsed.value->group.owner_user_id != request.actor_user_id) return Failure<GroupMutationRpcResponse>(RpcErrorCode::kDataLoss,"CreateGroup returned wrong owner identity");
    return parsed;
}

RpcResult<GetGroupRpcResponse> GroupRpcClient::GetGroup(const GetGroupRpcRequest& request, const RpcCallOptions& options) const {
    if (request.actor_user_id == 0 || request.group_id == 0) return Failure<GetGroupRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid GetGroup request");
    TINYIMX_GROUP_PREPARE("GetGroup", GetGroupRpcResponse);
    tinyimx::group::v1::GetGroupRequest in; FillMeta(options,in.mutable_meta()); in.set_actor_user_id(request.actor_user_id); in.set_group_id(request.group_id);
    tinyimx::group::v1::GetGroupResponse out; const auto status=stub->GetGroup(&context,in,&out);
    if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<GetGroupRpcResponse>(mapped.code,mapped.message);} const auto group=ToGroup(out.group());
    if(!group || group->group_id!=request.group_id) return Failure<GetGroupRpcResponse>(RpcErrorCode::kDataLoss,"GetGroup returned invalid group identity");
    GetGroupRpcResponse response; response.group=*group; return RpcResult<GetGroupRpcResponse>::Success(std::move(response));
}

RpcResult<GroupMutationRpcResponse> GroupRpcClient::UpdateGroup(const UpdateGroupRpcRequest& request, const RpcCallOptions& options) const {
    if(request.actor_user_id==0||request.group_id==0||request.expected_version==0||!ValidOperationId(request.client_operation_id)||(!request.name&&!request.description&&!request.avatar_url&&!request.join_policy)) return Failure<GroupMutationRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid UpdateGroup request");
    TINYIMX_GROUP_PREPARE("UpdateGroup", GroupMutationRpcResponse); tinyimx::group::v1::UpdateGroupRequest in; FillMeta(options,in.mutable_meta());
    in.set_actor_user_id(request.actor_user_id);in.set_client_operation_id(request.client_operation_id);in.set_group_id(request.group_id);in.set_expected_version(request.expected_version);
    if(request.name)in.set_name(*request.name); if(request.description)in.set_description(*request.description); if(request.avatar_url)in.set_avatar_url(*request.avatar_url); if(request.join_policy)in.set_join_policy(ToProto(*request.join_policy));
    tinyimx::group::v1::GroupMutationResponse out;const auto status=stub->UpdateGroup(&context,in,&out);if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<GroupMutationRpcResponse>(mapped.code,mapped.message);}auto parsed=ParseMutation(out);if(parsed.ok()&&parsed.value->group.group_id!=request.group_id)return Failure<GroupMutationRpcResponse>(RpcErrorCode::kDataLoss,"UpdateGroup returned wrong group identity");return parsed;
}

#define TINYIMX_GROUP_SIMPLE_MUTATION(METHOD, REQUEST_TYPE, PROTO_TYPE, EXTRA_VALIDATION, EXTRA_FILL) \
RpcResult<GroupMutationRpcResponse> GroupRpcClient::METHOD(const REQUEST_TYPE& request,const RpcCallOptions& options) const { \
    if(request.actor_user_id==0||request.group_id==0||!ValidOperationId(request.client_operation_id) EXTRA_VALIDATION) return Failure<GroupMutationRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid " #METHOD " request"); \
    TINYIMX_GROUP_PREPARE(#METHOD, GroupMutationRpcResponse); tinyimx::group::v1::PROTO_TYPE in; FillMeta(options,in.mutable_meta()); \
    in.set_actor_user_id(request.actor_user_id);in.set_client_operation_id(request.client_operation_id);in.set_group_id(request.group_id); EXTRA_FILL \
    tinyimx::group::v1::GroupMutationResponse out;const auto status=stub->METHOD(&context,in,&out);if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<GroupMutationRpcResponse>(mapped.code,mapped.message);}auto parsed=ParseMutation(out);if(parsed.ok()&&parsed.value->group.group_id!=request.group_id)return Failure<GroupMutationRpcResponse>(RpcErrorCode::kDataLoss,#METHOD " returned wrong group identity");return parsed; \
}

TINYIMX_GROUP_SIMPLE_MUTATION(DisbandGroup,DisbandGroupRpcRequest,DisbandGroupRequest,||request.expected_version==0,in.set_expected_version(request.expected_version);)
TINYIMX_GROUP_SIMPLE_MUTATION(JoinGroup,JoinGroupRpcRequest,JoinGroupRequest,,)
TINYIMX_GROUP_SIMPLE_MUTATION(LeaveGroup,LeaveGroupRpcRequest,LeaveGroupRequest,,)
TINYIMX_GROUP_SIMPLE_MUTATION(InviteMember,InviteMemberRpcRequest,InviteMemberRequest,||request.target_user_id==0,in.set_target_user_id(request.target_user_id);)
TINYIMX_GROUP_SIMPLE_MUTATION(KickMember,KickMemberRpcRequest,KickMemberRequest,||request.target_user_id==0,in.set_target_user_id(request.target_user_id);)
TINYIMX_GROUP_SIMPLE_MUTATION(TransferOwnership,TransferOwnershipRpcRequest,TransferOwnershipRequest,||request.target_user_id==0||request.target_user_id==request.actor_user_id,in.set_target_user_id(request.target_user_id);)

RpcResult<GroupMutationRpcResponse> GroupRpcClient::SetMemberRole(const SetMemberRoleRpcRequest& request,const RpcCallOptions& options) const {
    if(request.actor_user_id==0||request.group_id==0||request.target_user_id==0||!ValidOperationId(request.client_operation_id)||request.role==GroupRpcRole::kOwner) return Failure<GroupMutationRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid SetMemberRole request");
    TINYIMX_GROUP_PREPARE("SetMemberRole",GroupMutationRpcResponse);tinyimx::group::v1::SetMemberRoleRequest in;FillMeta(options,in.mutable_meta());in.set_actor_user_id(request.actor_user_id);in.set_client_operation_id(request.client_operation_id);in.set_group_id(request.group_id);in.set_target_user_id(request.target_user_id);in.set_role(ToProto(request.role));tinyimx::group::v1::GroupMutationResponse out;const auto status=stub->SetMemberRole(&context,in,&out);if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<GroupMutationRpcResponse>(mapped.code,mapped.message);}return ParseMutation(out);
}
RpcResult<GroupMutationRpcResponse> GroupRpcClient::SetMemberMute(const SetMemberMuteRpcRequest& request,const RpcCallOptions& options) const {
    if(request.actor_user_id==0||request.group_id==0||request.target_user_id==0||!ValidOperationId(request.client_operation_id)||request.muted_until.size()>64) return Failure<GroupMutationRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid SetMemberMute request");
    TINYIMX_GROUP_PREPARE("SetMemberMute",GroupMutationRpcResponse);tinyimx::group::v1::SetMemberMuteRequest in;FillMeta(options,in.mutable_meta());in.set_actor_user_id(request.actor_user_id);in.set_client_operation_id(request.client_operation_id);in.set_group_id(request.group_id);in.set_target_user_id(request.target_user_id);in.set_muted_until(request.muted_until);tinyimx::group::v1::GroupMutationResponse out;const auto status=stub->SetMemberMute(&context,in,&out);if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<GroupMutationRpcResponse>(mapped.code,mapped.message);}return ParseMutation(out);
}

RpcResult<ListGroupMembersRpcResponse> GroupRpcClient::ListGroupMembers(const ListGroupMembersRpcRequest& request,const RpcCallOptions& options) const {
    if(request.actor_user_id==0||request.group_id==0||!ValidPageLimit(request.limit))return Failure<ListGroupMembersRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid ListGroupMembers request");
    TINYIMX_GROUP_PREPARE("ListGroupMembers",ListGroupMembersRpcResponse);tinyimx::group::v1::ListGroupMembersRequest in;FillMeta(options,in.mutable_meta());in.set_actor_user_id(request.actor_user_id);in.set_group_id(request.group_id);in.set_after_user_id(request.after_user_id);in.set_limit(request.limit);tinyimx::group::v1::ListGroupMembersResponse out;const auto status=stub->ListGroupMembers(&context,in,&out);if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<ListGroupMembersRpcResponse>(mapped.code,mapped.message);}ListGroupMembersRpcResponse response;response.has_more=out.has_more();response.members.reserve(static_cast<std::size_t>(out.members_size()));std::uint64_t previous=request.after_user_id;for(const auto& item:out.members()){auto member=ToMember(item);if(!member||member->group_id!=request.group_id||member->user_id<=previous)return Failure<ListGroupMembersRpcResponse>(RpcErrorCode::kDataLoss,"ListGroupMembers returned invalid page");previous=member->user_id;response.members.push_back(std::move(*member));}return RpcResult<ListGroupMembersRpcResponse>::Success(std::move(response));
}
RpcResult<ListMyGroupsRpcResponse> GroupRpcClient::ListMyGroups(const ListMyGroupsRpcRequest& request,const RpcCallOptions& options) const {
    if(request.actor_user_id==0||!ValidPageLimit(request.limit))return Failure<ListMyGroupsRpcResponse>(RpcErrorCode::kInvalidArgument,"invalid ListMyGroups request");
    TINYIMX_GROUP_PREPARE("ListMyGroups",ListMyGroupsRpcResponse);tinyimx::group::v1::ListMyGroupsRequest in;FillMeta(options,in.mutable_meta());in.set_actor_user_id(request.actor_user_id);in.set_after_group_id(request.after_group_id);in.set_limit(request.limit);tinyimx::group::v1::ListMyGroupsResponse out;const auto status=stub->ListMyGroups(&context,in,&out);if(!status.ok()){const auto mapped=MapGrpcStatus(status);return Failure<ListMyGroupsRpcResponse>(mapped.code,mapped.message);}ListMyGroupsRpcResponse response;response.has_more=out.has_more();response.groups.reserve(static_cast<std::size_t>(out.groups_size()));std::uint64_t previous=request.after_group_id;for(const auto& item:out.groups()){auto group=ToGroup(item);if(!group||group->group_id<=previous)return Failure<ListMyGroupsRpcResponse>(RpcErrorCode::kDataLoss,"ListMyGroups returned invalid page");previous=group->group_id;response.groups.push_back(std::move(*group));}return RpcResult<ListMyGroupsRpcResponse>::Success(std::move(response));
}
RpcResult<CheckGroupSendPermissionRpcResponse>
GroupRpcClient::CheckGroupSendPermission(
    const CheckGroupSendPermissionRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.actor_user_id == 0 || request.group_id == 0) {
        return Failure<CheckGroupSendPermissionRpcResponse>(
            RpcErrorCode::kInvalidArgument,
            "invalid CheckGroupSendPermission request"
        );
    }

    TINYIMX_GROUP_PREPARE(
        "CheckGroupSendPermission",
        CheckGroupSendPermissionRpcResponse
    );

    tinyimx::group::v1::CheckGroupSendPermissionRequest in;
    FillMeta(options, in.mutable_meta());
    in.set_actor_user_id(request.actor_user_id);
    in.set_group_id(request.group_id);

    tinyimx::group::v1::CheckGroupSendPermissionResponse out;
    const auto status = stub->CheckGroupSendPermission(&context, in, &out);
    if (!status.ok()) {
        const auto mapped = MapGrpcStatus(status);
        return Failure<CheckGroupSendPermissionRpcResponse>(mapped.code, mapped.message);
    }

    const auto role = ToRole(out.role());
    if (out.allowed()) {
        if (!role || out.membership_epoch() == 0 || out.member_version() == 0) {
            return Failure<CheckGroupSendPermissionRpcResponse>(
                RpcErrorCode::kDataLoss,
                "CheckGroupSendPermission returned invalid allowed authorization snapshot"
            );
        }
    }

    CheckGroupSendPermissionRpcResponse response;
    response.allowed = out.allowed();
    if (role.has_value()) response.role = *role;
    response.membership_epoch = out.membership_epoch();
    response.member_version = out.member_version();
    response.message = out.message();
    return RpcResult<CheckGroupSendPermissionRpcResponse>::Success(std::move(response));
}

RpcResult<PrepareGroupMessageSendRpcResponse> GroupRpcClient::PrepareGroupMessageSend(
    const PrepareGroupMessageSendRpcRequest& request,
    const RpcCallOptions& options
) const {
    if (request.actor_user_id == 0 || request.group_id == 0) {
        return Failure<PrepareGroupMessageSendRpcResponse>(
            RpcErrorCode::kInvalidArgument, "invalid PrepareGroupMessageSend request");
    }
    TINYIMX_GROUP_PREPARE("PrepareGroupMessageSend", PrepareGroupMessageSendRpcResponse);
    tinyimx::group::v1::PrepareGroupMessageSendRequest in;
    FillMeta(options, in.mutable_meta());
    in.set_actor_user_id(request.actor_user_id);
    in.set_group_id(request.group_id);
    tinyimx::group::v1::PrepareGroupMessageSendResponse out;
    const auto status = stub->PrepareGroupMessageSend(&context, in, &out);
    if (!status.ok()) {
        const auto mapped = MapGrpcStatus(status);
        return Failure<PrepareGroupMessageSendRpcResponse>(mapped.code, mapped.message);
    }
    const auto role = ToRole(out.role());
    PrepareGroupMessageSendRpcResponse response;
    response.allowed = out.allowed();
    response.message = out.message();
    response.membership_epoch = out.membership_epoch();
    response.member_version = out.member_version();
    if (role.has_value()) response.role = *role;
    if (out.allowed()) {
        if (!role || response.membership_epoch == 0 || response.member_version == 0) {
            return Failure<PrepareGroupMessageSendRpcResponse>(
                RpcErrorCode::kDataLoss, "PrepareGroupMessageSend returned invalid authorization snapshot");
        }
        response.recipient_user_ids.reserve(static_cast<std::size_t>(out.recipient_user_ids_size()));
        std::uint64_t previous = 0;
        for (const auto user_id : out.recipient_user_ids()) {
            if (user_id == 0 || user_id == request.actor_user_id || user_id <= previous) {
                return Failure<PrepareGroupMessageSendRpcResponse>(
                    RpcErrorCode::kDataLoss, "PrepareGroupMessageSend returned invalid recipient snapshot");
            }
            previous = user_id;
            response.recipient_user_ids.push_back(user_id);
        }
    } else if (out.recipient_user_ids_size() != 0) {
        return Failure<PrepareGroupMessageSendRpcResponse>(
            RpcErrorCode::kDataLoss, "denied PrepareGroupMessageSend returned recipients");
    }
    return RpcResult<PrepareGroupMessageSendRpcResponse>::Success(std::move(response));
}

#undef TINYIMX_GROUP_SIMPLE_MUTATION
#undef TINYIMX_GROUP_PREPARE

std::size_t GroupRpcClient::CachedTargetCountForTest() const { std::lock_guard<std::mutex> lock(cache_mutex_); return stub_cache_.size(); }
std::uint64_t GroupRpcClient::StubCreationCountForTest() const { std::lock_guard<std::mutex> lock(cache_mutex_); return stub_creation_count_; }

std::shared_ptr<GroupRpcClient::GroupStubInterface> GroupRpcClient::GetOrCreateStub(const ServiceEndpoint& endpoint) const {
    if(endpoint.target.empty())return nullptr;std::lock_guard<std::mutex> lock(cache_mutex_);const std::uint64_t sequence=++cache_use_sequence_;auto existing=stub_cache_.find(endpoint.target);if(existing!=stub_cache_.end()){existing->second.last_used=sequence;return existing->second.stub;}auto channel=grpc::CreateChannel(endpoint.target,grpc::InsecureChannelCredentials());if(!channel)return nullptr;auto unique_stub=tinyimx::group::v1::GroupService::NewStub(channel);if(!unique_stub)return nullptr;std::shared_ptr<GroupStubInterface> stub(std::move(unique_stub));if(stub_cache_.size()>=kMaxCachedTargets){const auto victim=std::min_element(stub_cache_.begin(),stub_cache_.end(),[](const auto& a,const auto& b){return a.second.last_used<b.second.last_used;});if(victim!=stub_cache_.end())stub_cache_.erase(victim);}CachedStubEntry entry;entry.channel=std::move(channel);entry.stub=stub;entry.last_used=sequence;stub_cache_.emplace(endpoint.target,std::move(entry));++stub_creation_count_;return stub;
}

RpcStatus GroupRpcClient::MapGrpcStatus(const grpc::Status& status) {
    if(status.ok())return RpcStatus::Ok();RpcErrorCode code=RpcErrorCode::kUnknown;switch(status.error_code()){
        case grpc::StatusCode::INVALID_ARGUMENT:code=RpcErrorCode::kInvalidArgument;break;case grpc::StatusCode::CANCELLED:code=RpcErrorCode::kCancelled;break;case grpc::StatusCode::DEADLINE_EXCEEDED:code=RpcErrorCode::kDeadlineExceeded;break;case grpc::StatusCode::NOT_FOUND:code=RpcErrorCode::kNotFound;break;case grpc::StatusCode::ALREADY_EXISTS:code=RpcErrorCode::kAlreadyExists;break;case grpc::StatusCode::PERMISSION_DENIED:code=RpcErrorCode::kPermissionDenied;break;case grpc::StatusCode::UNAUTHENTICATED:code=RpcErrorCode::kUnauthenticated;break;case grpc::StatusCode::RESOURCE_EXHAUSTED:code=RpcErrorCode::kResourceExhausted;break;case grpc::StatusCode::FAILED_PRECONDITION:code=RpcErrorCode::kFailedPrecondition;break;case grpc::StatusCode::ABORTED:code=RpcErrorCode::kAborted;break;case grpc::StatusCode::OUT_OF_RANGE:code=RpcErrorCode::kOutOfRange;break;case grpc::StatusCode::UNIMPLEMENTED:code=RpcErrorCode::kUnimplemented;break;case grpc::StatusCode::UNAVAILABLE:code=RpcErrorCode::kUnavailable;break;case grpc::StatusCode::INTERNAL:code=RpcErrorCode::kInternal;break;case grpc::StatusCode::DATA_LOSS:code=RpcErrorCode::kDataLoss;break;case grpc::StatusCode::OK:code=RpcErrorCode::kOk;break;default:code=RpcErrorCode::kUnknown;break;}
    return {code,status.error_message()};
}

}  // namespace tinyimx::rpc
