#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/group/application/GroupPermissionPolicy.h"
#include "services/group/repository/GroupRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/GroupRepository.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kOwnerUserId = 10001;
constexpr std::uint64_t kOtherUserId = 10002;
int g_failed = 0;

void Expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++g_failed;
    }
}

std::string UniqueId(const std::string& prefix) {
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    return prefix + std::to_string(static_cast<long long>(nanos));
}

bool TableExists(tinyimx::MySqlConnectionPool* pool, const std::string& table) {
    auto connection = pool->Acquire();
    if (!connection) {
        return false;
    }
    tinyimx::MySqlQueryResult result;
    const std::string sql =
        "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = DATABASE() "
        "AND table_name = '" + connection->EscapeString(table) + "'";
    return connection->Query(sql, &result) && result.rows.size() == 1 &&
           result.rows.front().size() == 1 && result.rows.front().front() == "1";
}

std::uint64_t CountRows(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& table,
    const std::string& where_clause
) {
    auto connection = pool->Acquire();
    if (!connection) {
        return 0;
    }
    tinyimx::MySqlQueryResult result;
    const std::string sql = "SELECT COUNT(*) FROM " + table + " WHERE " + where_clause;
    if (!connection->Query(sql, &result) || result.rows.size() != 1 ||
        result.rows.front().size() != 1) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::stoull(result.rows.front().front()));
}

