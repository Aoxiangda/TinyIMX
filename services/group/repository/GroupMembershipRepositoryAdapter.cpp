#include "services/group/repository/GroupRepositoryAdapter.h"

#include "common/db/MySqlConnection.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/group/application/GroupEventFactory.h"
#include "services/group/application/GroupPermissionPolicy.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/GroupRepository.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx::group {
namespace {

constexpr const char* kJoinOperation = "join_group";
constexpr const char* kLeaveOperation = "leave_group";
constexpr const char* kInviteOperation = "invite_member";
constexpr const char* kKickOperation = "kick_member";
constexpr const char* kSetRoleOperation = "set_member_role";
constexpr const char* kSetMuteOperation = "set_member_mute";
constexpr const char* kTransferOperation = "transfer_ownership";

GroupApplicationStatus MapStorageStatus(tinyimx::GroupRepositoryStatus status) {
    switch (status) {
        case tinyimx::GroupRepositoryStatus::kSucceeded:
            return GroupApplicationStatus::kSucceeded;
        case tinyimx::GroupRepositoryStatus::kInvalidArgument:
            return GroupApplicationStatus::kInvalidArgument;
        case tinyimx::GroupRepositoryStatus::kNotFound:
            return GroupApplicationStatus::kNotFound;
        case tinyimx::GroupRepositoryStatus::kInvalidRecord:
            return GroupApplicationStatus::kInvalidRecord;
        case tinyimx::GroupRepositoryStatus::kStorageError:
            return GroupApplicationStatus::kStorageError;
    }
    return GroupApplicationStatus::kStorageError;
}

std::optional<GroupStatus> MapGroupStatus(std::uint32_t status) {
    if (status == 1) return GroupStatus::kActive;
    if (status == 2) return GroupStatus::kDisbanded;
    return std::nullopt;
}

std::optional<GroupJoinPolicy> MapJoinPolicy(std::uint32_t policy) {
    if (policy == 1) return GroupJoinPolicy::kInviteOnly;
    if (policy == 2) return GroupJoinPolicy::kOpen;
    return std::nullopt;
}

std::optional<GroupRole> MapRole(std::uint32_t role) {
    if (role == 1) return GroupRole::kOwner;
    if (role == 2) return GroupRole::kAdmin;
    if (role == 3) return GroupRole::kMember;
    return std::nullopt;
}

std::optional<GroupMemberStatus> MapMemberStatus(std::uint32_t status) {
    if (status == 1) return GroupMemberStatus::kActive;
    if (status == 2) return GroupMemberStatus::kLeft;
    if (status == 3) return GroupMemberStatus::kKicked;
    return std::nullopt;
}

std::optional<GroupView> ToGroupView(const tinyimx::GroupRecord& record) {
    const auto status = MapGroupStatus(record.status);
    const auto join_policy = MapJoinPolicy(record.join_policy);
    if (!status.has_value() || !join_policy.has_value()) {
        return std::nullopt;
    }
    GroupView view;
    view.group_id = record.group_id;
    view.name = record.name;
    view.description = record.description;
    view.avatar_url = record.avatar_url;
    view.owner_user_id = record.owner_user_id;
    view.status = *status;
    view.join_policy = *join_policy;
    view.max_members = record.max_members;
    view.version = record.version;
    view.member_version = record.member_version;
    view.created_at = record.created_at;
    view.updated_at = record.updated_at;
    view.disbanded_at = record.disbanded_at;
    view.disbanded_by_user_id = record.disbanded_by_user_id;
    return view;
}

std::optional<GroupMemberView> ToMemberView(const tinyimx::GroupMemberRecord& record) {
    const auto role = MapRole(record.role);
    const auto status = MapMemberStatus(record.status);
    if (!role.has_value() || !status.has_value()) {
        return std::nullopt;
    }
    GroupMemberView view;
    view.group_id = record.group_id;
    view.user_id = record.user_id;
    view.role = *role;
    view.status = *status;
    view.membership_epoch = record.membership_epoch;
    view.muted_until = record.muted_until;
    view.joined_at = record.joined_at;
    view.left_at = record.left_at;
    view.updated_at = record.updated_at;
    return view;
}

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &digest_size, EVP_sha256(), nullptr) != 1 ||
        digest_size == 0) {
        return {};
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_size; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

std::string Fingerprint(const JoinGroupCommand& command) {
    return Sha256Hex("join_group|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id));
}
std::string Fingerprint(const LeaveGroupCommand& command) {
    return Sha256Hex("leave_group|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id));
}
std::string Fingerprint(const InviteMemberCommand& command) {
    return Sha256Hex("invite_member|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id) + "|" + std::to_string(command.target_user_id));
}
std::string Fingerprint(const KickMemberCommand& command) {
    return Sha256Hex("kick_member|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id) + "|" + std::to_string(command.target_user_id));
}
std::string Fingerprint(const SetMemberRoleCommand& command) {
    return Sha256Hex("set_member_role|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id) + "|" + std::to_string(command.target_user_id) +
                     "|" + std::to_string(static_cast<std::uint32_t>(command.role)));
}
std::string Fingerprint(const SetMemberMuteCommand& command) {
    return Sha256Hex("set_member_mute|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id) + "|" + std::to_string(command.target_user_id) +
                     "|" + std::to_string(command.muted_until.size()) + ":" + command.muted_until);
}
std::string Fingerprint(const TransferOwnershipCommand& command) {
    return Sha256Hex("transfer_ownership|" + std::to_string(command.actor_user_id) + "|" +
                     std::to_string(command.group_id) + "|" + std::to_string(command.target_user_id));
}

GroupRepositoryMutationResult Failure(GroupApplicationStatus status, std::string message) {
    GroupRepositoryMutationResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

void Rollback(tinyimx::MySqlConnection* connection) {
    if (connection != nullptr && connection->InTransaction()) {
        connection->Rollback();
    }
}

bool CommitReadOnly(tinyimx::MySqlConnection* connection) {
    if (connection == nullptr) {
        return false;
    }
    if (connection->Commit()) {
        return true;
    }
    Rollback(connection);
    return false;
}

GroupRepositoryMutationResult ResolveExistingOperation(
    tinyimx::GroupRepository* repository,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    const std::string& operation_type,
    const std::string& fingerprint
) {
    if (repository == nullptr) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository is unavailable");
    }
    const auto existing = repository->FindOperation(actor_user_id, client_operation_id);
    if (!existing.Succeeded()) {
        return Failure(MapStorageStatus(existing.status), existing.message);
    }
    if (!existing.found) {
        return Failure(GroupApplicationStatus::kNotFound, "group operation not found");
    }

    GroupRepositoryMutationResult result;
    result.status = GroupApplicationStatus::kSucceeded;
    if (existing.record.operation_type != operation_type ||
        existing.record.request_fingerprint != fingerprint) {
        result.outcome = GroupMutationOutcome::kIdempotencyConflict;
        result.message = "client_operation_id was already used for a different group mutation";
        return result;
    }

    const auto group = repository->FindGroupById(existing.record.group_id);
    if (!group.Succeeded()) {
        return Failure(MapStorageStatus(group.status), group.message);
    }
    if (!group.found) {
        return Failure(GroupApplicationStatus::kInvalidRecord, "group operation points to missing group");
    }
    const auto view = ToGroupView(group.record);
    if (!view.has_value()) {
        return Failure(GroupApplicationStatus::kInvalidRecord, "group operation points to invalid group");
    }
    result.outcome = GroupMutationOutcome::kReused;
    result.group = *view;
    result.message = "group mutation safely reused from durable operation record";
    return result;
}

GroupRepositoryMutationResult PrecheckOperation(
    tinyimx::GroupRepository* repository,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    const std::string& operation_type,
    const std::string& fingerprint,
    bool* found
) {
    *found = false;
    const auto existing = repository->FindOperation(actor_user_id, client_operation_id);
    if (!existing.Succeeded()) {
        return Failure(MapStorageStatus(existing.status), existing.message);
    }
    if (!existing.found) {
        GroupRepositoryMutationResult result;
        result.status = GroupApplicationStatus::kSucceeded;
        return result;
    }
    *found = true;
    return ResolveExistingOperation(repository, actor_user_id, client_operation_id, operation_type, fingerprint);
}

bool ValidNormalizedMuteTimestamp(const std::string& value) {
    if (value.empty()) {
        return true;
    }
    if (value.size() != 23 || value[4] != '-' || value[7] != '-' ||
        value[10] != ' ' || value[13] != ':' || value[16] != ':' || value[19] != '.') {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7 || i == 10 || i == 13 || i == 16 || i == 19) {
            continue;
        }
        if (value[i] < '0' || value[i] > '9') {
            return false;
        }
    }
    return true;
}

bool ValidDependencies(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnectionPool* pool,
    tinyimx::outbox::OutboxRepository* outbox,
    const GroupPermissionPolicy* permission
) {
    return repository != nullptr && pool != nullptr && outbox != nullptr && permission != nullptr;
}

struct LockedMembers {
    GroupApplicationStatus status{GroupApplicationStatus::kSucceeded};
    std::optional<GroupMemberView> actor;
    std::optional<GroupMemberView> target;
    std::string message;
};

LockedMembers LockActorAndTarget(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t actor_user_id,
    std::uint64_t target_user_id
) {
    LockedMembers output;
    const std::uint64_t first = std::min(actor_user_id, target_user_id);
    const std::uint64_t second = std::max(actor_user_id, target_user_id);

    const auto first_result = repository->FindMemberOnConnection(connection, group_id, first, true);
    if (!first_result.Succeeded()) {
        output.status = MapStorageStatus(first_result.status);
        output.message = first_result.message;
        return output;
    }
    const auto second_result = repository->FindMemberOnConnection(connection, group_id, second, true);
    if (!second_result.Succeeded()) {
        output.status = MapStorageStatus(second_result.status);
        output.message = second_result.message;
        return output;
    }

    auto convert = [](const tinyimx::GroupMemberFindResult& source) -> std::optional<GroupMemberView> {
        return source.found ? ToMemberView(source.record) : std::optional<GroupMemberView>{};
    };
    const auto first_view = convert(first_result);
    const auto second_view = convert(second_result);
    if (first_result.found && !first_view.has_value()) {
        output.status = GroupApplicationStatus::kInvalidRecord;
        output.message = "invalid first locked membership record";
        return output;
    }
    if (second_result.found && !second_view.has_value()) {
        output.status = GroupApplicationStatus::kInvalidRecord;
        output.message = "invalid second locked membership record";
        return output;
    }

    if (actor_user_id == first) {
        output.actor = first_view;
        output.target = second_view;
    } else {
        output.actor = second_view;
        output.target = first_view;
    }
    return output;
}

GroupRepositoryMutationResult FinalizeMutation(
    tinyimx::MySqlConnectionLease* lease,
    tinyimx::GroupRepository* repository,
    tinyimx::outbox::OutboxRepository* outbox,
    const GroupOutboxEventSpec& event_spec,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    const std::string& operation_type,
    const std::string& fingerprint,
    const GroupView& group,
    std::uint64_t membership_epoch,
    const std::string& success_message
) {
    auto* connection = lease->operator->();
    const auto outbox_result = outbox->InsertOnConnection(
        connection, event_spec.event, event_spec.topic, event_spec.tag, event_spec.message_key
    );
    if (!outbox_result.success) {
        Rollback(connection);
        return Failure(GroupApplicationStatus::kStorageError,
                       "group membership outbox insert failed: " + outbox_result.message);
    }

    tinyimx::GroupOperationRecord operation;
    operation.actor_user_id = actor_user_id;
    operation.client_operation_id = client_operation_id;
    operation.operation_type = operation_type;
    operation.request_fingerprint = fingerprint;
    operation.group_id = group.group_id;
    operation.result_version = group.version;
    operation.result_member_version = group.member_version;
    operation.result_membership_epoch = membership_epoch;

    const auto op_result = repository->InsertOperationOnConnection(connection, operation);
    if (!op_result.Succeeded()) {
        Rollback(connection);
        lease->Reset();
        const auto recovered = ResolveExistingOperation(
            repository, actor_user_id, client_operation_id, operation_type, fingerprint
        );
        if (recovered.status == GroupApplicationStatus::kSucceeded) {
            return recovered;
        }
        return Failure(MapStorageStatus(op_result.status), op_result.message);
    }

    if (connection->Commit()) {
        GroupRepositoryMutationResult result;
        result.status = GroupApplicationStatus::kSucceeded;
        result.outcome = GroupMutationOutcome::kApplied;
        result.group = group;
        result.message = success_message;
        return result;
    }

    const std::string commit_error = connection->LastError();
    Rollback(connection);
    lease->Reset();
    auto recovered = ResolveExistingOperation(
        repository, actor_user_id, client_operation_id, operation_type, fingerprint
    );
    if (recovered.status == GroupApplicationStatus::kSucceeded) {
        if (recovered.outcome == GroupMutationOutcome::kReused) {
            recovered.message = "ambiguous membership commit recovered from durable operation record";
        }
        return recovered;
    }
    return Failure(
        GroupApplicationStatus::kStorageError,
        commit_error.empty() ? "group membership transaction commit failed"
                             : "group membership transaction commit failed: " + commit_error
    );
}

GroupRepositoryMutationResult CheckConcurrentOperation(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnectionLease* lease,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    const std::string& operation_type,
    const std::string& fingerprint,
    bool* reused
) {
    *reused = false;
    const auto concurrent = repository->FindOperationOnConnection(
        lease->operator->(), actor_user_id, client_operation_id, false
    );
    if (!concurrent.Succeeded()) {
        Rollback(lease->operator->());
        return Failure(MapStorageStatus(concurrent.status), concurrent.message);
    }
    if (!concurrent.found) {
        GroupRepositoryMutationResult result;
        result.status = GroupApplicationStatus::kSucceeded;
        return result;
    }
    Rollback(lease->operator->());
    lease->Reset();
    *reused = true;
    return ResolveExistingOperation(repository, actor_user_id, client_operation_id, operation_type, fingerprint);
}

std::optional<GroupView> ReloadGroup(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnection* connection,
    std::uint64_t group_id,
    GroupRepositoryMutationResult* error
) {
    const auto refreshed = repository->FindGroupByIdOnConnection(connection, group_id, false);
    if (!refreshed.Succeeded() || !refreshed.found) {
        *error = Failure(
            refreshed.Succeeded() ? GroupApplicationStatus::kInvalidRecord : MapStorageStatus(refreshed.status),
            refreshed.Succeeded() ? "post-mutation group verification failed" : refreshed.message
        );
        return std::nullopt;
    }
    const auto view = ToGroupView(refreshed.record);
    if (!view.has_value()) {
        *error = Failure(GroupApplicationStatus::kInvalidRecord, "post-mutation group record is invalid");
        return std::nullopt;
    }
    return view;
}

std::optional<GroupMemberView> ReloadMember(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnection* connection,
    std::uint64_t group_id,
    std::uint64_t user_id,
    GroupRepositoryMutationResult* error
) {
    const auto refreshed = repository->FindMemberOnConnection(connection, group_id, user_id, false);
    if (!refreshed.Succeeded() || !refreshed.found) {
        *error = Failure(
            refreshed.Succeeded() ? GroupApplicationStatus::kInvalidRecord : MapStorageStatus(refreshed.status),
            refreshed.Succeeded() ? "post-mutation membership verification failed" : refreshed.message
        );
        return std::nullopt;
    }
    const auto view = ToMemberView(refreshed.record);
    if (!view.has_value()) {
        *error = Failure(GroupApplicationStatus::kInvalidRecord, "post-mutation membership record is invalid");
        return std::nullopt;
    }
    return view;
}

GroupRepositoryMutationResult EnsureCapacity(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnection* connection,
    const GroupView& group
) {
    const auto count = repository->CountActiveMembersOnConnection(connection, group.group_id);
    if (!count.Succeeded()) {
        return Failure(MapStorageStatus(count.status), count.message);
    }
    if (count.value >= group.max_members) {
        return Failure(GroupApplicationStatus::kResourceExhausted, "group member capacity reached");
    }
    GroupRepositoryMutationResult result;
    result.status = GroupApplicationStatus::kSucceeded;
    return result;
}

}  // namespace

GroupRepositoryMutationResult GroupRepositoryAdapter::JoinGroup(const JoinGroupCommand& command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.client_operation_id.empty()) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid JoinGroup repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kJoinOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "JoinGroup failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kJoinOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;

    const auto group = ToGroupView(locked.record);
    if (!group.has_value()) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kInvalidRecord, "invalid locked group record");
    }
    if (group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "group is not active");
    }
    if (group->join_policy != GroupJoinPolicy::kOpen) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "group does not allow open join");
    }

    const auto user_exists = repository_->UserExistsOnConnection(connection.operator->(), command.actor_user_id);
    if (!user_exists.Succeeded() || !user_exists.value) {
        Rollback(connection.operator->());
        return Failure(user_exists.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(user_exists.status),
                       user_exists.Succeeded() ? "joining user not found" : user_exists.message);
    }

    const auto member = repository_->FindMemberOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, true
    );
    if (!member.Succeeded()) {
        Rollback(connection.operator->());
        return Failure(MapStorageStatus(member.status), member.message);
    }
    std::uint64_t epoch = 1;
    if (member.found) {
        const auto view = ToMemberView(member.record);
        if (!view.has_value()) {
            Rollback(connection.operator->());
            return Failure(GroupApplicationStatus::kInvalidRecord, "invalid existing membership record");
        }
        if (view->status == GroupMemberStatus::kActive) {
            Rollback(connection.operator->());
            return Failure(GroupApplicationStatus::kAlreadyExists, "user is already an active group member");
        }
        if (view->status == GroupMemberStatus::kKicked) {
            Rollback(connection.operator->());
            return Failure(GroupApplicationStatus::kFailedPrecondition,
                           "kicked member must be invited by an administrator");
        }
        epoch = view->membership_epoch + 1;
    }

    auto capacity = EnsureCapacity(repository_, connection.operator->(), *group);
    if (capacity.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return capacity;
    }
    const auto mutate = member.found
        ? repository_->ReactivateMemberOnConnection(connection.operator->(), command.group_id,
                                                     command.actor_user_id, epoch, 3)
        : repository_->InsertMemberOnConnection(connection.operator->(), command.group_id,
                                                 command.actor_user_id, 3, epoch);
    if (!mutate.Succeeded() || mutate.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(mutate.Succeeded() ? GroupApplicationStatus::kAborted : MapStorageStatus(mutate.status),
                       mutate.Succeeded() ? "JoinGroup lost membership transition precondition" : mutate.message);
    }
    const auto history = repository_->InsertMembershipHistoryOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, epoch, 3
    );
    if (!history.Succeeded() || history.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(history.Succeeded() ? GroupApplicationStatus::kAborted : MapStorageStatus(history.status),
                       history.Succeeded() ? "JoinGroup failed to create membership history" : history.message);
    }
    const auto version = repository_->UpdateGroupVersionsOnConnection(
        connection.operator->(), command.group_id, group->version + 1, group->member_version + 1
    );
    if (!version.Succeeded() || version.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(version.Succeeded() ? GroupApplicationStatus::kAborted : MapStorageStatus(version.status),
                       version.Succeeded() ? "JoinGroup lost active-group version precondition" : version.message);
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto updated_member = ReloadMember(repository_, connection.operator->(), command.group_id,
                                             command.actor_user_id, &error);
    if (!updated_group.has_value() || !updated_member.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::MemberJoined(*updated_group, *updated_member,
                                                            command.actor_user_id, false),
                            command.actor_user_id, command.client_operation_id, kJoinOperation,
                            fingerprint, *updated_group, epoch,
                            "group join, outbox event, and idempotency record committed");
}

