#include "services/file/application/FileApplicationService.h"
#include "services/file/storage/FileStoragePort.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

class FakeFileRepository final : public tinyimx::file::FileRepositoryPort {
public:
    tinyimx::file::BeginUploadResult begin_result;
    tinyimx::file::GetUploadSessionResult get_result;
    tinyimx::file::CancelUploadResult cancel_result;
    tinyimx::file::ReserveChunkResult reserve_result;
    tinyimx::file::MarkChunkStoredResult mark_result;
    tinyimx::file::GetUploadSnapshotResult snapshot_result;
    tinyimx::file::FinalizePreparationResult prepare_result;
    tinyimx::file::CompleteFinalizeResult complete_result;
    tinyimx::file::FailFinalizeChecksumResult fail_finalize_result;

    tinyimx::file::BeginUploadCommand last_begin;
    tinyimx::file::GetUploadSessionQuery last_get;
    tinyimx::file::CancelUploadCommand last_cancel;
    tinyimx::file::ReserveChunkCommand last_reserve;
    tinyimx::file::MarkChunkStoredCommand last_mark;
    tinyimx::file::GetUploadProgressQuery last_snapshot;
    tinyimx::file::FinalizeUploadCommand last_prepare;
    tinyimx::file::CompleteFinalizeCommand last_complete;

    tinyimx::file::BeginUploadResult BeginUpload(
        const tinyimx::file::BeginUploadCommand& command
    ) override { last_begin = command; return begin_result; }

    tinyimx::file::GetUploadSessionResult GetUploadSession(
        const tinyimx::file::GetUploadSessionQuery& query
    ) override { last_get = query; return get_result; }

    tinyimx::file::CancelUploadResult CancelUpload(
        const tinyimx::file::CancelUploadCommand& command
    ) override { last_cancel = command; return cancel_result; }

    tinyimx::file::ReserveChunkResult ReserveChunk(
        const tinyimx::file::ReserveChunkCommand& command
    ) override { last_reserve = command; return reserve_result; }

    tinyimx::file::MarkChunkStoredResult MarkChunkStored(
        const tinyimx::file::MarkChunkStoredCommand& command
    ) override { last_mark = command; return mark_result; }

    tinyimx::file::GetUploadSnapshotResult GetUploadSnapshot(
        const tinyimx::file::GetUploadProgressQuery& query
    ) override { last_snapshot = query; return snapshot_result; }

    tinyimx::file::FinalizePreparationResult PrepareFinalize(
        const tinyimx::file::FinalizeUploadCommand& command
    ) override { last_prepare = command; return prepare_result; }

    tinyimx::file::CompleteFinalizeResult CompleteFinalize(
        const tinyimx::file::CompleteFinalizeCommand& command
    ) override { last_complete = command; return complete_result; }

    tinyimx::file::FailFinalizeChecksumResult FailFinalizeChecksum(
        const tinyimx::file::FailFinalizeChecksumCommand&
    ) override { return fail_finalize_result; }
};

class FakeStorage final : public tinyimx::file::FileStoragePort {
public:
    tinyimx::file::StoreChunkResult StoreChunkAtomically(
        const tinyimx::file::StoreChunkRequest& request
    ) override {
        last_key = request.storage_part_key;
        last_data.assign(request.data.data(), request.data.size());
        last_checksum = request.expected_sha256;
        tinyimx::file::StoreChunkResult result;
        result.status = status;
        result.bytes_written = request.data.size();
        result.verified_sha256 = request.expected_sha256;
        result.message = "stored";
        return result;
    }

    tinyimx::file::ComposeObjectResult ComposeObjectAtomically(
        const tinyimx::file::ComposeObjectRequest& request
    ) override {
        last_compose_key = request.storage_key;
        last_compose_parts = request.parts.size();
        tinyimx::file::ComposeObjectResult result;
        result.status = compose_status;
        result.bytes_written = request.expected_total_size;
        result.verified_sha256 = request.expected_sha256;
        result.message = "composed";
        return result;
    }

    tinyimx::file::FileStorageStatus status{tinyimx::file::FileStorageStatus::kSucceeded};
    tinyimx::file::FileStorageStatus compose_status{tinyimx::file::FileStorageStatus::kSucceeded};
    std::string last_key;
    std::string last_data;
    std::string last_checksum;
    std::string last_compose_key;
    std::size_t last_compose_parts{0};
};