void CleanupGroup(tinyimx::MySqlConnectionPool* pool, std::uint64_t group_id) {
    if (group_id == 0) {
        return;
    }
    auto connection = pool->Acquire();
    if (!connection) {
        return;
    }
    connection->Execute(
        "DELETE FROM im_event_outbox WHERE aggregate_type = 'group' AND aggregate_id = '" +
        std::to_string(group_id) + "'"
    );
    connection->Execute(
        "DELETE FROM im_group_operation_dedup WHERE group_id = " + std::to_string(group_id)
    );
    connection->Execute(
        "DELETE FROM im_group_membership_history WHERE group_id = " + std::to_string(group_id)
    );
    connection->Execute(
        "DELETE FROM im_group_members WHERE group_id = " + std::to_string(group_id)
    );
    connection->Execute(
        "DELETE FROM im_groups WHERE group_id = " + std::to_string(group_id)
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "[FAIL] config load: " << config.LastError() << '\n';
        return 1;
    }
    if (!config.MySql().enable) {
        std::cerr << "[FAIL] MySQL must be enabled\n";
        return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "[FAIL] logger init\n";
        return 1;
    }

    tinyimx::MySqlConnectionPool pool;
    if (!pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] mysql pool init\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::GroupRepository repository(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::group::GroupPermissionPolicy permission;
    tinyimx::group::GroupRepositoryAdapter adapter(
        &repository, &pool, &outbox, &permission
    );

    std::cout << "========== TinyIMX M17-A1 Group Repository Integration ==========\n";
    Expect(TableExists(&pool, "im_groups"), "im_groups schema exists");
    Expect(TableExists(&pool, "im_group_members"), "im_group_members schema exists");
    Expect(TableExists(&pool, "im_group_membership_history"), "membership history schema exists");
    Expect(TableExists(&pool, "im_group_operation_dedup"), "operation dedup schema exists");

    const std::string create_op = UniqueId("m17a1-create-");
    tinyimx::group::CreateGroupCommand create;
    create.actor_user_id = kOwnerUserId;
    create.client_operation_id = create_op;
    create.name = UniqueId("m17-a1-group-");
    create.description = "M17-A1 integration group";
    create.join_policy = tinyimx::group::GroupJoinPolicy::kInviteOnly;
    create.max_members = 500;

    const auto created = adapter.CreateGroup(create);
    Expect(created.Accepted() &&
           created.outcome == tinyimx::group::GroupMutationOutcome::kApplied &&
           created.group.has_value(),
           "CreateGroup atomically creates durable group");

    const std::uint64_t group_id = created.group.has_value() ? created.group->group_id : 0;
    if (group_id != 0) {
        Expect(CountRows(&pool, "im_group_members",
                         "group_id = " + std::to_string(group_id) +
                         " AND user_id = " + std::to_string(kOwnerUserId) +
                         " AND role = 1 AND status = 1 AND membership_epoch = 1") == 1,
               "CreateGroup creates owner membership");
        Expect(CountRows(&pool, "im_group_membership_history",
                         "group_id = " + std::to_string(group_id) +
                         " AND user_id = " + std::to_string(kOwnerUserId) +
                         " AND membership_epoch = 1") == 1,
               "CreateGroup creates membership history");
        Expect(CountRows(&pool, "im_group_operation_dedup",
                         "actor_user_id = " + std::to_string(kOwnerUserId) +
                         " AND client_operation_id = '" + create_op + "'") == 1,
               "CreateGroup commits one durable operation record");
        Expect(CountRows(&pool, "im_event_outbox",
                         "event_id = 'group.created.v1:" + std::to_string(group_id) + ":1'"
                         " AND topic = 'tinyimx-message-events'"
                         " AND tag = 'group.created.v1'") == 1,
               "CreateGroup commits one relay-compatible group.created outbox event");
    }

    const auto reused = adapter.CreateGroup(create);
    Expect(reused.Accepted() &&
           reused.outcome == tinyimx::group::GroupMutationOutcome::kReused &&
           reused.group.has_value() && reused.group->group_id == group_id,
           "CreateGroup response-loss retry reuses durable operation");

    auto conflicting_create = create;
    conflicting_create.name += "-different";
    const auto conflict = adapter.CreateGroup(conflicting_create);
    Expect(conflict.Completed() &&
           conflict.outcome == tinyimx::group::GroupMutationOutcome::kIdempotencyConflict,
           "CreateGroup rejects operation-id reuse with different payload");

    const auto owner_get = adapter.GetGroup(kOwnerUserId, group_id);
    Expect(owner_get.Found(), "owner can query group");
    const auto outsider_get = adapter.GetGroup(kOtherUserId, group_id);
    Expect(outsider_get.status == tinyimx::group::GroupApplicationStatus::kPermissionDenied,
           "non-member cannot query group");

    tinyimx::group::UpdateGroupCommand update;
    update.actor_user_id = kOwnerUserId;
    update.client_operation_id = UniqueId("m17a1-update-");
    update.group_id = group_id;
    update.expected_version = 1;
    update.name = "m17-a1-updated";
    const auto updated = adapter.UpdateGroup(update);
    Expect(updated.Accepted() && updated.group.has_value() &&
           updated.group->version == 2 && updated.group->name == "m17-a1-updated",
           "UpdateGroup commits versioned mutation");

    auto stale_update = update;
    stale_update.client_operation_id = UniqueId("m17a1-stale-");
    stale_update.name = "must-not-apply";
    const auto stale = adapter.UpdateGroup(stale_update);
    Expect(stale.status == tinyimx::group::GroupApplicationStatus::kAborted,
           "UpdateGroup rejects stale expected_version");

    // Atomicity: force Outbox failure and prove the group row rolls back.
    tinyimx::group::UpdateGroupCommand rollback_update;
    rollback_update.actor_user_id = kOwnerUserId;
    rollback_update.client_operation_id = UniqueId("m17a1-rollback-");
    rollback_update.group_id = group_id;
    rollback_update.expected_version = 2;
    rollback_update.description = "must rollback";
    outbox.SetForceInsertFailureForTest(true);
    const auto rollback_result = adapter.UpdateGroup(rollback_update);
    outbox.SetForceInsertFailureForTest(false);
    Expect(!rollback_result.Completed(), "forced Outbox failure rejects UpdateGroup");
    Expect(CountRows(&pool, "im_group_operation_dedup",
                     "actor_user_id = " + std::to_string(kOwnerUserId) +
                     " AND client_operation_id = '" + rollback_update.client_operation_id + "'") == 0,
           "Outbox failure rolls back operation-dedup side effect");
    Expect(CountRows(&pool, "im_event_outbox",
                     "event_id = 'group.updated.v1:" + std::to_string(group_id) + ":3'") == 0,
           "Outbox failure leaves no update event behind");
    const auto after_rollback = adapter.GetGroup(kOwnerUserId, group_id);
    Expect(after_rollback.Found() && after_rollback.group->version == 2 &&
           after_rollback.group->description == "M17-A1 integration group",
           "Outbox failure rolls back group mutation");

    tinyimx::group::DisbandGroupCommand disband;
    disband.actor_user_id = kOwnerUserId;
    disband.client_operation_id = UniqueId("m17a1-disband-");
    disband.group_id = group_id;
    disband.expected_version = 2;
    const auto disbanded = adapter.DisbandGroup(disband);
    Expect(disbanded.Accepted() && disbanded.group.has_value() &&
           disbanded.group->status == tinyimx::group::GroupStatus::kDisbanded &&
           disbanded.group->version == 3 && disbanded.group->member_version == 2,
           "DisbandGroup atomically closes group and advances authority version");
    Expect(CountRows(&pool, "im_event_outbox",
                     "event_id = 'group.disbanded.v1:" + std::to_string(group_id) + ":3'"
                     " AND topic = 'tinyimx-message-events'"
                     " AND tag = 'group.disbanded.v1'") == 1,
           "DisbandGroup commits one relay-compatible group.disbanded event");

    const auto disband_reused = adapter.DisbandGroup(disband);
    Expect(disband_reused.Accepted() &&
           disband_reused.outcome == tinyimx::group::GroupMutationOutcome::kReused,
           "DisbandGroup response-loss retry is safe");

    auto after_disband_update = update;
    after_disband_update.client_operation_id = UniqueId("m17a1-after-disband-");
    after_disband_update.expected_version = 3;
    after_disband_update.name = "illegal";
    const auto after_disband = adapter.UpdateGroup(after_disband_update);
    Expect(after_disband.status == tinyimx::group::GroupApplicationStatus::kFailedPrecondition,
           "disbanded group rejects further metadata mutation");

    // Concurrent same operation: exactly one committed group, all callers
    // converge to the same durable group identity.
    const std::string concurrent_op = UniqueId("m17a1-concurrent-");
    tinyimx::group::CreateGroupCommand concurrent_create = create;
    concurrent_create.client_operation_id = concurrent_op;
    concurrent_create.name = UniqueId("m17-a1-concurrent-group-");
    constexpr std::size_t kThreads = 4;
    std::vector<tinyimx::group::GroupRepositoryMutationResult> results(kThreads);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() { results[i] = adapter.CreateGroup(concurrent_create); });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::size_t applied_count = 0;
    std::size_t reused_count = 0;
    std::uint64_t concurrent_group_id = 0;
    bool same_group = true;
    for (const auto& result : results) {
        if (result.Completed() && result.outcome == tinyimx::group::GroupMutationOutcome::kApplied) {
            ++applied_count;
        } else if (result.Completed() && result.outcome == tinyimx::group::GroupMutationOutcome::kReused) {
            ++reused_count;
        }
        if (!result.group.has_value()) {
            same_group = false;
            continue;
        }
        if (concurrent_group_id == 0) {
            concurrent_group_id = result.group->group_id;
        } else if (concurrent_group_id != result.group->group_id) {
            same_group = false;
        }
    }
    Expect(applied_count == 1, "concurrent duplicate CreateGroup has exactly one applied mutation");
    Expect(reused_count == kThreads - 1, "concurrent duplicate CreateGroup reuses all other callers");
    Expect(same_group && concurrent_group_id != 0,
           "concurrent duplicate CreateGroup converges to one group identity");

    CleanupGroup(&pool, group_id);
    CleanupGroup(&pool, concurrent_group_id);

    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout << "=================================================================\n";
    if (g_failed == 0) {
        std::cout << "[PASS] M17-A1 Group Repository integration tests\n";
        return 0;
    }
    std::cerr << "[FAIL] M17-A1 Group Repository integration tests, failed=" << g_failed << '\n';
    return 1;
}