GroupRepositoryMutationResult GroupRepositoryAdapter::LeaveGroup(const LeaveGroupCommand& command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.client_operation_id.empty()) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid LeaveGroup repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kLeaveOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "LeaveGroup failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kLeaveOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;

    const auto group = ToGroupView(locked.record);
    if (!group.has_value() || group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(group.has_value() ? GroupApplicationStatus::kFailedPrecondition
                                         : GroupApplicationStatus::kInvalidRecord,
                       group.has_value() ? "group is not active" : "invalid locked group record");
    }
    const auto member = repository_->FindMemberOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, true
    );
    if (!member.Succeeded()) {
        Rollback(connection.operator->());
        return Failure(MapStorageStatus(member.status), member.message);
    }
    const auto actor = member.found ? ToMemberView(member.record) : std::optional<GroupMemberView>{};
    if (!actor.has_value() || actor->status != GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "actor is not an active group member");
    }
    if (!permission_->CanLeave(actor->role)) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition,
                       "group owner must transfer ownership or disband before leaving");
    }

    const auto inactive = repository_->MarkMemberInactiveOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, 2
    );
    const auto history = repository_->CloseMembershipHistoryOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, actor->membership_epoch, 1,
        command.actor_user_id
    );
    const auto version = repository_->UpdateGroupVersionsOnConnection(
        connection.operator->(), command.group_id, group->version + 1, group->member_version + 1
    );
    if (!inactive.Succeeded() || inactive.affected_rows != 1 ||
        !history.Succeeded() || history.affected_rows != 1 ||
        !version.Succeeded() || version.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kStorageError, "LeaveGroup durable transition failed");
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto updated_member = ReloadMember(repository_, connection.operator->(), command.group_id,
                                             command.actor_user_id, &error);
    if (!updated_group.has_value() || !updated_member.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::MemberLeft(*updated_group, *updated_member,
                                                          command.actor_user_id),
                            command.actor_user_id, command.client_operation_id, kLeaveOperation,
                            fingerprint, *updated_group, actor->membership_epoch,
                            "group leave, outbox event, and idempotency record committed");
}

