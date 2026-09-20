#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/eventing/EventCodec.h"
#include "services/message/application/MessageEventFactory.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"

#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kUserA = 10001;
constexpr std::uint64_t kGroupVersion = 7;
constexpr std::uint64_t kMembershipEpoch = 3;
constexpr std::uint32_t kOwnerRole = 1;

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

bool GroupMessageTableExists(tinyimx::MySqlConnectionPool* pool) {
    if (pool == nullptr) return false;
    auto connection = pool->Acquire();
    if (!connection) return false;
    tinyimx::MySqlQueryResult rows;
    return connection->Query(
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name = 'im_group_messages'",
        &rows
    ) && rows.rows.size() == 1 && rows.rows.front().size() == 1 &&
        rows.rows.front().front() == "1";
}

std::uint64_t CreateFixtureGroup(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& name
) {
    if (pool == nullptr) return 0;
    auto connection = pool->Acquire();
    if (!connection) return 0;
    const std::string sql =
        "INSERT INTO im_groups "
        "(name, description, avatar_url, owner_user_id, status, join_policy, "
        "max_members, version, member_version) VALUES ('" +
        connection->EscapeString(name) + "', '', '', " +
        std::to_string(kUserA) + ", 1, 1, 20, 1, " +
        std::to_string(kGroupVersion) + ")";
    if (!connection->Execute(sql)) return 0;
    return connection->LastInsertId();
}

std::uint64_t OutboxCount(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& event_id
) {
    if (pool == nullptr) return 0;
    auto connection = pool->Acquire();
    if (!connection) return 0;
    tinyimx::MySqlQueryResult rows;
    const std::string sql =
        "SELECT COUNT(*) FROM im_event_outbox WHERE event_id='" +
        connection->EscapeString(event_id) + "'";
    if (!connection->Query(sql, &rows) || rows.rows.size() != 1 ||
        rows.rows.front().size() != 1) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::stoull(rows.rows.front().front()));
}

std::string OutboxPayload(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& event_id
) {
    if (pool == nullptr) return {};
    auto connection = pool->Acquire();
    if (!connection) return {};
    tinyimx::MySqlQueryResult rows;
    const std::string sql =
        "SELECT payload FROM im_event_outbox WHERE event_id='" +
        connection->EscapeString(event_id) + "' LIMIT 1";
    if (!connection->Query(sql, &rows) || rows.rows.size() != 1 ||
        rows.rows.front().size() != 1) {
        return {};
    }
    return rows.rows.front().front();
}

