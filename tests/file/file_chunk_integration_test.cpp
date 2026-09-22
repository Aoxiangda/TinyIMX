#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/file/application/FileApplicationService.h"
#include "services/file/repository/FileRepositoryAdapter.h"
#include "services/file/storage/LocalFilesystemStorage.h"
#include "services/repository/FileRepository.h"

#include <openssl/evp.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint64_t kOwnerUserId = 10001;
constexpr std::uint64_t kChunkSize = 256ULL * 1024ULL;
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

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int size = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &size, EVP_sha256(), nullptr) != 1) return {};
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i) output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    return output.str();
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

std::uint64_t ChunkStatus(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t upload_id,
    std::uint64_t chunk_index
) {
    auto connection = pool->Acquire();
    if (!connection) return 0;
    tinyimx::MySqlQueryResult result;
    const std::string sql = "SELECT status FROM im_file_upload_chunks WHERE upload_id = " +
        std::to_string(upload_id) + " AND chunk_index = " + std::to_string(chunk_index);
    if (!connection->Query(sql, &result) || result.rows.size() != 1 || result.rows[0].size() != 1) return 0;
    return static_cast<std::uint64_t>(std::stoull(result.rows[0][0]));
}

void Cleanup(tinyimx::MySqlConnectionPool* pool, std::uint64_t file_id, std::uint64_t upload_id) {
    auto connection = pool->Acquire();
    if (!connection) return;
    if (upload_id != 0) {
        connection->Execute("DELETE FROM im_file_upload_chunks WHERE upload_id = " + std::to_string(upload_id));
        connection->Execute("DELETE FROM im_file_upload_sessions WHERE upload_id = " + std::to_string(upload_id));
    }
    if (file_id != 0) connection->Execute("DELETE FROM im_files WHERE file_id = " + std::to_string(file_id));
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];
    const std::filesystem::path storage_root = argc >= 3
        ? std::filesystem::path(argv[2])
        : std::filesystem::temp_directory_path() / "tinyimx-m18b1-integration";
    std::error_code ec;
    std::filesystem::remove_all(storage_root, ec);

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
    tinyimx::file::LocalFilesystemStorage storage(storage_root);
    tinyimx::file::FileApplicationService service(&adapter, &storage);

    tinyimx::file::BeginUploadCommand begin;
    begin.actor_user_id = kOwnerUserId;
    begin.client_upload_id = UniqueId("m18b1-chunk-");
    begin.file_name = "m18-b1.bin";
    begin.content_type = "application/octet-stream";
    begin.total_size = kChunkSize + 17;
    begin.checksum_algorithm = "sha256";
    begin.expected_checksum = std::string(64, 'a');
    begin.preferred_chunk_size = kChunkSize;
    const auto created = service.BeginUpload(begin);
    Expect(created.Accepted(), "B1 fixture BeginUpload");
    const std::uint64_t file_id = created.bundle ? created.bundle->file.file_id : 0;
    const std::uint64_t upload_id = created.bundle ? created.bundle->session.upload_id : 0;

    std::string tail(17, 'T');
    tinyimx::file::UploadChunkCommand chunk1;
    chunk1.actor_user_id = kOwnerUserId;
    chunk1.upload_id = upload_id;
    chunk1.chunk_index = 1;
    chunk1.byte_offset = kChunkSize;
    chunk1.data = tail;
    chunk1.checksum_algorithm = "sha256";
    chunk1.checksum = Sha256Hex(tail);
    const auto out_of_order = service.UploadChunk(chunk1);
    Expect(out_of_order.Accepted() && out_of_order.outcome == tinyimx::file::UploadChunkOutcome::kStored,
           "out-of-order final chunk is accepted by durable geometry");
    Expect(ChunkStatus(&pool, upload_id, 1) == 2, "chunk1 manifest transitions RESERVED->STORED");

    const auto retry = service.UploadChunk(chunk1);
    Expect(retry.Accepted() && retry.outcome == tinyimx::file::UploadChunkOutcome::kReused,
           "response-loss duplicate chunk retry is reused");
    Expect(CountRows(&pool, "im_file_upload_chunks",
                     "upload_id = " + std::to_string(upload_id) + " AND chunk_index = 1") == 1,
           "duplicate chunk leaves one durable identity");

    auto conflict = chunk1;
    conflict.data.assign(17, 'X');
    conflict.checksum = Sha256Hex(conflict.data);
    const auto conflict_result = service.UploadChunk(conflict);
    Expect(conflict_result.Completed() &&
           conflict_result.outcome == tinyimx::file::UploadChunkOutcome::kIdempotencyConflict,
           "same chunk_index with different bytes is rejected as idempotency conflict");

    auto wrong_offset = chunk1;
    wrong_offset.chunk_index = 0;
    wrong_offset.byte_offset = 1;
    wrong_offset.data.assign(static_cast<std::size_t>(kChunkSize), 'A');
    wrong_offset.checksum = Sha256Hex(wrong_offset.data);
    Expect(service.UploadChunk(wrong_offset).status == tinyimx::file::FileApplicationStatus::kInvalidArgument,
           "chunk offset mismatch is rejected");

    tinyimx::file::UploadChunkCommand chunk0;
    chunk0.actor_user_id = kOwnerUserId;
    chunk0.upload_id = upload_id;
    chunk0.chunk_index = 0;
    chunk0.byte_offset = 0;
    chunk0.data.assign(static_cast<std::size_t>(kChunkSize), 'A');
    chunk0.checksum_algorithm = "sha256";
    chunk0.checksum = Sha256Hex(chunk0.data);

    constexpr std::size_t kThreads = 4;
    std::vector<tinyimx::file::UploadChunkResult> results(kThreads);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kThreads; ++i) {
        threads.emplace_back([&, i] { results[i] = service.UploadChunk(chunk0); });
    }
    for (auto& thread : threads) thread.join();
    std::size_t stored_count = 0;
    std::size_t reused_count = 0;
    bool all_stored_state = true;
    for (const auto& result : results) {
        if (result.outcome == tinyimx::file::UploadChunkOutcome::kStored) ++stored_count;
        if (result.outcome == tinyimx::file::UploadChunkOutcome::kReused) ++reused_count;
        all_stored_state = all_stored_state && result.Accepted() && result.chunk &&
            result.chunk->status == tinyimx::file::UploadChunkStatus::kStored;
    }
    Expect(stored_count == 1 && reused_count == kThreads - 1,
           "concurrent duplicate chunk has one creator and reused losers");
    Expect(all_stored_state, "concurrent duplicate chunk converges to STORED state");
    Expect(CountRows(&pool, "im_file_upload_chunks",
                     "upload_id = " + std::to_string(upload_id)) == 2,
           "two logical chunks produce exactly two manifest rows");

    const auto chunk0_path = storage_root / ("uploads/" + std::to_string(upload_id) + "/chunks/0.part");
    Expect(std::filesystem::exists(chunk0_path) &&
           std::filesystem::file_size(chunk0_path) == kChunkSize,
           "chunk bytes exist under server-generated storage_part_key");

    // Simulate storage loss after durable STORED state. Retry must rewrite the
    // same server-generated part instead of appending or inventing identity.
    std::filesystem::remove(chunk0_path, ec);
    const auto repaired = service.UploadChunk(chunk0);
    Expect(repaired.Accepted() && repaired.outcome == tinyimx::file::UploadChunkOutcome::kReused &&
           std::filesystem::exists(chunk0_path) && ReadAll(chunk0_path) == chunk0.data,
           "duplicate retry repairs missing storage bytes from durable chunk identity");

    const auto canceled = service.CancelUpload({kOwnerUserId, upload_id});
    Expect(canceled.Completed(), "cancel fixture after chunk writes");
    const auto after_cancel = service.UploadChunk(chunk0);
    Expect(after_cancel.status == tinyimx::file::FileApplicationStatus::kFailedPrecondition,
           "canceled upload rejects further chunk writes");

    Cleanup(&pool, file_id, upload_id);
    std::filesystem::remove_all(storage_root, ec);
    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    if (g_failed == 0) {
        std::cout << "[PASS] M18-B1 durable chunk/filesystem integration\n";
        return 0;
    }
    return 1;
}