GroupRepositoryMutationResult GroupRepositoryAdapter::InviteMember(const InviteMemberCommand& command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.target_user_id == 0 ||
        command.actor_user_id == command.target_user_id || command.client_operation_id.empty()) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid InviteMember repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kInviteOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "InviteMember failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kInviteOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;

    const auto group = ToGroupView(locked.record);
    if (!group.has_value() || group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(group.has_value() ? GroupApplicationStatus::kFailedPrecondition
                                         : GroupApplicationStatus::kInvalidRecord,
                       group.has_value() ? "group is not active" : "invalid locked group record");
    }
    const auto user_exists = repository_->UserExistsOnConnection(connection.operator->(), command.target_user_id);
    if (!user_exists.Succeeded() || !user_exists.value) {
        Rollback(connection.operator->());
        return Failure(user_exists.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(user_exists.status),
                       user_exists.Succeeded() ? "invited user not found" : user_exists.message);
    }

    const auto pair = LockActorAndTarget(repository_, connection.operator->(), command.group_id,
                                         command.actor_user_id, command.target_user_id);
    if (pair.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return Failure(pair.status, pair.message);
    }
    if (!pair.actor.has_value() || pair.actor->status != GroupMemberStatus::kActive ||
        !permission_->CanInvite(pair.actor->role)) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "actor cannot invite group members");
    }
    if (pair.target.has_value() && pair.target->status == GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kAlreadyExists, "target is already an active group member");
    }

    auto capacity = EnsureCapacity(repository_, connection.operator->(), *group);
    if (capacity.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return capacity;
    }
    const std::uint64_t epoch = pair.target.has_value() ? pair.target->membership_epoch + 1 : 1;
    const auto mutate = pair.target.has_value()
        ? repository_->ReactivateMemberOnConnection(connection.operator->(), command.group_id,
                                                     command.target_user_id, epoch, 3)
        : repository_->InsertMemberOnConnection(connection.operator->(), command.group_id,
                                                 command.target_user_id, 3, epoch);
    const auto history = mutate.Succeeded() && mutate.affected_rows == 1
        ? repository_->InsertMembershipHistoryOnConnection(connection.operator->(), command.group_id,
                                                            command.target_user_id, epoch, 3)
        : tinyimx::GroupMutationStorageResult{};
    const auto version = history.Succeeded() && history.affected_rows == 1
        ? repository_->UpdateGroupVersionsOnConnection(connection.operator->(), command.group_id,
                                                        group->version + 1, group->member_version + 1)
        : tinyimx::GroupMutationStorageResult{};
    if (!mutate.Succeeded() || mutate.affected_rows != 1 ||
        !history.Succeeded() || history.affected_rows != 1 ||
        !version.Succeeded() || version.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kStorageError, "InviteMember durable transition failed");
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto updated_member = ReloadMember(repository_, connection.operator->(), command.group_id,
                                             command.target_user_id, &error);
    if (!updated_group.has_value() || !updated_member.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::MemberJoined(*updated_group, *updated_member,
                                                            command.actor_user_id, true),
                            command.actor_user_id, command.client_operation_id, kInviteOperation,
                            fingerprint, *updated_group, epoch,
                            "member invite, outbox event, and idempotency record committed");
}

