#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/file/application/FileApplicationService.h"
#include "services/file/repository/FileRepositoryAdapter.h"
#include "services/repository/FileRepository.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kOwnerUserId = 10001;
int g_failed = 0;

void Expect(bool condition, const std::string& name) {
    if (condition) std::cout << "[PASS] " << name << '\n';
    else { std::cerr << "[FAIL] " << name << '\n'; ++g_failed; }
}

std::string UniqueId(const std::string& prefix) {
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix + std::to_string(static_cast<long long>(nanos));
}

bool TableExists(tinyimx::MySqlConnectionPool* pool, const std::string& table) {
    auto connection = pool->Acquire();
    if (!connection) return false;
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
    if (!connection) return 0;
    tinyimx::MySqlQueryResult result;
    if (!connection->Query("SELECT COUNT(*) FROM " + table + " WHERE " + where_clause, &result) ||
        result.rows.size() != 1 || result.rows.front().size() != 1) return 0;
    return static_cast<std::uint64_t>(std::stoull(result.rows.front().front()));
}

void Cleanup(tinyimx::MySqlConnectionPool* pool, std::uint64_t file_id) {
    if (file_id == 0) return;
    auto connection = pool->Acquire();
    if (!connection) return;
    connection->Execute(
        "DELETE FROM im_file_upload_sessions WHERE file_id = " + std::to_string(file_id));
    connection->Execute("DELETE FROM im_files WHERE file_id = " + std::to_string(file_id));
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path) || !config.MySql().enable) {
        std::cerr << "[FAIL] load MySQL-enabled config\n";
        return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 1;

    tinyimx::MySqlConnectionPool pool;
    if (!pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] mysql pool init\n";
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::FileRepository repository(&pool);
    tinyimx::file::FileRepositoryAdapter adapter(&repository, &pool);
    tinyimx::file::FileApplicationService service(&adapter);

    Expect(TableExists(&pool, "im_files"), "im_files schema exists");
    Expect(TableExists(&pool, "im_file_upload_sessions"), "upload session schema exists");

    tinyimx::file::BeginUploadCommand command;
    command.actor_user_id = kOwnerUserId;
    command.client_upload_id = UniqueId("m18a1-upload-");
    command.file_name = "m18-a1.txt";
    command.content_type = "text/plain";
    command.total_size = 4096;
    command.checksum_algorithm = "sha256";
    command.expected_checksum =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    const auto created = service.BeginUpload(command);
    Expect(created.Accepted() && created.outcome == tinyimx::file::BeginUploadOutcome::kCreated,
           "BeginUpload atomically creates file+session");
    const std::uint64_t file_id = created.bundle ? created.bundle->file.file_id : 0;
    const std::uint64_t upload_id = created.bundle ? created.bundle->session.upload_id : 0;
    Expect(file_id != 0 && upload_id != 0, "BeginUpload returns durable identities");
    Expect(created.bundle && created.bundle->file.storage_key == "files/" + std::to_string(file_id),
           "storage_key is server-generated from file_id");

    const auto fetched = service.GetUploadSession({kOwnerUserId, upload_id});
    Expect(fetched.Found() && fetched.bundle && fetched.bundle->file.file_id == file_id,
           "GetUploadSession returns owner-scoped durable bundle");

    auto missing_user = command;
    missing_user.actor_user_id = 999999999ULL;
    missing_user.client_upload_id = UniqueId("m18a1-missing-user-");
    const auto missing_user_result = service.BeginUpload(missing_user);
    Expect(missing_user_result.status == tinyimx::file::FileApplicationStatus::kNotFound,
           "BeginUpload rejects nonexistent owner user");

    const auto reused = service.BeginUpload(command);
    Expect(reused.Accepted() && reused.outcome == tinyimx::file::BeginUploadOutcome::kReused &&
           reused.bundle && reused.bundle->file.file_id == file_id,
           "response-loss retry reuses durable upload");

    auto conflict_command = command;
    conflict_command.file_name = "different.txt";
    const auto conflict = service.BeginUpload(conflict_command);
    Expect(conflict.Completed() &&
           conflict.outcome == tinyimx::file::BeginUploadOutcome::kIdempotencyConflict,
           "client_upload_id payload change is rejected");

    Expect(CountRows(&pool, "im_files", "file_id = " + std::to_string(file_id)) == 1,
           "one durable file row exists");
    Expect(CountRows(&pool, "im_file_upload_sessions",
                     "owner_user_id = " + std::to_string(kOwnerUserId) +
                     " AND client_upload_id = '" + command.client_upload_id + "'") == 1,
           "one durable upload session exists");

    // Concurrent same-key BeginUpload: one created, all others reused, and no
    // speculative file rows survive transaction rollback.
    auto concurrent = command;
    concurrent.client_upload_id = UniqueId("m18a1-concurrent-");
    concurrent.file_name = "m18-a1-concurrent.txt";
    constexpr std::size_t kThreads = 4;
    std::vector<tinyimx::file::BeginUploadResult> results(kThreads);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i]() { results[i] = service.BeginUpload(concurrent); });
    }
    for (auto& thread : threads) thread.join();

    std::size_t created_count = 0;
    std::size_t reused_count = 0;
    std::uint64_t concurrent_file_id = 0;
    bool same_identity = true;
    for (const auto& result : results) {
        if (result.outcome == tinyimx::file::BeginUploadOutcome::kCreated) ++created_count;
        if (result.outcome == tinyimx::file::BeginUploadOutcome::kReused) ++reused_count;
        if (!result.bundle) { same_identity = false; continue; }
        if (concurrent_file_id == 0) concurrent_file_id = result.bundle->file.file_id;
        else if (concurrent_file_id != result.bundle->file.file_id) same_identity = false;
    }
    Expect(created_count == 1, "concurrent duplicate BeginUpload has one creator");
    Expect(reused_count == kThreads - 1, "concurrent duplicate BeginUpload reuses losers");
    Expect(same_identity && concurrent_file_id != 0,
           "concurrent duplicate BeginUpload converges to one file identity");
    Expect(CountRows(&pool, "im_file_upload_sessions",
                     "owner_user_id = " + std::to_string(kOwnerUserId) +
                     " AND client_upload_id = '" + concurrent.client_upload_id + "'") == 1,
           "concurrent BeginUpload leaves one durable session");
    Expect(CountRows(&pool, "im_files",
                     "owner_user_id = " + std::to_string(kOwnerUserId) +
                     " AND file_name = 'm18-a1-concurrent.txt'") == 1,
           "concurrent loser transactions leave no orphan file rows");

    const auto canceled = service.CancelUpload({kOwnerUserId, upload_id});
    Expect(canceled.Completed() &&
           canceled.outcome == tinyimx::file::CancelUploadOutcome::kApplied &&
           canceled.bundle && canceled.bundle->file.status == tinyimx::file::FileStatus::kCanceled &&
           canceled.bundle->session.status == tinyimx::file::UploadSessionStatus::kCanceled,
           "CancelUpload atomically cancels session+file");
    const auto canceled_again = service.CancelUpload({kOwnerUserId, upload_id});
    Expect(canceled_again.Completed() &&
           canceled_again.outcome == tinyimx::file::CancelUploadOutcome::kReused,
           "duplicate CancelUpload is idempotent");

    auto expired_command = command;
    expired_command.client_upload_id = UniqueId("m18a1-expired-");
    expired_command.file_name = "expired.txt";
    const auto expired_created = service.BeginUpload(expired_command);
    const std::uint64_t expired_file_id =
        expired_created.bundle ? expired_created.bundle->file.file_id : 0;
    const std::uint64_t expired_upload_id =
        expired_created.bundle ? expired_created.bundle->session.upload_id : 0;
    if (expired_file_id != 0 && expired_upload_id != 0) {
        auto connection = pool.Acquire();
        if (connection) {
            connection->Execute(
                "UPDATE im_file_upload_sessions SET status = 5 WHERE upload_id = " +
                std::to_string(expired_upload_id));
            connection->Execute(
                "UPDATE im_files SET status = 6 WHERE file_id = " +
                std::to_string(expired_file_id));
        }
        const auto expired_cancel = service.CancelUpload({kOwnerUserId, expired_upload_id});
        Expect(expired_cancel.status == tinyimx::file::FileApplicationStatus::kFailedPrecondition,
               "expired upload rejects CancelUpload transition");
    } else {
        Expect(false, "expired upload fixture created");
    }

    Cleanup(&pool, file_id);
    Cleanup(&pool, concurrent_file_id);
    Cleanup(&pool, expired_file_id);
    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (g_failed == 0) {
        std::cout << "[PASS] M18-A1 file repository integration\n";
        return 0;
    }
    return 1;
}
