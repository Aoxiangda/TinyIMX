#include "services/group/application/GroupApplicationService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

namespace tinyimx::group {
namespace {

constexpr std::size_t kMaxOperationIdBytes = 64;
constexpr std::size_t kMaxNameBytes = 128;
constexpr std::size_t kMaxDescriptionBytes = 512;
constexpr std::size_t kMaxAvatarUrlBytes = 512;
constexpr std::uint32_t kMinMembers = 2;
constexpr std::uint32_t kMaxMembers = 500;
constexpr std::uint32_t kDefaultPageSize = 50;
constexpr std::uint32_t kMaxPageSize = 100;

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool ValidOperationId(const std::string& value) {
    return !value.empty() && value.size() <= kMaxOperationIdBytes;
}

bool ValidJoinPolicy(GroupJoinPolicy policy) {
    return policy == GroupJoinPolicy::kInviteOnly || policy == GroupJoinPolicy::kOpen;
}

bool ValidMutableRole(GroupRole role) {
    return role == GroupRole::kAdmin || role == GroupRole::kMember;
}

bool IsDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

int ParseTwoDigits(const std::string& value, std::size_t pos) {
    if (pos + 1 >= value.size() || !IsDigit(value[pos]) || !IsDigit(value[pos + 1])) {
        return -1;
    }
    return (value[pos] - '0') * 10 + (value[pos + 1] - '0');
}

int ParseFourDigits(const std::string& value, std::size_t pos) {
    if (pos + 3 >= value.size()) {
        return -1;
    }
    int output = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (!IsDigit(value[pos + i])) {
            return -1;
        }
        output = output * 10 + (value[pos + i] - '0');
    }
    return output;
}

bool LeapYear(int year) {
    return (year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0));
}

int DaysInMonth(int year, int month) {
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && LeapYear(year)) {
        return 29;
    }
    return kDays[month - 1];
}

bool NormalizeMuteTimestamp(std::string* value) {
    if (value == nullptr) {
        return false;
    }
    if (value->empty()) {
        return true;
    }

    // Contract: UTC RFC3339 with millisecond precision.
    // Example: 2026-09-20T12:30:00.000Z
    if (value->size() != 24 || (*value)[4] != '-' || (*value)[7] != '-' ||
        (*value)[10] != 'T' || (*value)[13] != ':' || (*value)[16] != ':' ||
        (*value)[19] != '.' || (*value)[23] != 'Z') {
        return false;
    }

    const int year = ParseFourDigits(*value, 0);
    const int month = ParseTwoDigits(*value, 5);
    const int day = ParseTwoDigits(*value, 8);
    const int hour = ParseTwoDigits(*value, 11);
    const int minute = ParseTwoDigits(*value, 14);
    const int second = ParseTwoDigits(*value, 17);
    const int millis = (IsDigit((*value)[20]) && IsDigit((*value)[21]) && IsDigit((*value)[22]))
        ? (((*value)[20] - '0') * 100 + ((*value)[21] - '0') * 10 + ((*value)[22] - '0'))
        : -1;

    if (year < 1970 || month < 1 || month > 12 || day < 1 ||
        day > DaysInMonth(year, month) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59 ||
        millis < 0 || millis > 999) {
        return false;
    }

    // MySQL stores UTC DATETIME(3) without a timezone suffix. The service
    // boundary requires UTC, so this conversion is lossless by contract.
    (*value)[10] = ' ';
    value->resize(23);
    return true;
}

std::uint32_t NormalizePageSize(std::uint32_t limit) {
    if (limit == 0) {
        return kDefaultPageSize;
    }
    return std::min(limit, kMaxPageSize);
}