GroupRepositoryMutationResult GroupRepositoryAdapter::KickMember(const KickMemberCommand& command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.target_user_id == 0 ||
        command.actor_user_id == command.target_user_id || command.client_operation_id.empty()) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid KickMember repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kKickOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "KickMember failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kKickOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;
    const auto group = ToGroupView(locked.record);
    if (!group.has_value() || group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(group.has_value() ? GroupApplicationStatus::kFailedPrecondition
                                         : GroupApplicationStatus::kInvalidRecord,
                       group.has_value() ? "group is not active" : "invalid locked group record");
    }

    const auto pair = LockActorAndTarget(repository_, connection.operator->(), command.group_id,
                                         command.actor_user_id, command.target_user_id);
    if (pair.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return Failure(pair.status, pair.message);
    }
    if (!pair.actor.has_value() || pair.actor->status != GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "actor is not an active group member");
    }
    if (!pair.target.has_value() || pair.target->status != GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "target is not an active group member");
    }
    if (!permission_->CanKick(pair.actor->role, pair.target->role)) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "actor cannot kick target member");
    }

    const auto inactive = repository_->MarkMemberInactiveOnConnection(
        connection.operator->(), command.group_id, command.target_user_id, 3
    );
    const auto history = repository_->CloseMembershipHistoryOnConnection(
        connection.operator->(), command.group_id, command.target_user_id,
        pair.target->membership_epoch, 2, command.actor_user_id
    );
    const auto version = repository_->UpdateGroupVersionsOnConnection(
        connection.operator->(), command.group_id, group->version + 1, group->member_version + 1
    );
    if (!inactive.Succeeded() || inactive.affected_rows != 1 ||
        !history.Succeeded() || history.affected_rows != 1 ||
        !version.Succeeded() || version.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kStorageError, "KickMember durable transition failed");
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto updated_member = ReloadMember(repository_, connection.operator->(), command.group_id,
                                             command.target_user_id, &error);
    if (!updated_group.has_value() || !updated_member.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::MemberKicked(*updated_group, *updated_member,
                                                            command.actor_user_id),
                            command.actor_user_id, command.client_operation_id, kKickOperation,
                            fingerprint, *updated_group, pair.target->membership_epoch,
                            "member kick, outbox event, and idempotency record committed");
}

