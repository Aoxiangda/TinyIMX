#include "services/group/repository/GroupRepositoryAdapter.h"

#include "common/db/MySqlConnection.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/group/application/GroupEventFactory.h"
#include "services/group/application/GroupPermissionPolicy.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/GroupRepository.h"

#include <openssl/evp.h>

#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace tinyimx::group {
namespace {

constexpr const char* kCreateOperation = "create_group";
constexpr const char* kUpdateOperation = "update_group";
constexpr const char* kDisbandOperation = "disband_group";

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
    if (status == 1) {
        return GroupStatus::kActive;
    }
    if (status == 2) {
        return GroupStatus::kDisbanded;
    }
    return std::nullopt;
}

std::optional<GroupJoinPolicy> MapJoinPolicy(std::uint32_t policy) {
    if (policy == 1) {
        return GroupJoinPolicy::kInviteOnly;
    }
    if (policy == 2) {
        return GroupJoinPolicy::kOpen;
    }
    return std::nullopt;
}

std::optional<GroupRole> MapRole(std::uint32_t role) {
    if (role == 1) {
        return GroupRole::kOwner;
    }
    if (role == 2) {
        return GroupRole::kAdmin;
    }
    if (role == 3) {
        return GroupRole::kMember;
    }
    return std::nullopt;
}

std::optional<GroupMemberStatus> MapMemberStatus(std::uint32_t status) {
    if (status == 1) {
        return GroupMemberStatus::kActive;
    }
    if (status == 2) {
        return GroupMemberStatus::kLeft;
    }
    if (status == 3) {
        return GroupMemberStatus::kKicked;
    }
    return std::nullopt;
}