GroupRepositoryMutationResult InvalidMutation(std::string message) {
    GroupRepositoryMutationResult result;
    result.status = GroupApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

GroupMemberListResult InvalidMemberList(std::string message) {
    GroupMemberListResult result;
    result.status = GroupApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

GroupListResult InvalidGroupList(std::string message) {
    GroupListResult result;
    result.status = GroupApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

GroupSendPermissionResult InvalidPermissionQuery(std::string message) {
    GroupSendPermissionResult result;
    result.status = GroupApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

bool ValidMutationIdentity(
    std::uint64_t actor_user_id,
    std::uint64_t group_id,
    const std::string& client_operation_id
) {
    return actor_user_id != 0 && group_id != 0 && ValidOperationId(client_operation_id);
}

}  // namespace

GroupApplicationService::GroupApplicationService(GroupRepositoryPort* repository)
    : repository_(repository) {
}

CreateGroupApplicationResult GroupApplicationService::CreateGroup(CreateGroupCommand command) {
    command.name = Trim(std::move(command.name));
    if (command.actor_user_id == 0 || !ValidOperationId(command.client_operation_id) ||
        command.name.empty() || command.name.size() > kMaxNameBytes ||
        command.description.size() > kMaxDescriptionBytes ||
        command.avatar_url.size() > kMaxAvatarUrlBytes ||
        !ValidJoinPolicy(command.join_policy) ||
        command.max_members < kMinMembers || command.max_members > kMaxMembers) {
        return InvalidMutation("invalid CreateGroup application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->CreateGroup(command);
}

GetGroupApplicationResult GroupApplicationService::GetGroup(
    std::uint64_t actor_user_id,
    std::uint64_t group_id
) {
    GroupRepositoryGetResult result;
    if (actor_user_id == 0 || group_id == 0) {
        result.status = GroupApplicationStatus::kInvalidArgument;
        result.message = "invalid GetGroup application request";
        return result;
    }
    if (repository_ == nullptr) {
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "group repository port is unavailable";
        return result;
    }
    return repository_->GetGroup(actor_user_id, group_id);
}

UpdateGroupApplicationResult GroupApplicationService::UpdateGroup(UpdateGroupCommand command) {
    if (command.name.has_value()) {
        command.name = Trim(std::move(*command.name));
    }
    const bool has_update = command.name.has_value() || command.description.has_value() ||
                            command.avatar_url.has_value() || command.join_policy.has_value();
    if (command.actor_user_id == 0 || command.group_id == 0 || command.expected_version == 0 ||
        !ValidOperationId(command.client_operation_id) || !has_update ||
        (command.name.has_value() &&
         (command.name->empty() || command.name->size() > kMaxNameBytes)) ||
        (command.description.has_value() && command.description->size() > kMaxDescriptionBytes) ||
        (command.avatar_url.has_value() && command.avatar_url->size() > kMaxAvatarUrlBytes) ||
        (command.join_policy.has_value() && !ValidJoinPolicy(*command.join_policy))) {
        return InvalidMutation("invalid UpdateGroup application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->UpdateGroup(command);
}

DisbandGroupApplicationResult GroupApplicationService::DisbandGroup(DisbandGroupCommand command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.expected_version == 0 ||
        !ValidOperationId(command.client_operation_id)) {
        return InvalidMutation("invalid DisbandGroup application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->DisbandGroup(command);
}

JoinGroupApplicationResult GroupApplicationService::JoinGroup(JoinGroupCommand command) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id)) {
        return InvalidMutation("invalid JoinGroup application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->JoinGroup(command);
}

LeaveGroupApplicationResult GroupApplicationService::LeaveGroup(LeaveGroupCommand command) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id)) {
        return InvalidMutation("invalid LeaveGroup application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->LeaveGroup(command);
}

InviteMemberApplicationResult GroupApplicationService::InviteMember(InviteMemberCommand command) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id) ||
        command.target_user_id == 0 || command.target_user_id == command.actor_user_id) {
        return InvalidMutation("invalid InviteMember application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->InviteMember(command);
}

KickMemberApplicationResult GroupApplicationService::KickMember(KickMemberCommand command) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id) ||
        command.target_user_id == 0 || command.target_user_id == command.actor_user_id) {
        return InvalidMutation("invalid KickMember application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->KickMember(command);
}

SetMemberRoleApplicationResult GroupApplicationService::SetMemberRole(SetMemberRoleCommand command) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id) ||
        command.target_user_id == 0 || command.target_user_id == command.actor_user_id ||
        !ValidMutableRole(command.role)) {
        return InvalidMutation("invalid SetMemberRole application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->SetMemberRole(command);
}

SetMemberMuteApplicationResult GroupApplicationService::SetMemberMute(SetMemberMuteCommand command) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id) ||
        command.target_user_id == 0 || command.target_user_id == command.actor_user_id ||
        !NormalizeMuteTimestamp(&command.muted_until)) {
        return InvalidMutation("invalid SetMemberMute application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->SetMemberMute(command);
}

TransferOwnershipApplicationResult GroupApplicationService::TransferOwnership(
    TransferOwnershipCommand command
) {
    if (!ValidMutationIdentity(command.actor_user_id, command.group_id, command.client_operation_id) ||
        command.target_user_id == 0 || command.target_user_id == command.actor_user_id) {
        return InvalidMutation("invalid TransferOwnership application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidMutation("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->TransferOwnership(command);
}

ListGroupMembersApplicationResult GroupApplicationService::ListGroupMembers(
    ListGroupMembersQuery query
) {
    if (query.actor_user_id == 0 || query.group_id == 0 || query.limit > kMaxPageSize) {
        return InvalidMemberList("invalid ListGroupMembers application request");
    }
    query.limit = NormalizePageSize(query.limit);
    if (repository_ == nullptr) {
        auto result = InvalidMemberList("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->ListGroupMembers(query);
}

ListMyGroupsApplicationResult GroupApplicationService::ListMyGroups(ListMyGroupsQuery query) {
    if (query.actor_user_id == 0 || query.limit > kMaxPageSize) {
        return InvalidGroupList("invalid ListMyGroups application request");
    }
    query.limit = NormalizePageSize(query.limit);
    if (repository_ == nullptr) {
        auto result = InvalidGroupList("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->ListMyGroups(query);
}

CheckGroupSendPermissionApplicationResult GroupApplicationService::CheckGroupSendPermission(
    std::uint64_t actor_user_id,
    std::uint64_t group_id
) {
    if (actor_user_id == 0 || group_id == 0) {
        return InvalidPermissionQuery("invalid CheckGroupSendPermission application request");
    }
    if (repository_ == nullptr) {
        auto result = InvalidPermissionQuery("group repository port is unavailable");
        result.status = GroupApplicationStatus::kStorageError;
        return result;
    }
    return repository_->CheckGroupSendPermission(actor_user_id, group_id);
}

}  // namespace tinyimx::group