GroupRepositoryMutationResult GroupRepositoryAdapter::SetMemberRole(const SetMemberRoleCommand& command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.target_user_id == 0 ||
        command.actor_user_id == command.target_user_id || command.client_operation_id.empty() ||
        (command.role != GroupRole::kAdmin && command.role != GroupRole::kMember)) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid SetMemberRole repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kSetRoleOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "SetMemberRole failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kSetRoleOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;
    const auto group = ToGroupView(locked.record);
    if (!group.has_value() || group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(group.has_value() ? GroupApplicationStatus::kFailedPrecondition
                                         : GroupApplicationStatus::kInvalidRecord,
                       group.has_value() ? "group is not active" : "invalid locked group record");
    }
    const auto pair = LockActorAndTarget(repository_, connection.operator->(), command.group_id,
                                         command.actor_user_id, command.target_user_id);
    if (pair.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return Failure(pair.status, pair.message);
    }
    if (!pair.actor.has_value() || pair.actor->status != GroupMemberStatus::kActive ||
        !pair.target.has_value() || pair.target->status != GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "role mutation requires active actor and target");
    }
    if (!permission_->CanSetRole(pair.actor->role, pair.target->role)) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "actor cannot change target role");
    }
    if (pair.target->role == command.role) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "target already has requested role");
    }

    const auto role = repository_->UpdateMemberRoleOnConnection(
        connection.operator->(), command.group_id, command.target_user_id,
        static_cast<std::uint32_t>(command.role)
    );
    const auto version = repository_->UpdateGroupVersionsOnConnection(
        connection.operator->(), command.group_id, group->version + 1, group->member_version + 1
    );
    if (!role.Succeeded() || role.affected_rows != 1 ||
        !version.Succeeded() || version.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kStorageError, "SetMemberRole durable transition failed");
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto updated_member = ReloadMember(repository_, connection.operator->(), command.group_id,
                                             command.target_user_id, &error);
    if (!updated_group.has_value() || !updated_member.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::MemberRoleChanged(*updated_group, *updated_member,
                                                                 command.actor_user_id),
                            command.actor_user_id, command.client_operation_id, kSetRoleOperation,
                            fingerprint, *updated_group, pair.target->membership_epoch,
                            "member role change, outbox event, and idempotency record committed");
}

