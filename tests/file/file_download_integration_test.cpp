#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/file/application/FileApplicationService.h"
#include "services/file/repository/FileRepositoryAdapter.h"
#include "services/file/storage/LocalFilesystemStorage.h"
#include "services/repository/FileRepository.h"

#include <openssl/evp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::uint64_t kOwnerUserId = 10001;
constexpr std::uint64_t kOtherUserId = 10002;
constexpr std::uint64_t kChunkSize = 256ULL * 1024ULL;
constexpr std::uint64_t kRangeSize = 128ULL * 1024ULL;
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
    if (EVP_Digest(input.data(), input.size(), digest, &size, EVP_sha256(), nullptr) != 1) {
        return {};
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

void Cleanup(
    tinyimx::MySqlConnectionPool* pool,
    std::uint64_t file_id,
    std::uint64_t upload_id
) {
    auto connection = pool->Acquire();
    if (!connection) return;
    if (upload_id != 0) {
        connection->Execute(
            "DELETE FROM im_file_upload_chunks WHERE upload_id = " + std::to_string(upload_id));
        connection->Execute(
            "DELETE FROM im_file_upload_sessions WHERE upload_id = " + std::to_string(upload_id));
    }
    if (file_id != 0) {
        connection->Execute("DELETE FROM im_files WHERE file_id = " + std::to_string(file_id));
    }
}

struct Fixture {
    std::uint64_t file_id{0};
    std::uint64_t upload_id{0};
    std::string whole;
    std::string whole_sha256;
};

Fixture CreateAvailableFixture(tinyimx::file::FileApplicationService* service) {
    Fixture fixture;
    fixture.whole.assign(static_cast<std::size_t>(kChunkSize), 'A');
    fixture.whole.append(static_cast<std::size_t>(kChunkSize), 'B');
    fixture.whole += "M18-C1-DOWNLOAD-TAIL";
    fixture.whole_sha256 = Sha256Hex(fixture.whole);

    tinyimx::file::BeginUploadCommand begin;
    begin.actor_user_id = kOwnerUserId;
    begin.client_upload_id = UniqueId("m18c1-download-");
    begin.file_name = "download.bin";
    begin.content_type = "application/octet-stream";
    begin.total_size = static_cast<std::uint64_t>(fixture.whole.size());
    begin.checksum_algorithm = "sha256";
    begin.expected_checksum = fixture.whole_sha256;
    begin.preferred_chunk_size = kChunkSize;
    const auto created = service->BeginUpload(begin);
    if (!created.Accepted()) return fixture;
    fixture.file_id = created.bundle->file.file_id;
    fixture.upload_id = created.bundle->session.upload_id;

    const std::uint64_t chunk_count = 1 + ((begin.total_size - 1) / kChunkSize);
    for (std::uint64_t index = 0; index < chunk_count; ++index) {
        const std::uint64_t offset = index * kChunkSize;
        const std::uint64_t size = std::min(kChunkSize, begin.total_size - offset);
        tinyimx::file::UploadChunkCommand chunk;
        chunk.actor_user_id = kOwnerUserId;
        chunk.upload_id = fixture.upload_id;
        chunk.chunk_index = index;
        chunk.byte_offset = offset;
        chunk.data = fixture.whole.substr(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
        chunk.checksum_algorithm = "sha256";
        chunk.checksum = Sha256Hex(chunk.data);
        if (!service->UploadChunk(std::move(chunk)).Accepted()) {
            fixture.file_id = 0;
            fixture.upload_id = 0;
            return fixture;
        }
    }
    const auto finalized = service->FinalizeUpload({kOwnerUserId, fixture.upload_id});
    if (!finalized.Completed() ||
        finalized.outcome != tinyimx::file::FinalizeUploadOutcome::kCompleted) {
        fixture.file_id = 0;
        fixture.upload_id = 0;
    }
    return fixture;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];
    const std::filesystem::path storage_root = argc >= 3
        ? std::filesystem::path(argv[2])
        : std::filesystem::temp_directory_path() / "tinyimx-m18c1-download";
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

    const auto fixture = CreateAvailableFixture(&service);
    Expect(fixture.file_id != 0 && fixture.upload_id != 0,
           "C1 fixture reaches AVAILABLE through real upload/finalize path");

    const auto info = service.GetDownloadInfo({kOwnerUserId, fixture.file_id});
    Expect(info.Found() && info.info->file_id == fixture.file_id &&
           info.info->file_name == "download.bin" &&
           info.info->total_size == fixture.whole.size() &&
           info.info->checksum_algorithm == "sha256" &&
           info.info->verified_checksum == fixture.whole_sha256 &&
           !info.info->available_at.empty(),
           "owner receives safe AVAILABLE download metadata");

    const auto hidden = service.GetDownloadInfo({kOtherUserId, fixture.file_id});
    Expect(hidden.status == tinyimx::file::FileApplicationStatus::kNotFound,
           "non-owner download lookup hides file existence");

    // Independent range RPC semantics: rebuild the file with multiple calls as
    // a reconnecting client would, always binding retries to the same SHA-256.
    std::string reconstructed;
    std::uint64_t offset = 0;
    std::size_t calls = 0;
    while (offset < fixture.whole.size()) {
        tinyimx::file::ReadFileRangeQuery query;
        query.actor_user_id = kOwnerUserId;
        query.file_id = fixture.file_id;
        query.offset = offset;
        query.length = kRangeSize;
        query.if_match_sha256 = fixture.whole_sha256;
        const auto range = service.ReadFileRange(std::move(query));
        if (!range.Completed()) {
            Expect(false, "range-resume sequence completes");
            break;
        }
        Expect(range.offset == offset && range.next_offset > offset &&
               range.range_sha256 == Sha256Hex(range.data) &&
               range.info->verified_checksum == fixture.whole_sha256,
               "range response carries stable validator and payload checksum");
        reconstructed += range.data;
        offset = range.next_offset;
        ++calls;
        if (range.eof) break;
    }
    Expect(calls >= 4 && reconstructed == fixture.whole && offset == fixture.whole.size(),
           "reconnect-style range resume reconstructs exact final object");

    tinyimx::file::ReadFileRangeQuery retry;
    retry.actor_user_id = kOwnerUserId;
    retry.file_id = fixture.file_id;
    retry.offset = kRangeSize;
    retry.length = kRangeSize;
    retry.if_match_sha256 = fixture.whole_sha256;
    const auto retry_a = service.ReadFileRange(retry);
    const auto retry_b = service.ReadFileRange(retry);
    Expect(retry_a.Completed() && retry_b.Completed() && retry_a.data == retry_b.data &&
           retry_a.range_sha256 == retry_b.range_sha256 &&
           retry_a.next_offset == retry_b.next_offset,
           "same range retry is stateless and deterministic");

    retry.if_match_sha256 = std::string(64, 'f');
    Expect(service.ReadFileRange(retry).status ==
               tinyimx::file::FileApplicationStatus::kFailedPrecondition,
           "strong validator mismatch fences stale resume");
    retry.if_match_sha256 = fixture.whole_sha256;
    retry.offset = fixture.whole.size();
    Expect(service.ReadFileRange(retry).status ==
               tinyimx::file::FileApplicationStatus::kOutOfRange,
           "offset at EOF is rejected instead of returning ambiguous empty data");
    retry.offset = 0;
    retry.length = (1ULL << 20) + 1;
    Expect(service.ReadFileRange(retry).status ==
               tinyimx::file::FileApplicationStatus::kInvalidArgument,
           "range request above 1MiB application bound is rejected");

    // Downloads depend only on the immutable final object, not upload parts.
    std::filesystem::remove_all(
        storage_root / ("uploads/" + std::to_string(fixture.upload_id)), ec);
    retry.length = 64;
    const auto after_parts_removed = service.ReadFileRange(retry);
    Expect(after_parts_removed.Completed() && after_parts_removed.data == fixture.whole.substr(0, 64),
           "download reads final object independently of upload chunk parts");

    // Stateless concurrency: all readers can use any FileService instance.
    constexpr std::size_t kReaders = 4;
    std::vector<tinyimx::file::ReadFileRangeResult> concurrent(kReaders);
    std::vector<std::thread> threads;
    for (std::size_t i = 0; i < kReaders; ++i) {
        threads.emplace_back([&, i] {
            tinyimx::file::ReadFileRangeQuery query;
            query.actor_user_id = kOwnerUserId;
            query.file_id = fixture.file_id;
            query.offset = static_cast<std::uint64_t>(i * 4096);
            query.length = 4096;
            query.if_match_sha256 = fixture.whole_sha256;
            concurrent[i] = service.ReadFileRange(std::move(query));
        });
    }
    for (auto& thread : threads) thread.join();
    bool concurrent_ok = true;
    for (std::size_t i = 0; i < kReaders; ++i) {
        concurrent_ok = concurrent_ok && concurrent[i].Completed() &&
            concurrent[i].data == fixture.whole.substr(i * 4096, 4096);
    }
    Expect(concurrent_ok, "concurrent stateless range reads converge on immutable object");

    // AVAILABLE metadata with missing/size-drifted storage is DATA LOSS, never EOF.
    const auto final_path = storage_root / ("files/" + std::to_string(fixture.file_id));
    const auto backup = storage_root / "final.backup";
    std::filesystem::rename(final_path, backup, ec);
    retry.offset = 0;
    retry.length = 64;
    const auto missing_object = service.ReadFileRange(retry);
    Expect(missing_object.status == tinyimx::file::FileApplicationStatus::kInvalidRecord,
           "AVAILABLE metadata plus missing object is surfaced as data inconsistency");
    std::filesystem::rename(backup, final_path, ec);

    {
        std::ofstream truncate(final_path, std::ios::binary | std::ios::trunc);
        truncate << "short";
    }
    const auto size_drift = service.ReadFileRange(retry);
    Expect(size_drift.status == tinyimx::file::FileApplicationStatus::kInvalidRecord,
           "AVAILABLE object size drift is detected before serving bytes");

    // Owner can see a not-yet-available file but may not download it.
    tinyimx::file::BeginUploadCommand pending_begin;
    pending_begin.actor_user_id = kOwnerUserId;
    pending_begin.client_upload_id = UniqueId("m18c1-pending-");
    pending_begin.file_name = "pending.bin";
    pending_begin.content_type = "application/octet-stream";
    pending_begin.total_size = 5;
    pending_begin.checksum_algorithm = "sha256";
    pending_begin.expected_checksum = Sha256Hex("hello");
    pending_begin.preferred_chunk_size = kChunkSize;
    const auto pending = service.BeginUpload(pending_begin);
    Expect(pending.Accepted(), "pending fixture BeginUpload");
    if (pending.Accepted()) {
        Expect(service.GetDownloadInfo({kOwnerUserId, pending.bundle->file.file_id}).status ==
                   tinyimx::file::FileApplicationStatus::kFailedPrecondition,
               "owner cannot download non-AVAILABLE file");
        Cleanup(&pool, pending.bundle->file.file_id, pending.bundle->session.upload_id);
    }

    Cleanup(&pool, fixture.file_id, fixture.upload_id);
    std::filesystem::remove_all(storage_root, ec);
    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (g_failed == 0) {
        std::cout << "[PASS] M18-C1 download authorization/range-resume integration\n";
        return 0;
    }
    return 1;
}