void CleanupGroup(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t group_id
) {
    if (pool == nullptr || group_id == 0) return;
    auto connection = pool->Acquire();
    if (!connection) return;

    // Outbox rows intentionally have no FK into the aggregate table, so remove
    // the event intent before deleting the test aggregate.
    connection->Execute(
        "DELETE FROM im_event_outbox WHERE event_type='group_message.created.v1' "
        "AND aggregate_type='group_message' "
        "AND CAST(aggregate_id AS UNSIGNED) IN "
        "(SELECT message_id FROM im_group_messages WHERE group_id=" +
        std::to_string(group_id) + ")"
    );
    connection->Execute(
        "DELETE FROM im_group_messages WHERE group_id=" + std::to_string(group_id)
    );
    connection->Execute(
        "DELETE FROM im_groups WHERE group_id=" + std::to_string(group_id)
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];

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

    tinyimx::MessageRepository repository(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::message::MessageRepositoryAdapter adapter(
        &repository,
        &pool,
        &outbox
    );

    std::cout
        << "========== TinyIMX M17-B1 Group Durable Write Integration ==========\n";

    Expect(GroupMessageTableExists(&pool), "im_group_messages schema exists");

    const std::string fixture_name = UniqueId("m17b1-group-");
    const std::uint64_t group_id = CreateFixtureGroup(&pool, fixture_name);
    Expect(group_id != 0, "fixture group created");
    if (group_id == 0) {
        pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::message::GroupMessageView event_probe;
    event_probe.message_id = 99000001;
    event_probe.client_message_id = "must-not-leak-client-message-id";
    event_probe.group_id = group_id;
    event_probe.from_user_id = kUserA;
    event_probe.message_type = 1;
    event_probe.content = "must-not-leak-group-message-content";
    event_probe.membership_epoch = kMembershipEpoch;
    event_probe.member_version = kGroupVersion;
    event_probe.authorized_role = kOwnerRole;
    event_probe.created_at = "2026-09-18 00:00:00";
    const auto event_spec =
        tinyimx::message::MessageEventFactory::GroupMessageCreated(event_probe);
    const auto encoded = tinyimx::eventing::EventCodec::Encode(event_spec.event);
    Expect(encoded.success, "EventCodec encodes group_message.created.v1");
    Expect(encoded.encoded.find(event_probe.content) == std::string::npos,
           "group event excludes content");
    Expect(encoded.encoded.find(event_probe.client_message_id) == std::string::npos,
           "group event excludes client_message_id");

    const std::string created_c = UniqueId("m17b1-created-");
    const std::string created_content = "m17-b1 reliable group message";
    const auto created = adapter.PersistAuthorizedGroupMessage(
        kUserA,
        group_id,
        created_c,
        1,
        created_content,
        kMembershipEpoch,
        kGroupVersion,
        kOwnerRole
    );
    Expect(
        created.Completed() &&
        created.outcome == tinyimx::message::PersistGroupMessageOutcome::kCreated &&
        created.message_id != 0,
        "authorized group message Created"
    );
    Expect(
        created.record.membership_epoch == kMembershipEpoch &&
        created.record.member_version == kGroupVersion &&
        created.record.authorized_role == kOwnerRole,
        "authorization snapshot persisted"
    );

    const std::string created_event_id =
        "group_message.created.v1:" + std::to_string(created.message_id);
    Expect(OutboxCount(&pool, created_event_id) == 1,
           "Created commits exactly one group-message outbox event");
    const std::string payload = OutboxPayload(&pool, created_event_id);
    Expect(!payload.empty(), "group-message outbox payload readable");
    Expect(payload.find(created_content) == std::string::npos,
           "durable group event excludes content");
    Expect(payload.find(created_c) == std::string::npos,
           "durable group event excludes client_message_id");

    // Snapshot changes do not participate in the logical-message fingerprint.
    const auto reused = adapter.PersistAuthorizedGroupMessage(
        kUserA,
        group_id,
        created_c,
        1,
        created_content,
        kMembershipEpoch + 1,
        kGroupVersion + 1,
        2
    );
    Expect(
        reused.Completed() &&
        reused.outcome == tinyimx::message::PersistGroupMessageOutcome::kReused &&
        reused.message_id == created.message_id,
        "same logical send Reused despite newer authorization snapshot"
    );
    Expect(
        reused.record.membership_epoch == kMembershipEpoch &&
        reused.record.member_version == kGroupVersion &&
        reused.record.authorized_role == kOwnerRole,
        "Reused preserves original authorization snapshot"
    );
    Expect(OutboxCount(&pool, created_event_id) == 1,
           "Reused does not duplicate group outbox event");

    const auto content_conflict = adapter.PersistAuthorizedGroupMessage(
        kUserA,
        group_id,
        created_c,
        1,
        created_content + "-different",
        kMembershipEpoch,
        kGroupVersion,
        kOwnerRole
    );
    Expect(
        content_conflict.Completed() &&
        content_conflict.outcome ==
            tinyimx::message::PersistGroupMessageOutcome::kIdempotencyConflict &&
        content_conflict.message_id == created.message_id,
        "same client_message_id with different content conflicts"
    );

    const std::uint64_t second_group_id =
        CreateFixtureGroup(&pool, UniqueId("m17b1-group2-"));
    Expect(second_group_id != 0, "second fixture group created");
    if (second_group_id != 0) {
        const auto group_conflict = adapter.PersistAuthorizedGroupMessage(
            kUserA,
            second_group_id,
            created_c,
            1,
            created_content,
            kMembershipEpoch,
            kGroupVersion,
            kOwnerRole
        );
        Expect(
            group_conflict.Completed() &&
            group_conflict.outcome ==
                tinyimx::message::PersistGroupMessageOutcome::kIdempotencyConflict &&
            group_conflict.message_id == created.message_id,
            "same client_message_id with different group conflicts"
        );
    }

    constexpr std::size_t kThreads = 4;
    const std::string concurrent_c = UniqueId("m17b1-concurrent-");
    const std::string concurrent_content = "m17-b1 concurrent group message";
    auto barrier = std::make_shared<std::barrier<>>(
        static_cast<std::ptrdiff_t>(kThreads)
    );
    adapter.SetTransactionalPreInsertHookForTest([barrier]() {
        barrier->arrive_and_wait();
    });

    std::vector<tinyimx::message::MessageRepositoryGroupPersistResult>
        concurrent(kThreads);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            concurrent[i] = adapter.PersistAuthorizedGroupMessage(
                kUserA,
                group_id,
                concurrent_c,
                1,
                concurrent_content,
                kMembershipEpoch,
                kGroupVersion,
                kOwnerRole
            );
        });
    }
    for (auto& thread : threads) thread.join();
    adapter.SetTransactionalPreInsertHookForTest({});

    std::size_t created_count = 0;
    std::size_t reused_count = 0;
    std::uint64_t common_message_id = 0;
    bool common_id = true;
    for (const auto& result : concurrent) {
        if (result.Completed() &&
            result.outcome == tinyimx::message::PersistGroupMessageOutcome::kCreated) {
            ++created_count;
        } else if (
            result.Completed() &&
            result.outcome == tinyimx::message::PersistGroupMessageOutcome::kReused
        ) {
            ++reused_count;
        }
        if (common_message_id == 0) common_message_id = result.message_id;
        else if (result.message_id != common_message_id) common_id = false;
    }
    Expect(created_count == 1, "concurrent same C has exactly one Created");
    Expect(reused_count == kThreads - 1, "concurrent same C reuses all others");
    Expect(common_id && common_message_id != 0,
           "concurrent same C converges to one group message id");
    Expect(
        OutboxCount(
            &pool,
            "group_message.created.v1:" + std::to_string(common_message_id)
        ) == 1,
        "concurrent same C commits exactly one outbox event"
    );

    const std::string rollback_c = UniqueId("m17b1-rollback-");
    outbox.SetForceInsertFailureForTest(true);
    const auto rollback = adapter.PersistAuthorizedGroupMessage(
        kUserA,
        group_id,
        rollback_c,
        1,
        "must rollback with outbox",
        kMembershipEpoch,
        kGroupVersion,
        kOwnerRole
    );
    outbox.SetForceInsertFailureForTest(false);
    Expect(!rollback.Completed(), "injected outbox failure rejects group persistence");
    const auto rollback_lookup = repository.FindGroupMessageByClientMessageId(
        kUserA,
        rollback_c
    );
    Expect(
        rollback_lookup.Succeeded() && !rollback_lookup.Found(),
        "outbox failure rolls back group message row"
    );

    CleanupGroup(&pool, group_id);
    if (second_group_id != 0) CleanupGroup(&pool, second_group_id);

    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout
        << "===================================================================\n";
    if (g_failed == 0) {
        std::cout << "[PASS] M17-B1 group durable write integration tests\n";
        return 0;
    }
    std::cerr
        << "[FAIL] M17-B1 group durable write integration tests, failed="
        << g_failed << '\n';
    return 1;
}
