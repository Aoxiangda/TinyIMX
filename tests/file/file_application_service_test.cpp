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

    tinyimx::file::BeginUploadCommand last_begin;
    tinyimx::file::GetUploadSessionQuery last_get;
    tinyimx::file::CancelUploadCommand last_cancel;
    tinyimx::file::ReserveChunkCommand last_reserve;
    tinyimx::file::MarkChunkStoredCommand last_mark;

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

    tinyimx::file::FileStorageStatus status{tinyimx::file::FileStorageStatus::kSucceeded};
    std::string last_key;
    std::string last_data;
    std::string last_checksum;
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

    if (service.GetUploadSession({0, 77}).status != FileApplicationStatus::kInvalidArgument)
        return Fail("GetUploadSession accepted zero actor");
    if (service.CancelUpload({10001, 0}).status != FileApplicationStatus::kInvalidArgument)
        return Fail("CancelUpload accepted zero upload_id");

    std::cout << "[PASS] M18-B1 file application validation/storage orchestration\n";
    return 0;
}
