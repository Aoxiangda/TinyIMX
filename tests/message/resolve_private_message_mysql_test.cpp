#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failed = 0;
void Expect(bool condition, const char* label, std::uint64_t id) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << " message_id=" << id << '\n';
    if (!condition) ++failed;
}
}

// This fixture reads existing historical records. It never calls Persist,
// ConfirmReceiver, MarkRead, or an SQL mutation, and never prints content.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: resolve_private_message_mysql_tests CONFIG_PATH\n";
        return 2;
    }
    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1]) || !config.MySql().enable) {
        std::cerr << "[FAIL] enabled MySQL configuration required\n";
        return 2;
    }
    tinyimx::MySqlConnectionPool pool;
    if (!pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] MySQL pool initialization\n";
        return 2;
    }
    tinyimx::MessageRepository repository(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::message::MessageRepositoryAdapter adapter(&repository, &pool, &outbox);
    tinyimx::message::MessageApplicationService application(&adapter);
    tinyimx::message::MessageServiceImpl service(&application);
    tinyimx::message::MessageServiceServer server(&service);
    if (!server.Start("127.0.0.1:0")) {
        std::cerr << "[FAIL] temporary loopback MessageService startup\n";
        return 2;
    }
    auto endpoints = std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>(
        "", "", server.BoundTarget());
    tinyimx::rpc::MessageRpcClient client(endpoints);
    tinyimx::rpc::RpcCallOptions options;
    options.remaining_timeout = std::chrono::seconds(2);
    options.caller_service = "read-only-mysql-validation";
    options.caller_instance = "r2b1-local";
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    for (const std::uint64_t id : {398858ULL, 398888ULL, 421466ULL, 421467ULL}) {
        const auto before = repository.FindPrivateMessageById(id);
        Expect(before.Found(), "Resolve.MySQLHistoricalSampleExists", id);
        if (!before.Found()) continue;
        const auto& row = before.record;
        tinyimx::rpc::ResolvePrivateMessageRpcRequest request;
        request.from_user_id = row.from_user_id;
        request.to_user_id = row.to_user_id;
        request.client_message_id = row.client_message_id;
        request.message_type = row.message_type;
        request.content = row.content;
        const auto matched = client.ResolvePrivateMessage(request, options);
        Expect(matched.ok() && matched.value->record && matched.value->record->message_id == id &&
                   static_cast<std::uint32_t>(matched.value->record->delivery_state) == row.delivery_status,
               "Resolve.RealGrpcMySQLPreservesDurableState", id);
        auto conflict = request;
        conflict.content += "-read-only-conflict-probe";
        const auto conflicting = client.ResolvePrivateMessage(conflict, options);
        Expect(conflicting.ok() && conflicting.value->outcome ==
                   tinyimx::rpc::ResolvePrivateMessageRpcOutcome::kIdempotencyConflict &&
                   !conflicting.value->record, "Resolve.RealGrpcMySQLConflictHidesContent", id);
        auto missing = request;
        missing.client_message_id = "codex-resolve-absent-" + std::to_string(unique) + "-" + std::to_string(id);
        const auto absent = client.ResolvePrivateMessage(missing, options);
        Expect(absent.ok() && absent.value->outcome == tinyimx::rpc::ResolvePrivateMessageRpcOutcome::kNotObserved &&
                   !absent.value->record, "Resolve.RealGrpcMySQLNotObserved", id);
        const auto after = repository.FindPrivateMessageById(id);
        Expect(after.Found() && after.record.delivery_status == row.delivery_status &&
                   after.record.content == row.content && after.record.client_message_id == row.client_message_id &&
                   after.record.from_user_id == row.from_user_id && after.record.to_user_id == row.to_user_id &&
                   after.record.delivered_at == row.delivered_at && after.record.read_at == row.read_at,
               "Resolve.MySQLHistoricalRecordUnchanged", id);
        std::cout << "OBSERVED_DURABLE_STATE message_id=" << id << " state=" << row.delivery_status << '\n';
    }
    server.Shutdown();
    server.Wait();
    pool.Shutdown();
    std::cout << "failed=" << failed << " LOAD=NOT_RUN SQL_MUTATION=NOT_RUN HISTORICAL_RECOVERY=NOT_RUN\n";
    return failed == 0 ? 0 : 1;
}
