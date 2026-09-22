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

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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
    std::vector<tinyimx::file::UploadChunkCommand> chunks;
};

Fixture CreateFixture(
    tinyimx::file::FileApplicationService* service,
    const std::string& client_prefix,
    std::size_t full_chunks,
    std::string tail,
    bool wrong_whole_checksum = false
) {
    Fixture fixture;
    for (std::size_t i = 0; i < full_chunks; ++i) {
        fixture.whole.append(static_cast<std::size_t>(kChunkSize), static_cast<char>('A' + i));
    }
    fixture.whole += tail;

    tinyimx::file::BeginUploadCommand begin;
    begin.actor_user_id = kOwnerUserId;
    begin.client_upload_id = UniqueId(client_prefix);
    begin.file_name = client_prefix + ".bin";
    begin.content_type = "application/octet-stream";
    begin.total_size = static_cast<std::uint64_t>(fixture.whole.size());
    begin.checksum_algorithm = "sha256";
    begin.expected_checksum = wrong_whole_checksum ? std::string(64, 'a') : Sha256Hex(fixture.whole);
    begin.preferred_chunk_size = kChunkSize;
    const auto created = service->BeginUpload(begin);
    if (!created.Accepted()) return fixture;
    fixture.file_id = created.bundle->file.file_id;
    fixture.upload_id = created.bundle->session.upload_id;

    const std::uint64_t expected_count =
        1 + ((begin.total_size - 1) / begin.preferred_chunk_size);
    for (std::uint64_t index = 0; index < expected_count; ++index) {
        const std::uint64_t offset = index * begin.preferred_chunk_size;
        const std::uint64_t remaining = begin.total_size - offset;
        const std::uint64_t size = std::min(begin.preferred_chunk_size, remaining);
        tinyimx::file::UploadChunkCommand chunk;
        chunk.actor_user_id = kOwnerUserId;
        chunk.upload_id = fixture.upload_id;
        chunk.chunk_index = index;
        chunk.byte_offset = offset;
        chunk.data = fixture.whole.substr(
            static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
        chunk.checksum_algorithm = "sha256";
        chunk.checksum = Sha256Hex(chunk.data);
        fixture.chunks.push_back(std::move(chunk));
    }
    return fixture;
}

bool UploadAll(
    tinyimx::file::FileApplicationService* service,
    const Fixture& fixture
) {
    bool ok = true;
    for (const auto& chunk : fixture.chunks) {
        ok = service->UploadChunk(chunk).Accepted() && ok;
    }
    return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) config_path = argv[1];
    const std::filesystem::path storage_root = argc >= 3
        ? std::filesystem::path(argv[2])
        : std::filesystem::temp_directory_path() / "tinyimx-m18b2-finalize";
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

    std::vector<std::pair<std::uint64_t, std::uint64_t>> cleanup_ids;

    // Resume/progress: upload chunks 0 and 2 while 1 is missing.
    auto normal = CreateFixture(&service, "m18b2-normal-", 2, "TAIL-17-BYTES!!!");
    cleanup_ids.push_back({normal.file_id, normal.upload_id});
    Expect(normal.file_id != 0 && normal.upload_id != 0 && normal.chunks.size() == 3,
           "B2 normal fixture BeginUpload");
    Expect(service.UploadChunk(normal.chunks[0]).Accepted() &&
           service.UploadChunk(normal.chunks[2]).Accepted(),
           "out-of-order subset uploaded");

    // Simulate the B1 crash window: durable reservation exists but storage/mark
    // did not complete. Resume must treat RESERVED exactly like a missing chunk.
    tinyimx::file::ReserveChunkCommand reserved_gap;
    reserved_gap.actor_user_id = kOwnerUserId;
    reserved_gap.upload_id = normal.upload_id;
    reserved_gap.chunk_index = normal.chunks[1].chunk_index;
    reserved_gap.byte_offset = normal.chunks[1].byte_offset;
    reserved_gap.chunk_size = static_cast<std::uint64_t>(normal.chunks[1].data.size());
    reserved_gap.checksum_algorithm = normal.chunks[1].checksum_algorithm;
    reserved_gap.checksum = normal.chunks[1].checksum;
    const auto reserved_only = adapter.ReserveChunk(reserved_gap);
    Expect(reserved_only.Accepted() &&
           reserved_only.outcome == tinyimx::file::ReserveChunkOutcome::kCreated,
           "crash-window RESERVED chunk identity created without storage bytes");

    const auto partial_progress = service.GetUploadProgress({kOwnerUserId, normal.upload_id});
    Expect(partial_progress.Found() && partial_progress.progress->expected_chunk_count == 3 &&
           partial_progress.progress->stored_chunk_count == 2 &&
           partial_progress.progress->reserved_chunk_count == 1 &&
           partial_progress.progress->missing_ranges.size() == 1 &&
           partial_progress.progress->missing_ranges[0].start_index == 1 &&
           partial_progress.progress->missing_ranges[0].end_index == 1 &&
           !partial_progress.progress->ready_to_finalize,
           "resume derives compact missing range from durable manifest");

    const auto not_ready = service.FinalizeUpload({kOwnerUserId, normal.upload_id});
    Expect(not_ready.Completed() &&
           not_ready.outcome == tinyimx::file::FinalizeUploadOutcome::kNotReady &&
           not_ready.progress && not_ready.progress->missing_ranges.size() == 1,
           "FinalizeUpload returns NOT_READY without changing incomplete upload");
    const auto still_active = service.GetUploadSession({kOwnerUserId, normal.upload_id});
    Expect(still_active.Found() &&
           still_active.bundle->session.status == tinyimx::file::UploadSessionStatus::kActive &&
           still_active.bundle->file.status == tinyimx::file::FileStatus::kUploading,
           "incomplete finalize leaves ACTIVE/UPLOADING state");

    Expect(service.UploadChunk(normal.chunks[1]).Accepted(), "missing chunk uploaded");
    const auto ready = service.GetUploadProgress({kOwnerUserId, normal.upload_id});
    Expect(ready.Found() && ready.progress->ready_to_finalize &&
           ready.progress->stored_chunk_count == 3 && ready.progress->missing_ranges.empty(),
           "complete manifest becomes ready_to_finalize");

    // Remove one physical part after the manifest says STORED. Finalize must
    // enter durable FINALIZING/VERIFYING but never publish AVAILABLE.
    const auto missing_part_path = storage_root /
        ("uploads/" + std::to_string(normal.upload_id) + "/chunks/1.part");
    std::filesystem::remove(missing_part_path, ec);
    const auto storage_failure = service.FinalizeUpload({kOwnerUserId, normal.upload_id});
    Expect(storage_failure.status == tinyimx::file::FileApplicationStatus::kStorageError,
           "missing physical part fails finalize without false AVAILABLE");
    const auto finalizing = service.GetUploadSession({kOwnerUserId, normal.upload_id});
    Expect(finalizing.Found() &&
           finalizing.bundle->session.status == tinyimx::file::UploadSessionStatus::kFinalizing &&
           finalizing.bundle->file.status == tinyimx::file::FileStatus::kVerifying,
           "storage failure retains resumable FINALIZING/VERIFYING state");

    const auto repaired = service.UploadChunk(normal.chunks[1]);
    Expect(repaired.Accepted() &&
           repaired.outcome == tinyimx::file::UploadChunkOutcome::kReused &&
           std::filesystem::exists(missing_part_path),
           "FINALIZING exact chunk replay repairs missing storage bytes");

    const auto finalized = service.FinalizeUpload({kOwnerUserId, normal.upload_id});
    const auto final_path = storage_root / ("files/" + std::to_string(normal.file_id));
    Expect(finalized.Completed() &&
           finalized.outcome == tinyimx::file::FinalizeUploadOutcome::kCompleted &&
           finalized.bundle &&
           finalized.bundle->session.status == tinyimx::file::UploadSessionStatus::kCompleted &&
           finalized.bundle->file.status == tinyimx::file::FileStatus::kAvailable &&
           finalized.bundle->file.verified_checksum == Sha256Hex(normal.whole) &&
           std::filesystem::exists(final_path) && ReadAll(final_path) == normal.whole,
           "FinalizeUpload publishes verified final object then marks AVAILABLE");

    const auto finalize_retry = service.FinalizeUpload({kOwnerUserId, normal.upload_id});
    Expect(finalize_retry.Completed() &&
           finalize_retry.outcome == tinyimx::file::FinalizeUploadOutcome::kReused &&
           finalize_retry.bundle && finalize_retry.bundle->file.file_id == normal.file_id,
           "response-loss FinalizeUpload retry reuses durable completed result");
    Expect(service.UploadChunk(normal.chunks[0]).status ==
               tinyimx::file::FileApplicationStatus::kFailedPrecondition,
           "AVAILABLE upload fences further chunk writes");

    // Crash window: durable FINALIZING + already-published final object, but DB
    // completion has not happened. Retry through application must recover.
    auto crash = CreateFixture(&service, "m18b2-crash-", 1, "CRASH-TAIL");
    cleanup_ids.push_back({crash.file_id, crash.upload_id});
    Expect(crash.file_id != 0 && UploadAll(&service, crash), "crash-window fixture uploaded");
    const auto prepared = adapter.PrepareFinalize({kOwnerUserId, crash.upload_id});
    Expect(prepared.Completed() && prepared.snapshot &&
           prepared.outcome == tinyimx::file::FinalizePreparationOutcome::kStarted,
           "crash-window fixture durably enters FINALIZING");
    tinyimx::file::ComposeObjectRequest compose;
    if (prepared.snapshot) {
        compose.storage_key = prepared.snapshot->bundle.file.storage_key;
        compose.expected_total_size = prepared.snapshot->bundle.file.total_size;
        compose.expected_sha256 = prepared.snapshot->bundle.file.expected_checksum;
        for (const auto& chunk : prepared.snapshot->chunks) {
            compose.parts.push_back({chunk.storage_part_key, chunk.chunk_size, chunk.checksum});
        }
    }
    const auto prepublished = storage.ComposeObjectAtomically(compose);
    Expect(prepublished.Succeeded(),
           "simulate crash after final object publish and before DB completion");
    const auto crash_recovered = service.FinalizeUpload({kOwnerUserId, crash.upload_id});
    Expect(crash_recovered.Completed() &&
           crash_recovered.outcome == tinyimx::file::FinalizeUploadOutcome::kCompleted &&
           crash_recovered.bundle &&
           crash_recovered.bundle->file.status == tinyimx::file::FileStatus::kAvailable,
           "FinalizeUpload retry recovers published-object/unfinished-DB crash window");

    // Concurrent finalizers operate on immutable chunks; one durable completion
    // wins while the rest resolve the same AVAILABLE result.
    auto concurrent = CreateFixture(&service, "m18b2-concurrent-", 1, "CONCURRENT-TAIL");
    cleanup_ids.push_back({concurrent.file_id, concurrent.upload_id});
    Expect(concurrent.file_id != 0 && UploadAll(&service, concurrent),
           "concurrent finalize fixture uploaded");
    constexpr std::size_t kFinalizeThreads = 4;
    std::vector<tinyimx::file::FinalizeUploadResult> finalize_results(kFinalizeThreads);
    std::vector<std::thread> finalize_threads;
    for (std::size_t i = 0; i < kFinalizeThreads; ++i) {
        finalize_threads.emplace_back([&, i] {
            finalize_results[i] = service.FinalizeUpload({kOwnerUserId, concurrent.upload_id});
        });
    }
    for (auto& thread : finalize_threads) thread.join();
    std::size_t completed_count = 0;
    std::size_t reused_count = 0;
    bool same_available = true;
    for (const auto& result : finalize_results) {
        if (result.outcome == tinyimx::file::FinalizeUploadOutcome::kCompleted) ++completed_count;
        if (result.outcome == tinyimx::file::FinalizeUploadOutcome::kReused) ++reused_count;
        same_available = same_available && result.Completed() && result.bundle &&
            result.bundle->file.file_id == concurrent.file_id &&
            result.bundle->file.status == tinyimx::file::FileStatus::kAvailable;
    }
    Expect(completed_count == 1 && reused_count == kFinalizeThreads - 1 && same_available,
           "concurrent finalize converges to one durable completion and reused losers");

    // Whole-file expected checksum is immutable BeginUpload intent. Per-chunk
    // checks can all pass while the whole checksum is intentionally wrong.
    auto bad = CreateFixture(&service, "m18b2-bad-checksum-", 1, "BAD-WHOLE", true);
    cleanup_ids.push_back({bad.file_id, bad.upload_id});
    Expect(bad.file_id != 0 && UploadAll(&service, bad), "checksum-mismatch fixture uploaded");
    const auto mismatch = service.FinalizeUpload({kOwnerUserId, bad.upload_id});
    const auto bad_final_path = storage_root / ("files/" + std::to_string(bad.file_id));
    Expect(mismatch.Completed() &&
           mismatch.outcome == tinyimx::file::FinalizeUploadOutcome::kChecksumMismatch &&
           mismatch.bundle && mismatch.bundle->file.status == tinyimx::file::FileStatus::kFailed &&
           mismatch.bundle->session.status == tinyimx::file::UploadSessionStatus::kFinalizing &&
           mismatch.verified_checksum == Sha256Hex(bad.whole) &&
           !std::filesystem::exists(bad_final_path),
           "whole checksum mismatch records FAILED and never publishes final object");
    const auto mismatch_retry = service.FinalizeUpload({kOwnerUserId, bad.upload_id});
    Expect(mismatch_retry.Completed() &&
           mismatch_retry.outcome == tinyimx::file::FinalizeUploadOutcome::kChecksumMismatch &&
           mismatch_retry.verified_checksum == Sha256Hex(bad.whole),
           "checksum-mismatch retry reuses durable terminal failure");
    Expect(service.UploadChunk(bad.chunks[0]).status ==
               tinyimx::file::FileApplicationStatus::kFailedPrecondition,
           "FAILED file fences chunk repair/rewrite");

    auto canceled = CreateFixture(&service, "m18b2-canceled-", 0, "CANCEL-ME");
    cleanup_ids.push_back({canceled.file_id, canceled.upload_id});
    Expect(canceled.file_id != 0 && service.CancelUpload({kOwnerUserId, canceled.upload_id}).Completed(),
           "cancel finalize fixture");
    Expect(service.FinalizeUpload({kOwnerUserId, canceled.upload_id}).status ==
               tinyimx::file::FileApplicationStatus::kFailedPrecondition,
           "canceled upload fences FinalizeUpload");

    auto expired = CreateFixture(&service, "m18b2-expired-", 0, "EXPIRE-ME");
    cleanup_ids.push_back({expired.file_id, expired.upload_id});
    Expect(expired.file_id != 0, "expire finalize fixture");
    {
        auto connection = pool.Acquire();
        const bool expired_session = connection && connection->Execute(
            "UPDATE im_file_upload_sessions SET status = 5 WHERE upload_id = " +
            std::to_string(expired.upload_id));
        const bool expired_file = connection && connection->Execute(
            "UPDATE im_files SET status = 6 WHERE file_id = " +
            std::to_string(expired.file_id));
        Expect(expired_session && expired_file, "durably mark upload/file EXPIRED fixture");
    }
    Expect(service.FinalizeUpload({kOwnerUserId, expired.upload_id}).status ==
               tinyimx::file::FileApplicationStatus::kFailedPrecondition,
           "expired upload fences FinalizeUpload");

    for (const auto& [file_id, upload_id] : cleanup_ids) Cleanup(&pool, file_id, upload_id);
    std::filesystem::remove_all(storage_root, ec);
    pool.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    if (g_failed == 0) {
        std::cout << "[PASS] M18-B2 resume/finalize/filesystem integration\n";
        return 0;
    }
    return 1;
}