int Fail(const char* message) {
    std::cerr << "[FAIL] " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using namespace tinyimx::file;

    FakeFileRepository repository;
    FakeStorage storage;
    repository.begin_result.status = FileApplicationStatus::kSucceeded;
    repository.begin_result.outcome = BeginUploadOutcome::kCreated;
    repository.get_result.status = FileApplicationStatus::kSucceeded;
    repository.cancel_result.status = FileApplicationStatus::kSucceeded;
    repository.cancel_result.outcome = CancelUploadOutcome::kApplied;

    UploadChunkView reserved_chunk;
    reserved_chunk.upload_id = 77;
    reserved_chunk.file_id = 7001;
    reserved_chunk.owner_user_id = 10001;
    reserved_chunk.chunk_index = 0;
    reserved_chunk.byte_offset = 0;
    reserved_chunk.chunk_size = 5;
    reserved_chunk.checksum_algorithm = "sha256";
    reserved_chunk.checksum =
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    reserved_chunk.storage_part_key = "uploads/77/chunks/0.part";
    reserved_chunk.status = UploadChunkStatus::kReserved;
    repository.reserve_result.status = FileApplicationStatus::kSucceeded;
    repository.reserve_result.outcome = ReserveChunkOutcome::kCreated;
    repository.reserve_result.chunk = reserved_chunk;
    reserved_chunk.status = UploadChunkStatus::kStored;
    repository.mark_result.status = FileApplicationStatus::kSucceeded;
    repository.mark_result.chunk = reserved_chunk;

    UploadSnapshotView snapshot;
    snapshot.bundle.file.file_id = 7001;
    snapshot.bundle.file.owner_user_id = 10001;
    snapshot.bundle.file.file_name = "hello.bin";
    snapshot.bundle.file.content_type = "application/octet-stream";
    snapshot.bundle.file.total_size = 5;
    snapshot.bundle.file.checksum_algorithm = "sha256";
    snapshot.bundle.file.expected_checksum = reserved_chunk.checksum;
    snapshot.bundle.file.storage_backend = "local_fs";
    snapshot.bundle.file.storage_key = "files/7001";
    snapshot.bundle.file.status = FileStatus::kUploading;
    snapshot.bundle.file.version = 1;
    snapshot.bundle.session.upload_id = 77;
    snapshot.bundle.session.file_id = 7001;
    snapshot.bundle.session.owner_user_id = 10001;
    snapshot.bundle.session.client_upload_id = "upload";
    snapshot.bundle.session.total_size = 5;
    snapshot.bundle.session.chunk_size = 5;
    snapshot.bundle.session.status = UploadSessionStatus::kActive;
    snapshot.bundle.session.version = 1;
    snapshot.chunks.push_back(reserved_chunk);
    repository.snapshot_result.status = FileApplicationStatus::kSucceeded;
    repository.snapshot_result.snapshot = snapshot;

    auto finalizing_snapshot = snapshot;
    finalizing_snapshot.bundle.file.status = FileStatus::kVerifying;
    finalizing_snapshot.bundle.session.status = UploadSessionStatus::kFinalizing;
    repository.prepare_result.status = FileApplicationStatus::kSucceeded;
    repository.prepare_result.outcome = FinalizePreparationOutcome::kStarted;
    repository.prepare_result.snapshot = finalizing_snapshot;

    auto completed_bundle = snapshot.bundle;
    completed_bundle.file.status = FileStatus::kAvailable;
    completed_bundle.file.verified_checksum = reserved_chunk.checksum;
    completed_bundle.session.status = UploadSessionStatus::kCompleted;
    repository.complete_result.status = FileApplicationStatus::kSucceeded;
    repository.complete_result.outcome = CompleteFinalizeOutcome::kCompleted;
    repository.complete_result.bundle = completed_bundle;

    FileApplicationService service(&repository, &storage);

    BeginUploadCommand begin;
    begin.actor_user_id = 10001;
    begin.client_upload_id = "  upload-1  ";
    begin.file_name = "  report.pdf  ";
    begin.content_type = " Application/PDF ";
    begin.total_size = 1024;
    begin.checksum_algorithm = " SHA256 ";
    begin.expected_checksum =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    begin.preferred_chunk_size = 0;

    if (!service.BeginUpload(begin).Completed()) return Fail("valid BeginUpload rejected");
    if (repository.last_begin.client_upload_id != "upload-1" ||
        repository.last_begin.file_name != "report.pdf" ||
        repository.last_begin.content_type != "application/pdf" ||
        repository.last_begin.checksum_algorithm != "sha256" ||
        repository.last_begin.expected_checksum !=
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" ||
        repository.last_begin.preferred_chunk_size != 4ULL * 1024ULL * 1024ULL) {
        return Fail("BeginUpload normalization/default chunk size mismatch");
    }

    begin.file_name = "../secret";
    if (service.BeginUpload(begin).status != FileApplicationStatus::kInvalidArgument)
        return Fail("unsafe file_name accepted");
    begin.file_name = "report.pdf";

    begin.total_size = 0;
    if (service.BeginUpload(begin).status != FileApplicationStatus::kInvalidArgument)
        return Fail("zero total_size accepted");
    begin.total_size = 1024;

    begin.expected_checksum = "bad";
    if (service.BeginUpload(begin).status != FileApplicationStatus::kInvalidArgument)
        return Fail("invalid SHA-256 accepted");
    begin.expected_checksum =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    begin.preferred_chunk_size = 12345;
    if (service.BeginUpload(begin).status != FileApplicationStatus::kInvalidArgument)
        return Fail("invalid chunk size accepted");

    const auto get = service.GetUploadSession({10001, 77});
    if (get.status != FileApplicationStatus::kSucceeded ||
        repository.last_get.actor_user_id != 10001 || repository.last_get.upload_id != 77) {
        return Fail("GetUploadSession did not delegate correctly");
    }

    const auto cancel = service.CancelUpload({10001, 77});
    if (!cancel.Completed() || repository.last_cancel.upload_id != 77)
        return Fail("CancelUpload did not delegate correctly");

    UploadChunkCommand chunk;
    chunk.actor_user_id = 10001;
    chunk.upload_id = 77;
    chunk.chunk_index = 0;
    chunk.byte_offset = 0;
    chunk.data = "hello";
    chunk.checksum_algorithm = " SHA256 ";
    chunk.checksum =
        "2CF24DBA5FB0A30E26E83B2AC5B9E29E1B161E5C1FA7425E73043362938B9824";
    const auto uploaded = service.UploadChunk(chunk);
    if (!uploaded.Accepted() || uploaded.chunk->status != UploadChunkStatus::kStored)
        return Fail("valid UploadChunk rejected");
    if (repository.last_reserve.chunk_size != 5 ||
        repository.last_reserve.checksum_algorithm != "sha256" ||
        storage.last_key != "uploads/77/chunks/0.part" || storage.last_data != "hello" ||
        repository.last_mark.chunk_index != 0) {
        return Fail("UploadChunk did not preserve reserve->storage->mark chain");
    }

    chunk.checksum = std::string(64, 'a');
    if (service.UploadChunk(chunk).status != FileApplicationStatus::kInvalidArgument)
        return Fail("UploadChunk accepted checksum mismatch");
    chunk.checksum =
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";
    chunk.data.clear();
    if (service.UploadChunk(chunk).status != FileApplicationStatus::kInvalidArgument)
        return Fail("UploadChunk accepted empty data");

    const auto progress = service.GetUploadProgress({10001, 77});
    if (!progress.Found() || !progress.progress->ready_to_finalize ||
        progress.progress->expected_chunk_count != 1 ||
        progress.progress->stored_chunk_count != 1 ||
        !progress.progress->missing_ranges.empty()) {
        return Fail("GetUploadProgress did not derive complete durable manifest");
    }

    const auto finalized = service.FinalizeUpload({10001, 77});
    if (!finalized.Completed() || finalized.outcome != FinalizeUploadOutcome::kCompleted ||
        !finalized.bundle || finalized.bundle->file.status != FileStatus::kAvailable ||
        repository.last_prepare.upload_id != 77 || repository.last_complete.upload_id != 77 ||
        storage.last_compose_key != "files/7001" || storage.last_compose_parts != 1) {
        return Fail("FinalizeUpload did not preserve prepare->compose->complete chain");
    }

    if (service.GetUploadSession({0, 77}).status != FileApplicationStatus::kInvalidArgument)
        return Fail("GetUploadSession accepted zero actor");
    if (service.CancelUpload({10001, 0}).status != FileApplicationStatus::kInvalidArgument)
        return Fail("CancelUpload accepted zero upload_id");

    std::cout << "[PASS] M18-B2 file application progress/finalize orchestration\n";
    return 0;
}