std::optional<GroupView> ToView(const tinyimx::GroupRecord& record) {
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

std::optional<GroupMemberView> ToView(const tinyimx::GroupMemberRecord& record) {
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

std::string AppendField(const std::string& value) {
    return std::to_string(value.size()) + ":" + value + "|";
}

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            input.data(), input.size(), digest, &digest_size,
            EVP_sha256(), nullptr
        ) != 1 || digest_size == 0) {
        return {};
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_size; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

std::string Fingerprint(const CreateGroupCommand& command) {
    std::string canonical = "create_group|";
    canonical += std::to_string(command.actor_user_id) + "|";
    canonical += AppendField(command.name);
    canonical += AppendField(command.description);
    canonical += AppendField(command.avatar_url);
    canonical += std::to_string(static_cast<std::uint32_t>(command.join_policy)) + "|";
    canonical += std::to_string(command.max_members);
    return Sha256Hex(canonical);
}

std::string Fingerprint(const UpdateGroupCommand& command) {
    std::string canonical = "update_group|" + std::to_string(command.actor_user_id) + "|" +
                            std::to_string(command.group_id) + "|" +
                            std::to_string(command.expected_version) + "|";
    canonical += command.name.has_value() ? "1" + AppendField(*command.name) : "0|";
    canonical += command.description.has_value() ? "1" + AppendField(*command.description) : "0|";
    canonical += command.avatar_url.has_value() ? "1" + AppendField(*command.avatar_url) : "0|";
    canonical += command.join_policy.has_value()
        ? "1|" + std::to_string(static_cast<std::uint32_t>(*command.join_policy))
        : "0|";
    return Sha256Hex(canonical);
}

std::string Fingerprint(const DisbandGroupCommand& command) {
    return Sha256Hex(
        "disband_group|" + std::to_string(command.actor_user_id) + "|" +
        std::to_string(command.group_id) + "|" + std::to_string(command.expected_version)
    );
}

GroupRepositoryMutationResult StorageFailure(
    GroupApplicationStatus status,
    std::string message
) {
    GroupRepositoryMutationResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

void RollbackIfNeeded(tinyimx::MySqlConnection* connection) {
    if (connection != nullptr && connection->InTransaction()) {
        connection->Rollback();
    }
}

GroupRepositoryMutationResult ResolveExistingOperation(
    tinyimx::GroupRepository* repository,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    const std::string& operation_type,
    const std::string& fingerprint
) {
    GroupRepositoryMutationResult output;
    if (repository == nullptr) {
        output.status = GroupApplicationStatus::kStorageError;
        output.message = "group repository is unavailable";
        return output;
    }

    const auto existing = repository->FindOperation(actor_user_id, client_operation_id);
    if (!existing.Succeeded()) {
        output.status = MapStorageStatus(existing.status);
        output.message = existing.message;
        return output;
    }
    if (!existing.found) {
        output.status = GroupApplicationStatus::kNotFound;
        output.message = "group operation not found";
        return output;
    }

    output.status = GroupApplicationStatus::kSucceeded;
    if (existing.record.operation_type != operation_type ||
        existing.record.request_fingerprint != fingerprint) {
        output.outcome = GroupMutationOutcome::kIdempotencyConflict;
        output.message = "client_operation_id was already used for a different group mutation";
        return output;
    }

    const auto group = repository->FindGroupById(existing.record.group_id);
    if (!group.Succeeded()) {
        output.status = MapStorageStatus(group.status);
        output.message = group.message;
        return output;
    }
    if (!group.found) {
        output.status = GroupApplicationStatus::kInvalidRecord;
        output.message = "group operation points to a missing group";
        return output;
    }
    const auto view = ToView(group.record);
    if (!view.has_value()) {
        output.status = GroupApplicationStatus::kInvalidRecord;
        output.message = "group operation points to an invalid group";
        return output;
    }

    output.outcome = GroupMutationOutcome::kReused;
    output.group = *view;
    output.message = "group mutation safely reused from durable operation record";
    return output;
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
    GroupRepositoryMutationResult output;
    const auto existing = repository->FindOperation(actor_user_id, client_operation_id);
    if (!existing.Succeeded()) {
        output.status = MapStorageStatus(existing.status);
        output.message = existing.message;
        return output;
    }
    if (!existing.found) {
        output.status = GroupApplicationStatus::kSucceeded;
        return output;
    }
    *found = true;
    return ResolveExistingOperation(
        repository, actor_user_id, client_operation_id, operation_type, fingerprint
    );
}

GroupRepositoryMutationResult CommitOrRecover(
    tinyimx::MySqlConnectionLease* lease,
    tinyimx::GroupRepository* repository,
    std::uint64_t actor_user_id,
    const std::string& client_operation_id,
    const std::string& operation_type,
    const std::string& fingerprint,
    const GroupView& applied_group,
    const std::string& success_message
) {
    GroupRepositoryMutationResult output;
    auto* connection = lease->operator->();
    if (connection->Commit()) {
        output.status = GroupApplicationStatus::kSucceeded;
        output.outcome = GroupMutationOutcome::kApplied;
        output.group = applied_group;
        output.message = success_message;
        return output;
    }

    const std::string commit_error = connection->LastError();
    RollbackIfNeeded(connection);
    lease->Reset();

    auto recovered = ResolveExistingOperation(
        repository, actor_user_id, client_operation_id, operation_type, fingerprint
    );
    if (recovered.status == GroupApplicationStatus::kSucceeded) {
        if (recovered.outcome == GroupMutationOutcome::kReused) {
            recovered.message = "ambiguous commit recovered from durable group operation record";
        }
        return recovered;
    }

    output.status = GroupApplicationStatus::kStorageError;
    output.message = "group transaction commit failed";
    if (!commit_error.empty()) {
        output.message += ": " + commit_error;
    }
    return output;
}

bool ValidDependencies(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnectionPool* pool,
    tinyimx::outbox::OutboxRepository* outbox,
    const GroupPermissionPolicy* permission
) {
    return repository != nullptr && pool != nullptr && outbox != nullptr && permission != nullptr;
}

}  // namespace

GroupRepositoryAdapter::GroupRepositoryAdapter(
    tinyimx::GroupRepository* repository,
    tinyimx::MySqlConnectionPool* pool,
    tinyimx::outbox::OutboxRepository* outbox_repository,
    const GroupPermissionPolicy* permission_policy
)
    : repository_(repository),
      pool_(pool),
      outbox_(outbox_repository),
      permission_(permission_policy) {
}

GroupRepositoryMutationResult GroupRepositoryAdapter::CreateGroup(
    const CreateGroupCommand& command
) {
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }

    const std::string fingerprint = Fingerprint(command);
    if (fingerprint.size() != 64) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "failed to fingerprint CreateGroup request");
    }

    bool found = false;
    auto precheck = PrecheckOperation(
        repository_, command.actor_user_id, command.client_operation_id,
        kCreateOperation, fingerprint, &found
    );
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) {
        return precheck;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "CreateGroup failed to acquire database connection");
    }
    if (!connection->BeginTransaction()) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "CreateGroup failed to begin transaction: " + connection->LastError());
    }

    const auto user_exists = repository_->UserExistsOnConnection(connection.operator->(), command.actor_user_id);
    if (!user_exists.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(user_exists.status), user_exists.message);
    }
    if (!user_exists.value) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kNotFound, "CreateGroup owner user does not exist");
    }

    const auto inserted = repository_->InsertGroupOnConnection(
        connection.operator->(), command.actor_user_id, command.name,
        command.description, command.avatar_url,
        static_cast<std::uint32_t>(command.join_policy), command.max_members
    );
    if (!inserted.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(inserted.status), inserted.message);
    }

    const auto owner_member = repository_->InsertOwnerMemberOnConnection(
        connection.operator->(), inserted.group_id, command.actor_user_id
    );
    if (!owner_member.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(owner_member.status), owner_member.message);
    }

    const auto history = repository_->InsertMembershipHistoryOnConnection(
        connection.operator->(), inserted.group_id, command.actor_user_id, 1, 1
    );
    if (!history.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(history.status), history.message);
    }

    const auto created = repository_->FindGroupByIdOnConnection(
        connection.operator->(), inserted.group_id, false
    );
    if (!created.Succeeded() || !created.found) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(
            created.Succeeded() ? GroupApplicationStatus::kInvalidRecord : MapStorageStatus(created.status),
            created.Succeeded() ? "CreateGroup post-insert verification failed" : created.message
        );
    }
    const auto view = ToView(created.record);
    if (!view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kInvalidRecord, "CreateGroup produced invalid group record");
    }

    const auto event_spec = GroupEventFactory::GroupCreated(*view);
    const auto outbox_result = outbox_->InsertOnConnection(
        connection.operator->(), event_spec.event, event_spec.topic,
        event_spec.tag, event_spec.message_key
    );
    if (!outbox_result.success) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kStorageError, "CreateGroup outbox insert failed: " + outbox_result.message);
    }

    tinyimx::GroupOperationRecord operation;
    operation.actor_user_id = command.actor_user_id;
    operation.client_operation_id = command.client_operation_id;
    operation.operation_type = kCreateOperation;
    operation.request_fingerprint = fingerprint;
    operation.group_id = view->group_id;
    operation.result_version = view->version;
    operation.result_member_version = view->member_version;
    operation.result_membership_epoch = 1;
    const auto op_result = repository_->InsertOperationOnConnection(connection.operator->(), operation);
    if (!op_result.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        auto recovered = ResolveExistingOperation(
            repository_, command.actor_user_id, command.client_operation_id,
            kCreateOperation, fingerprint
        );
        if (recovered.status == GroupApplicationStatus::kSucceeded) {
            return recovered;
        }
        return StorageFailure(MapStorageStatus(op_result.status), op_result.message);
    }

    return CommitOrRecover(
        &connection, repository_, command.actor_user_id, command.client_operation_id,
        kCreateOperation, fingerprint, *view,
        "group, owner membership, outbox event, and idempotency record committed"
    );
}