GroupRepositoryMutationResult GroupRepositoryAdapter::SetMemberMute(const SetMemberMuteCommand& command) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.target_user_id == 0 ||
        command.actor_user_id == command.target_user_id || command.client_operation_id.empty() ||
        !ValidNormalizedMuteTimestamp(command.muted_until)) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid SetMemberMute repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kSetMuteOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "SetMemberMute failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kSetMuteOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;
    const auto group = ToGroupView(locked.record);
    if (!group.has_value() || group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(group.has_value() ? GroupApplicationStatus::kFailedPrecondition
                                         : GroupApplicationStatus::kInvalidRecord,
                       group.has_value() ? "group is not active" : "invalid locked group record");
    }
    const auto pair = LockActorAndTarget(repository_, connection.operator->(), command.group_id,
                                         command.actor_user_id, command.target_user_id);
    if (pair.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return Failure(pair.status, pair.message);
    }
    if (!pair.actor.has_value() || pair.actor->status != GroupMemberStatus::kActive ||
        !pair.target.has_value() || pair.target->status != GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "mute mutation requires active actor and target");
    }
    if (!permission_->CanMute(pair.actor->role, pair.target->role)) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "actor cannot mute target member");
    }
    if (pair.target->muted_until == command.muted_until) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition, "target already has requested mute state");
    }

    const auto mute = repository_->UpdateMemberMuteOnConnection(
        connection.operator->(), command.group_id, command.target_user_id, command.muted_until
    );
    const auto version = repository_->UpdateGroupVersionsOnConnection(
        connection.operator->(), command.group_id, group->version + 1, group->member_version + 1
    );
    if (!mute.Succeeded() || mute.affected_rows != 1 ||
        !version.Succeeded() || version.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kStorageError, "SetMemberMute durable transition failed");
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto updated_member = ReloadMember(repository_, connection.operator->(), command.group_id,
                                             command.target_user_id, &error);
    if (!updated_group.has_value() || !updated_member.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::MemberMuteChanged(*updated_group, *updated_member,
                                                                 command.actor_user_id),
                            command.actor_user_id, command.client_operation_id, kSetMuteOperation,
                            fingerprint, *updated_group, pair.target->membership_epoch,
                            "member mute change, outbox event, and idempotency record committed");
}

