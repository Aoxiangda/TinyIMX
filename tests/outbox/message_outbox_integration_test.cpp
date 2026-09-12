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
constexpr std::uint64_t kUserB = 10002;

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

bool QueryScalar(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& sql,
    std::string* value
) {
    if (pool == nullptr || value == nullptr) {
        return false;
    }
    auto connection = pool->Acquire();
    if (!connection) {
        return false;
    }
    tinyimx::MySqlQueryResult result;
    if (!connection->Query(sql, &result) ||
        result.rows.size() != 1 || result.rows.front().size() != 1) {
        return false;
    }
    *value = result.rows.front().front();
    return true;
}

std::uint64_t EventCount(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& event_id
) {
    auto connection = pool->Acquire();
    if (!connection) {
        return 0;
    }
    const std::string sql =
        "SELECT COUNT(*) FROM im_event_outbox WHERE event_id = '" +
        connection->EscapeString(event_id) + "'";
    tinyimx::MySqlQueryResult result;
    if (!connection->Query(sql, &result) ||
        result.rows.size() != 1 || result.rows.front().size() != 1) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::stoull(result.rows.front().front()));
}

std::uint64_t DialogReadEventCount(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t reader,
    std::uint64_t peer
) {
    auto connection = pool->Acquire();
    if (!connection) {
        return 0;
    }
    const std::string aggregate = std::to_string(reader) + ":" + std::to_string(peer);
    const std::string sql =
        "SELECT COUNT(*) FROM im_event_outbox "
        "WHERE event_type = 'dialog.read_advanced.v1' "
        "AND aggregate_id = '" + connection->EscapeString(aggregate) + "'";
    tinyimx::MySqlQueryResult result;
    if (!connection->Query(sql, &result) ||
        result.rows.size() != 1 || result.rows.front().size() != 1) {
        return 0;
    }
    return static_cast<std::uint64_t>(std::stoull(result.rows.front().front()));
}

std::string EventPayload(
    tinyimx::MySqlConnectionPool* pool,
    const std::string& event_id
) {
    auto connection = pool->Acquire();
    if (!connection) {
        return {};
    }
    const std::string sql =
        "SELECT payload FROM im_event_outbox WHERE event_id = '" +
        connection->EscapeString(event_id) + "' LIMIT 1";
    tinyimx::MySqlQueryResult result;
    if (!connection->Query(sql, &result) ||
        result.rows.size() != 1 || result.rows.front().size() != 1) {
        return {};
    }
    return result.rows.front().front();
}