GroupRepositoryGetResult GroupRepositoryAdapter::GetGroup(
    std::uint64_t actor_user_id,
    std::uint64_t group_id
) {
    GroupRepositoryGetResult output;
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        output.status = GroupApplicationStatus::kStorageError;
        output.message = "group repository adapter dependencies are unavailable";
        return output;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        output.status = GroupApplicationStatus::kStorageError;
        output.message = "GetGroup failed to acquire database connection";
        return output;
    }

    const auto group = repository_->FindGroupByIdOnConnection(connection.operator->(), group_id, false);
    if (!group.Succeeded()) {
        output.status = MapStorageStatus(group.status);
        output.message = group.message;
        return output;
    }
    if (!group.found) {
        output.status = GroupApplicationStatus::kNotFound;
        output.message = "group not found";
        return output;
    }

    const auto member = repository_->FindMemberOnConnection(
        connection.operator->(), group_id, actor_user_id, false
    );
    if (!member.Succeeded()) {
        output.status = MapStorageStatus(member.status);
        output.message = member.message;
        return output;
    }
    if (!member.found) {
        output.status = GroupApplicationStatus::kPermissionDenied;
        output.message = "actor is not a group member";
        return output;
    }

    const auto member_view = ToView(member.record);
    const auto group_view = ToView(group.record);
    if (!member_view.has_value() || !group_view.has_value()) {
        output.status = GroupApplicationStatus::kInvalidRecord;
        output.message = "invalid group domain record";
        return output;
    }
    if (!permission_->CanView(*member_view)) {
        output.status = GroupApplicationStatus::kPermissionDenied;
        output.message = "actor has no permission to view group";
        return output;
    }

    output.status = GroupApplicationStatus::kSucceeded;
    output.group = *group_view;
    output.message = "group queried";
    return output;
}