GroupRepositoryMutationResult GroupRepositoryAdapter::TransferOwnership(
    const TransferOwnershipCommand& command
) {
    if (command.actor_user_id == 0 || command.group_id == 0 || command.target_user_id == 0 ||
        command.actor_user_id == command.target_user_id || command.client_operation_id.empty()) {
        return Failure(GroupApplicationStatus::kInvalidArgument, "invalid TransferOwnership repository command");
    }
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return Failure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    bool found = false;
    auto precheck = PrecheckOperation(repository_, command.actor_user_id, command.client_operation_id,
                                      kTransferOperation, fingerprint, &found);
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) return precheck;

    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        return Failure(GroupApplicationStatus::kStorageError, "TransferOwnership failed to begin database transaction");
    }
    const auto locked = repository_->FindGroupByIdOnConnection(connection.operator->(), command.group_id, true);
    if (!locked.Succeeded() || !locked.found) {
        Rollback(connection.operator->());
        return Failure(locked.Succeeded() ? GroupApplicationStatus::kNotFound : MapStorageStatus(locked.status),
                       locked.Succeeded() ? "group not found" : locked.message);
    }
    bool reused = false;
    auto concurrent = CheckConcurrentOperation(repository_, &connection, command.actor_user_id,
                                               command.client_operation_id, kTransferOperation,
                                               fingerprint, &reused);
    if (reused || concurrent.status != GroupApplicationStatus::kSucceeded) return concurrent;
    const auto group = ToGroupView(locked.record);
    if (!group.has_value() || group->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(group.has_value() ? GroupApplicationStatus::kFailedPrecondition
                                         : GroupApplicationStatus::kInvalidRecord,
                       group.has_value() ? "group is not active" : "invalid locked group record");
    }
    const auto pair = LockActorAndTarget(repository_, connection.operator->(), command.group_id,
                                         command.actor_user_id, command.target_user_id);
    if (pair.status != GroupApplicationStatus::kSucceeded) {
        Rollback(connection.operator->());
        return Failure(pair.status, pair.message);
    }
    if (!pair.actor.has_value() || pair.actor->status != GroupMemberStatus::kActive ||
        !pair.target.has_value() || pair.target->status != GroupMemberStatus::kActive) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kFailedPrecondition,
                       "ownership transfer requires active owner and target");
    }
    if (group->owner_user_id != command.actor_user_id || pair.actor->role != GroupRole::kOwner ||
        !permission_->CanTransferOwnership(pair.actor->role, pair.target->role)) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kPermissionDenied, "only active owner can transfer ownership");
    }

    const auto group_update = repository_->UpdateGroupOwnerAndVersionsOnConnection(
        connection.operator->(), command.group_id, command.target_user_id,
        group->version + 1, group->member_version + 1
    );
    const auto old_owner_role = repository_->UpdateMemberRoleOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, 2
    );
    const auto new_owner_role = repository_->UpdateMemberRoleOnConnection(
        connection.operator->(), command.group_id, command.target_user_id, 1
    );
    if (!group_update.Succeeded() || group_update.affected_rows != 1 ||
        !old_owner_role.Succeeded() || old_owner_role.affected_rows != 1 ||
        !new_owner_role.Succeeded() || new_owner_role.affected_rows != 1) {
        Rollback(connection.operator->());
        return Failure(GroupApplicationStatus::kStorageError, "TransferOwnership durable transition failed");
    }

    GroupRepositoryMutationResult error;
    const auto updated_group = ReloadGroup(repository_, connection.operator->(), command.group_id, &error);
    const auto new_owner = ReloadMember(repository_, connection.operator->(), command.group_id,
                                        command.target_user_id, &error);
    if (!updated_group.has_value() || !new_owner.has_value()) {
        Rollback(connection.operator->());
        return error;
    }
    return FinalizeMutation(&connection, repository_, outbox_,
                            GroupEventFactory::OwnershipTransferred(*updated_group, *new_owner,
                                                                    command.actor_user_id),
                            command.actor_user_id, command.client_operation_id, kTransferOperation,
                            fingerprint, *updated_group, new_owner->membership_epoch,
                            "ownership transfer, outbox event, and idempotency record committed");
}

GroupMemberListResult GroupRepositoryAdapter::ListGroupMembers(
    const ListGroupMembersQuery& query
) {
    GroupMemberListResult result;
    if (repository_ == nullptr || pool_ == nullptr || permission_ == nullptr) {
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "group repository adapter dependencies are unavailable";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "ListGroupMembers failed to begin consistent-read transaction";
        return result;
    }
    const auto group = repository_->FindGroupByIdOnConnection(connection.operator->(), query.group_id, false);
    const auto actor = repository_->FindMemberOnConnection(
        connection.operator->(), query.group_id, query.actor_user_id, false
    );
    if (!group.Succeeded() || !actor.Succeeded()) {
        Rollback(connection.operator->());
        result.status = !group.Succeeded() ? MapStorageStatus(group.status) : MapStorageStatus(actor.status);
        result.message = !group.Succeeded() ? group.message : actor.message;
        return result;
    }
    if (!group.found) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kNotFound;
        result.message = "group not found";
        return result;
    }
    const auto group_view = ToGroupView(group.record);
    const auto actor_view = actor.found ? ToMemberView(actor.record) : std::optional<GroupMemberView>{};
    if (!group_view.has_value()) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kInvalidRecord;
        result.message = "invalid group record";
        return result;
    }
    if (group_view->status != GroupStatus::kActive) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kFailedPrecondition;
        result.message = "group is not active";
        return result;
    }
    if (!actor_view.has_value() || !permission_->CanView(*actor_view)) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kPermissionDenied;
        result.message = "only active members can list group members";
        return result;
    }

    const auto listed = repository_->ListActiveMembersOnConnection(
        connection.operator->(), query.group_id, query.after_user_id, query.limit
    );
    if (!listed.Succeeded()) {
        Rollback(connection.operator->());
        result.status = MapStorageStatus(listed.status);
        result.message = listed.message;
        return result;
    }
    for (const auto& record : listed.records) {
        auto view = ToMemberView(record);
        if (!view.has_value()) {
            Rollback(connection.operator->());
            result.status = GroupApplicationStatus::kInvalidRecord;
            result.message = "invalid membership row in member list";
            return result;
        }
        result.members.push_back(std::move(*view));
    }
    if (!connection->Commit()) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "ListGroupMembers consistent-read commit failed";
        return result;
    }
    result.status = GroupApplicationStatus::kSucceeded;
    result.has_more = listed.has_more;
    result.message = "group members listed";
    return result;
}