bool OutboxTableExists(tinyimx::MySqlConnectionPool* pool) {
    std::string value;
    return QueryScalar(
        pool,
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name = 'im_event_outbox'",
        &value
    ) && value == "1";
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

    tinyimx::MessageRepository repository(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::message::MessageRepositoryAdapter adapter(
        &repository,
        &pool,
        &outbox
    );

    std::cout << "========== TinyIMX M16-A Transactional Outbox Integration ==========\n";

    Expect(OutboxTableExists(&pool), "Outbox schema exists");

    // Event contract is intentionally minimal: content and client id are not
    // required by downstream unread/notification projections.
    tinyimx::message::MessageView event_probe;
    event_probe.message_id = 900000001;
    event_probe.client_message_id = "must-not-leak-client-message-id";
    event_probe.from_user_id = kUserA;
    event_probe.to_user_id = kUserB;
    event_probe.message_type = 1;
    event_probe.content = "must-not-leak-message-content";
    event_probe.created_at = "2026-09-08 00:00:00";
    const auto event_spec = tinyimx::message::MessageEventFactory::MessageCreated(event_probe);
    const auto encoded = tinyimx::eventing::EventCodec::Encode(event_spec.event);
    Expect(encoded.success, "EventCodec encodes message.created");
    Expect(encoded.encoded.find("must-not-leak-message-content") == std::string::npos,
           "message.created excludes content");
    Expect(encoded.encoded.find("must-not-leak-client-message-id") == std::string::npos,
           "message.created excludes client_message_id");

    const std::string created_c = UniqueId("m16a-created-");
    const std::string created_content =
        R"({"from":10001,"text":"m16a transactional outbox","to":10002})";
    const auto created = adapter.PersistPrivateMessage(
        kUserA, kUserB, created_c, 1, created_content
    );
    Expect(created.Completed() &&
           created.outcome == tinyimx::message::PersistPrivateMessageOutcome::kCreated &&
           created.message_id != 0,
           "Created commits durable message");

    const std::string created_event_id =
        "message.created.v1:" + std::to_string(created.message_id);
    Expect(EventCount(&pool, created_event_id) == 1,
           "Created commits exactly one message.created outbox row");
    const std::string created_payload = EventPayload(&pool, created_event_id);
    Expect(!created_payload.empty(), "Created outbox payload readable");
    Expect(created_payload.find(created_content) == std::string::npos,
           "durable outbox payload excludes message content");
    Expect(created_payload.find(created_c) == std::string::npos,
           "durable outbox payload excludes client_message_id");

    const auto reused = adapter.PersistPrivateMessage(
        kUserA, kUserB, created_c, 1, created_content
    );
    Expect(reused.Completed() &&
           reused.outcome == tinyimx::message::PersistPrivateMessageOutcome::kReused &&
           reused.message_id == created.message_id,
           "Same C reuses same M");
    Expect(EventCount(&pool, created_event_id) == 1,
           "Reused does not duplicate outbox event");

    const auto conflict = adapter.PersistPrivateMessage(
        kUserA, kUserB, created_c, 1, created_content + "-conflict"
    );
    Expect(conflict.Completed() &&
           conflict.outcome == tinyimx::message::PersistPrivateMessageOutcome::kIdempotencyConflict &&
           conflict.message_id == created.message_id,
           "Same C with different identity conflicts");
    Expect(EventCount(&pool, created_event_id) == 1,
           "Conflict does not duplicate outbox event");

    // Deterministic UNIQUE-key race: all callers must miss precheck before any
    // caller is allowed to insert.
    constexpr std::size_t kThreads = 4;
    const std::string concurrent_c = UniqueId("m16a-concurrent-");
    const std::string concurrent_content =
        R"({"from":10001,"text":"m16a concurrent outbox","to":10002})";
    auto barrier = std::make_shared<std::barrier<>>(
        static_cast<std::ptrdiff_t>(kThreads)
    );
    adapter.SetTransactionalPreInsertHookForTest([barrier]() {
        barrier->arrive_and_wait();
    });

    std::vector<tinyimx::message::MessageRepositoryPersistResult> concurrent(kThreads);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() {
            concurrent[i] = adapter.PersistPrivateMessage(
                kUserA, kUserB, concurrent_c, 1, concurrent_content
            );
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    adapter.SetTransactionalPreInsertHookForTest({});

    std::size_t created_count = 0;
    std::size_t reused_count = 0;
    std::uint64_t common_message_id = 0;
    bool common_id = true;
    for (const auto& result : concurrent) {
        if (result.outcome == tinyimx::message::PersistPrivateMessageOutcome::kCreated &&
            result.Completed()) {
            ++created_count;
        } else if (result.outcome == tinyimx::message::PersistPrivateMessageOutcome::kReused &&
                   result.Completed()) {
            ++reused_count;
        }
        if (common_message_id == 0) {
            common_message_id = result.message_id;
        } else if (result.message_id != common_message_id) {
            common_id = false;
        }
    }
    Expect(created_count == 1, "Concurrent same C has exactly one Created");
    Expect(reused_count == kThreads - 1, "Concurrent same C reuses all other requests");
    Expect(common_id && common_message_id != 0, "Concurrent same C converges to one M");
    Expect(EventCount(
               &pool,
               "message.created.v1:" + std::to_string(common_message_id)
           ) == 1,
           "Concurrent same C commits exactly one outbox event");

    // Atomicity fault: Outbox failure must roll the newly inserted business row
    // back instead of returning a durable message without a durable event.
    const std::string rollback_c = UniqueId("m16a-rollback-");
    outbox.SetForceInsertFailureForTest(true);
    const auto rollback_result = adapter.PersistPrivateMessage(
        kUserA, kUserB, rollback_c, 1,
        R"({"from":10001,"text":"must rollback","to":10002})"
    );
    outbox.SetForceInsertFailureForTest(false);
    Expect(!rollback_result.Completed(), "Injected outbox failure rejects persistence");
    const auto rollback_lookup = repository.FindPrivateMessageByClientMessageId(
        kUserA, rollback_c
    );
    Expect(rollback_lookup.Succeeded() && !rollback_lookup.Found(),
           "Outbox failure rolls back message row");

    // Read transition uses the same UoW rule. First prepare one confirmed row.
    const std::string read_c = UniqueId("m16a-read-");
    const auto read_created = adapter.PersistPrivateMessage(
        kUserA, kUserB, read_c, 1,
        R"({"from":10001,"text":"read transition","to":10002})"
    );
    Expect(read_created.Accepted(), "Read fixture message persisted");
    const auto confirmed = repository.MarkReceiverConfirmed(read_created.message_id);
    Expect(confirmed.Succeeded(), "Read fixture advanced to ReceiverConfirmed");

    const std::uint64_t read_events_before = DialogReadEventCount(&pool, kUserB, kUserA);
    outbox.SetForceInsertFailureForTest(true);
    const auto read_rollback = adapter.MarkDialogRead(kUserB, kUserA);
    outbox.SetForceInsertFailureForTest(false);
    Expect(!read_rollback.Succeeded(), "Injected read outbox failure rejects mutation");
    const auto after_read_rollback = repository.FindPrivateMessageById(read_created.message_id);
    Expect(after_read_rollback.Found() &&
           after_read_rollback.record.delivery_status ==
               static_cast<std::uint32_t>(tinyimx::DeliveryStatus::kReceiverConfirmed),
           "Read outbox failure rolls back delivery-state transition");
    Expect(DialogReadEventCount(&pool, kUserB, kUserA) == read_events_before,
           "Read outbox failure creates no durable event");

    const auto read_success = adapter.MarkDialogRead(kUserB, kUserA);
    Expect(read_success.Succeeded() && read_success.affected_rows > 0,
           "MarkDialogRead commits durable transition");
    Expect(DialogReadEventCount(&pool, kUserB, kUserA) == read_events_before + 1,
           "MarkDialogRead commits one dialog.read_advanced event");

    const auto read_retry = adapter.MarkDialogRead(kUserB, kUserA);
    Expect(read_retry.Succeeded() && read_retry.affected_rows == 0,
           "MarkDialogRead retry is idempotent");
    Expect(DialogReadEventCount(&pool, kUserB, kUserA) == read_events_before + 1,
           "MarkDialogRead retry creates no duplicate event");

    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout << "====================================================================\n";
    if (g_failed == 0) {
        std::cout << "[PASS] M16-A Transactional Outbox integration tests\n";
        return 0;
    }
    std::cerr << "[FAIL] M16-A Transactional Outbox integration tests, failed="
              << g_failed << '\n';
    return 1;
}