GroupRepositoryMutationResult GroupRepositoryAdapter::UpdateGroup(
    const UpdateGroupCommand& command
) {
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    if (fingerprint.size() != 64) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "failed to fingerprint UpdateGroup request");
    }

    bool found = false;
    auto precheck = PrecheckOperation(
        repository_, command.actor_user_id, command.client_operation_id,
        kUpdateOperation, fingerprint, &found
    );
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) {
        return precheck;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "UpdateGroup failed to acquire database connection");
    }
    if (!connection->BeginTransaction()) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "UpdateGroup failed to begin transaction: " + connection->LastError());
    }

    const auto locked = repository_->FindGroupByIdOnConnection(
        connection.operator->(), command.group_id, true
    );
    if (!locked.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(locked.status), locked.message);
    }
    if (!locked.found) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kNotFound, "group not found");
    }

    // A concurrent duplicate may have completed while this request was waiting
    // for the group row lock. Re-check before version/permission evaluation.
    const auto concurrent_op = repository_->FindOperationOnConnection(
        connection.operator->(), command.actor_user_id, command.client_operation_id, false
    );
    if (!concurrent_op.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(concurrent_op.status), concurrent_op.message);
    }
    if (concurrent_op.found) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        return ResolveExistingOperation(
            repository_, command.actor_user_id, command.client_operation_id,
            kUpdateOperation, fingerprint
        );
    }

    const auto group_view = ToView(locked.record);
    if (!group_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kInvalidRecord, "invalid locked group record");
    }
    if (group_view->status != GroupStatus::kActive) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kFailedPrecondition, "disbanded group cannot be updated");
    }
    if (group_view->version != command.expected_version) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kAborted, "group version changed; refresh before retrying mutation");
    }

    const auto member = repository_->FindMemberOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, true
    );
    if (!member.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(member.status), member.message);
    }
    const auto actor = member.found ? ToView(member.record) : std::optional<GroupMemberView>{};
    if (!actor.has_value() || actor->status != GroupMemberStatus::kActive) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kPermissionDenied, "actor is not an active group member");
    }

    const bool metadata_change = command.name.has_value() ||
                                 command.description.has_value() ||
                                 command.avatar_url.has_value();
    if ((metadata_change && !permission_->CanUpdateMetadata(actor->role)) ||
        (command.join_policy.has_value() && !permission_->CanUpdateJoinPolicy(actor->role))) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kPermissionDenied, "actor has no permission to update requested group fields");
    }

    const std::uint64_t new_version = group_view->version + 1;
    const auto mutation = repository_->UpdateGroupOnConnection(
        connection.operator->(), command.group_id,
        command.name.has_value(), command.name.value_or(""),
        command.description.has_value(), command.description.value_or(""),
        command.avatar_url.has_value(), command.avatar_url.value_or(""),
        command.join_policy.has_value(),
        static_cast<std::uint32_t>(command.join_policy.value_or(group_view->join_policy)),
        new_version
    );
    if (!mutation.Succeeded() || mutation.affected_rows != 1) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(
            mutation.Succeeded() ? GroupApplicationStatus::kAborted : MapStorageStatus(mutation.status),
            mutation.Succeeded() ? "group update lost its active-row precondition" : mutation.message
        );
    }

    const auto updated = repository_->FindGroupByIdOnConnection(
        connection.operator->(), command.group_id, false
    );
    if (!updated.Succeeded() || !updated.found) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(
            updated.Succeeded() ? GroupApplicationStatus::kInvalidRecord : MapStorageStatus(updated.status),
            updated.Succeeded() ? "UpdateGroup post-update verification failed" : updated.message
        );
    }
    const auto updated_view = ToView(updated.record);
    if (!updated_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kInvalidRecord, "UpdateGroup produced invalid group record");
    }

    const auto event_spec = GroupEventFactory::GroupUpdated(*updated_view);
    const auto outbox_result = outbox_->InsertOnConnection(
        connection.operator->(), event_spec.event, event_spec.topic,
        event_spec.tag, event_spec.message_key
    );
    if (!outbox_result.success) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kStorageError, "UpdateGroup outbox insert failed: " + outbox_result.message);
    }

    tinyimx::GroupOperationRecord operation;
    operation.actor_user_id = command.actor_user_id;
    operation.client_operation_id = command.client_operation_id;
    operation.operation_type = kUpdateOperation;
    operation.request_fingerprint = fingerprint;
    operation.group_id = updated_view->group_id;
    operation.result_version = updated_view->version;
    operation.result_member_version = updated_view->member_version;
    const auto op_result = repository_->InsertOperationOnConnection(connection.operator->(), operation);
    if (!op_result.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        auto recovered = ResolveExistingOperation(
            repository_, command.actor_user_id, command.client_operation_id,
            kUpdateOperation, fingerprint
        );
        if (recovered.status == GroupApplicationStatus::kSucceeded) {
            return recovered;
        }
        return StorageFailure(MapStorageStatus(op_result.status), op_result.message);
    }

    return CommitOrRecover(
        &connection, repository_, command.actor_user_id, command.client_operation_id,
        kUpdateOperation, fingerprint, *updated_view,
        "group update, outbox event, and idempotency record committed"
    );
}