GroupListResult GroupRepositoryAdapter::ListMyGroups(const ListMyGroupsQuery& query) {
    GroupListResult result;
    if (repository_ == nullptr) {
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "group repository is unavailable";
        return result;
    }
    const auto listed = repository_->ListActiveGroupsForUser(
        query.actor_user_id, query.after_group_id, query.limit
    );
    if (!listed.Succeeded()) {
        result.status = MapStorageStatus(listed.status);
        result.message = listed.message;
        return result;
    }
    for (const auto& record : listed.records) {
        auto view = ToGroupView(record);
        if (!view.has_value()) {
            result.status = GroupApplicationStatus::kInvalidRecord;
            result.message = "invalid group row in ListMyGroups";
            result.groups.clear();
            return result;
        }
        result.groups.push_back(std::move(*view));
    }
    result.status = GroupApplicationStatus::kSucceeded;
    result.has_more = listed.has_more;
    result.message = "active groups listed";
    return result;
}

GroupSendPermissionResult GroupRepositoryAdapter::CheckGroupSendPermission(
    std::uint64_t actor_user_id,
    std::uint64_t group_id
) {
    GroupSendPermissionResult result;
    if (repository_ == nullptr || pool_ == nullptr || permission_ == nullptr) {
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "group repository adapter dependencies are unavailable";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection || !connection->BeginTransaction()) {
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "CheckGroupSendPermission failed to begin consistent-read transaction";
        return result;
    }
    const auto group = repository_->FindGroupByIdOnConnection(connection.operator->(), group_id, false);
    if (!group.Succeeded()) {
        Rollback(connection.operator->());
        result.status = MapStorageStatus(group.status);
        result.message = group.message;
        return result;
    }
    if (!group.found) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kNotFound;
        result.message = "group not found";
        return result;
    }
    const auto group_view = ToGroupView(group.record);
    if (!group_view.has_value()) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kInvalidRecord;
        result.message = "invalid group record";
        return result;
    }
    result.member_version = group_view->member_version;
    if (group_view->status != GroupStatus::kActive) {
        if (!CommitReadOnly(connection.operator->())) {
            result.status = GroupApplicationStatus::kStorageError;
            result.message = "CheckGroupSendPermission read-only commit failed";
            return result;
        }
        result.status = GroupApplicationStatus::kSucceeded;
        result.allowed = false;
        result.message = "group is not active";
        return result;
    }

    const auto member = repository_->FindMemberOnConnection(connection.operator->(), group_id,
                                                             actor_user_id, false);
    if (!member.Succeeded()) {
        Rollback(connection.operator->());
        result.status = MapStorageStatus(member.status);
        result.message = member.message;
        return result;
    }
    if (!member.found) {
        if (!CommitReadOnly(connection.operator->())) {
            result.status = GroupApplicationStatus::kStorageError;
            result.message = "CheckGroupSendPermission read-only commit failed";
            return result;
        }
        result.status = GroupApplicationStatus::kSucceeded;
        result.allowed = false;
        result.message = "user is not a group member";
        return result;
    }
    const auto member_view = ToMemberView(member.record);
    if (!member_view.has_value()) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kInvalidRecord;
        result.message = "invalid membership record";
        return result;
    }
    result.role = member_view->role;
    result.membership_epoch = member_view->membership_epoch;
    if (member_view->status != GroupMemberStatus::kActive) {
        if (!CommitReadOnly(connection.operator->())) {
            result.status = GroupApplicationStatus::kStorageError;
            result.message = "CheckGroupSendPermission read-only commit failed";
            return result;
        }
        result.status = GroupApplicationStatus::kSucceeded;
        result.allowed = false;
        result.message = "membership is not active";
        return result;
    }

    const auto mute = repository_->IsMemberMuteActiveOnConnection(
        connection.operator->(), group_id, actor_user_id
    );
    if (!mute.Succeeded()) {
        Rollback(connection.operator->());
        result.status = MapStorageStatus(mute.status);
        result.message = mute.message;
        return result;
    }
    if (!connection->Commit()) {
        Rollback(connection.operator->());
        result.status = GroupApplicationStatus::kStorageError;
        result.message = "CheckGroupSendPermission consistent-read commit failed";
        return result;
    }
    result.status = GroupApplicationStatus::kSucceeded;
    result.allowed = permission_->CanSend(*member_view, mute.value);
    result.message = result.allowed ? "group send permission granted"
                                    : "group send permission denied by active mute";
    return result;
}

}  // namespace tinyimx::group