GroupRepositoryMutationResult GroupRepositoryAdapter::DisbandGroup(
    const DisbandGroupCommand& command
) {
    if (!ValidDependencies(repository_, pool_, outbox_, permission_)) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "group repository adapter dependencies are unavailable");
    }
    const std::string fingerprint = Fingerprint(command);
    if (fingerprint.size() != 64) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "failed to fingerprint DisbandGroup request");
    }

    bool found = false;
    auto precheck = PrecheckOperation(
        repository_, command.actor_user_id, command.client_operation_id,
        kDisbandOperation, fingerprint, &found
    );
    if (found || precheck.status != GroupApplicationStatus::kSucceeded) {
        return precheck;
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "DisbandGroup failed to acquire database connection");
    }
    if (!connection->BeginTransaction()) {
        return StorageFailure(GroupApplicationStatus::kStorageError, "DisbandGroup failed to begin transaction: " + connection->LastError());
    }

    const auto locked = repository_->FindGroupByIdOnConnection(
        connection.operator->(), command.group_id, true
    );
    if (!locked.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(locked.status), locked.message);
    }
    if (!locked.found) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kNotFound, "group not found");
    }

    const auto concurrent_op = repository_->FindOperationOnConnection(
        connection.operator->(), command.actor_user_id, command.client_operation_id, false
    );
    if (!concurrent_op.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(concurrent_op.status), concurrent_op.message);
    }
    if (concurrent_op.found) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        return ResolveExistingOperation(
            repository_, command.actor_user_id, command.client_operation_id,
            kDisbandOperation, fingerprint
        );
    }

    const auto group_view = ToView(locked.record);
    if (!group_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kInvalidRecord, "invalid locked group record");
    }
    if (group_view->status != GroupStatus::kActive) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kFailedPrecondition, "group is already disbanded");
    }
    if (group_view->version != command.expected_version) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kAborted, "group version changed; refresh before retrying disband");
    }

    const auto member = repository_->FindMemberOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id, true
    );
    if (!member.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(MapStorageStatus(member.status), member.message);
    }
    const auto actor = member.found ? ToView(member.record) : std::optional<GroupMemberView>{};
    if (!actor.has_value() || actor->status != GroupMemberStatus::kActive ||
        actor->user_id != group_view->owner_user_id || !permission_->CanDisband(actor->role)) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kPermissionDenied, "only the active group owner can disband the group");
    }

    const auto mutation = repository_->DisbandGroupOnConnection(
        connection.operator->(), command.group_id, command.actor_user_id,
        group_view->version + 1, group_view->member_version + 1
    );
    if (!mutation.Succeeded() || mutation.affected_rows != 1) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(
            mutation.Succeeded() ? GroupApplicationStatus::kAborted : MapStorageStatus(mutation.status),
            mutation.Succeeded() ? "group disband lost its active-row precondition" : mutation.message
        );
    }

    const auto disbanded = repository_->FindGroupByIdOnConnection(
        connection.operator->(), command.group_id, false
    );
    if (!disbanded.Succeeded() || !disbanded.found) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(
            disbanded.Succeeded() ? GroupApplicationStatus::kInvalidRecord : MapStorageStatus(disbanded.status),
            disbanded.Succeeded() ? "DisbandGroup post-update verification failed" : disbanded.message
        );
    }
    const auto disbanded_view = ToView(disbanded.record);
    if (!disbanded_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kInvalidRecord, "DisbandGroup produced invalid group record");
    }

    const auto event_spec = GroupEventFactory::GroupDisbanded(*disbanded_view);
    const auto outbox_result = outbox_->InsertOnConnection(
        connection.operator->(), event_spec.event, event_spec.topic,
        event_spec.tag, event_spec.message_key
    );
    if (!outbox_result.success) {
        RollbackIfNeeded(connection.operator->());
        return StorageFailure(GroupApplicationStatus::kStorageError, "DisbandGroup outbox insert failed: " + outbox_result.message);
    }

    tinyimx::GroupOperationRecord operation;
    operation.actor_user_id = command.actor_user_id;
    operation.client_operation_id = command.client_operation_id;
    operation.operation_type = kDisbandOperation;
    operation.request_fingerprint = fingerprint;
    operation.group_id = disbanded_view->group_id;
    operation.result_version = disbanded_view->version;
    operation.result_member_version = disbanded_view->member_version;
    const auto op_result = repository_->InsertOperationOnConnection(connection.operator->(), operation);
    if (!op_result.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        auto recovered = ResolveExistingOperation(
            repository_, command.actor_user_id, command.client_operation_id,
            kDisbandOperation, fingerprint
        );
        if (recovered.status == GroupApplicationStatus::kSucceeded) {
            return recovered;
        }
        return StorageFailure(MapStorageStatus(op_result.status), op_result.message);
    }

    return CommitOrRecover(
        &connection, repository_, command.actor_user_id, command.client_operation_id,
        kDisbandOperation, fingerprint, *disbanded_view,
        "group disband, outbox event, and idempotency record committed"
    );
}

}  // namespace tinyimx::group
