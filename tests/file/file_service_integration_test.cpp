#include "services/file/application/FileApplicationService.h"
#include "services/file/server/FileServiceServer.h"
#include "services/file/service/FileServiceImpl.h"
#include "services/file/storage/FileStoragePort.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <openssl/evp.h>

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <memory>
#include <string>

namespace {

class FakeFileRepository final : public tinyimx::file::FileRepositoryPort {
public:
    tinyimx::file::BeginUploadResult BeginUpload(
        const tinyimx::file::BeginUploadCommand& command
    ) override {
        ++begin_calls;
        last_actor = command.actor_user_id;
        last_client_upload_id = command.client_upload_id;
        const bool reused = command.client_upload_id == "reused";
        const bool conflict = command.client_upload_id == "conflict";
        tinyimx::file::BeginUploadResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.outcome = conflict
            ? tinyimx::file::BeginUploadOutcome::kIdempotencyConflict
            : (reused ? tinyimx::file::BeginUploadOutcome::kReused
                      : tinyimx::file::BeginUploadOutcome::kCreated);
        result.message = conflict ? "conflict" : (reused ? "reused" : "created");
        if (!conflict) result.bundle = MakeBundle(command.actor_user_id, reused ? 88 : 77);
        return result;
    }

    tinyimx::file::GetUploadSessionResult GetUploadSession(
        const tinyimx::file::GetUploadSessionQuery& query
    ) override {
        ++get_calls;
        tinyimx::file::GetUploadSessionResult result;
        if (query.upload_id == 999) {
            result.status = tinyimx::file::FileApplicationStatus::kNotFound;
            result.message = "not found";
            return result;
        }
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.bundle = MakeBundle(query.actor_user_id, query.upload_id);
        return result;
    }

    tinyimx::file::CancelUploadResult CancelUpload(
        const tinyimx::file::CancelUploadCommand& command
    ) override {
        ++cancel_calls;
        tinyimx::file::CancelUploadResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.outcome = cancel_calls == 1
            ? tinyimx::file::CancelUploadOutcome::kApplied
            : tinyimx::file::CancelUploadOutcome::kReused;
        auto bundle = MakeBundle(command.actor_user_id, command.upload_id);
        bundle.file.status = tinyimx::file::FileStatus::kCanceled;
        bundle.session.status = tinyimx::file::UploadSessionStatus::kCanceled;
        result.bundle = bundle;
        result.message = cancel_calls == 1 ? "canceled" : "already canceled";
        return result;
    }

    tinyimx::file::ReserveChunkResult ReserveChunk(
        const tinyimx::file::ReserveChunkCommand& command
    ) override {
        last_chunk_actor = command.actor_user_id;
        tinyimx::file::UploadChunkView chunk;
        chunk.upload_id = command.upload_id;
        chunk.chunk_index = command.chunk_index;
        chunk.file_id = 7077;
        chunk.owner_user_id = command.actor_user_id;
        chunk.byte_offset = command.byte_offset;
        chunk.chunk_size = command.chunk_size;
        chunk.checksum_algorithm = command.checksum_algorithm;
        chunk.checksum = command.checksum;
        chunk.storage_part_key = "uploads/" + std::to_string(command.upload_id) +
                                 "/chunks/" + std::to_string(command.chunk_index) + ".part";
        chunk.status = tinyimx::file::UploadChunkStatus::kReserved;
        tinyimx::file::ReserveChunkResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.outcome = chunk_calls++ == 0
            ? tinyimx::file::ReserveChunkOutcome::kCreated
            : tinyimx::file::ReserveChunkOutcome::kReused;
        result.chunk = chunk;
        return result;
    }

    tinyimx::file::MarkChunkStoredResult MarkChunkStored(
        const tinyimx::file::MarkChunkStoredCommand& command
    ) override {
        tinyimx::file::UploadChunkView chunk;
        chunk.upload_id = command.upload_id;
        chunk.chunk_index = command.chunk_index;
        chunk.file_id = 7077;
        chunk.owner_user_id = 10001;
        chunk.chunk_size = command.chunk_size;
        chunk.checksum_algorithm = "sha256";
        chunk.checksum = command.checksum;
        chunk.storage_part_key = "uploads/" + std::to_string(command.upload_id) +
                                 "/chunks/" + std::to_string(command.chunk_index) + ".part";
        chunk.status = tinyimx::file::UploadChunkStatus::kStored;
        tinyimx::file::MarkChunkStoredResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.chunk = chunk;
        return result;
    }

    tinyimx::file::GetUploadSnapshotResult GetUploadSnapshot(
        const tinyimx::file::GetUploadProgressQuery& query
    ) override {
        tinyimx::file::GetUploadSnapshotResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.snapshot = MakeSnapshot(query.actor_user_id, query.upload_id, false);
        return result;
    }

    tinyimx::file::FinalizePreparationResult PrepareFinalize(
        const tinyimx::file::FinalizeUploadCommand& command
    ) override {
        tinyimx::file::FinalizePreparationResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.outcome = tinyimx::file::FinalizePreparationOutcome::kStarted;
        result.snapshot = MakeSnapshot(command.actor_user_id, command.upload_id, true);
        return result;
    }

    tinyimx::file::CompleteFinalizeResult CompleteFinalize(
        const tinyimx::file::CompleteFinalizeCommand& command
    ) override {
        auto bundle = MakeBundle(command.actor_user_id, command.upload_id);
        bundle.file.status = tinyimx::file::FileStatus::kAvailable;
        bundle.file.verified_checksum = command.verified_checksum;
        bundle.session.status = tinyimx::file::UploadSessionStatus::kCompleted;
        tinyimx::file::CompleteFinalizeResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.outcome = tinyimx::file::CompleteFinalizeOutcome::kCompleted;
        result.bundle = bundle;
        return result;
    }

    tinyimx::file::FailFinalizeChecksumResult FailFinalizeChecksum(
        const tinyimx::file::FailFinalizeChecksumCommand& command
    ) override {
        auto bundle = MakeBundle(command.actor_user_id, command.upload_id);
        bundle.file.status = tinyimx::file::FileStatus::kFailed;
        bundle.file.verified_checksum = command.actual_checksum;
        bundle.session.status = tinyimx::file::UploadSessionStatus::kFinalizing;
        tinyimx::file::FailFinalizeChecksumResult result;
        result.status = tinyimx::file::FileApplicationStatus::kSucceeded;
        result.bundle = bundle;
        return result;
    }

    static tinyimx::file::UploadBundleView MakeBundle(
        std::uint64_t actor_user_id, std::uint64_t upload_id
    ) {
        tinyimx::file::UploadBundleView bundle;
        bundle.file.file_id = 7000 + upload_id;
        bundle.file.owner_user_id = actor_user_id;
        bundle.file.file_name = "report.pdf";
        bundle.file.content_type = "application/pdf";
        bundle.file.total_size = 1024;
        bundle.file.checksum_algorithm = "sha256";
        bundle.file.expected_checksum = std::string(64, 'a');
        bundle.file.storage_backend = "local_fs";
        bundle.file.storage_key = "files/" + std::to_string(bundle.file.file_id);
        bundle.file.status = tinyimx::file::FileStatus::kUploading;
        bundle.file.version = 1;
        bundle.session.upload_id = upload_id;
        bundle.session.file_id = bundle.file.file_id;
        bundle.session.owner_user_id = actor_user_id;
        bundle.session.client_upload_id = "upload";
        bundle.session.total_size = bundle.file.total_size;
        bundle.session.chunk_size = 4ULL * 1024ULL * 1024ULL;
        bundle.session.status = tinyimx::file::UploadSessionStatus::kActive;
        bundle.session.version = 1;
        return bundle;
    }

    static tinyimx::file::UploadSnapshotView MakeSnapshot(
        std::uint64_t actor_user_id,
        std::uint64_t upload_id,
        bool finalizing
    ) {
        tinyimx::file::UploadSnapshotView snapshot;
        snapshot.bundle = MakeBundle(actor_user_id, upload_id);
        if (finalizing) {
            snapshot.bundle.file.status = tinyimx::file::FileStatus::kVerifying;
            snapshot.bundle.session.status = tinyimx::file::UploadSessionStatus::kFinalizing;
        }
        tinyimx::file::UploadChunkView chunk;
        chunk.upload_id = upload_id;
        chunk.chunk_index = 0;
        chunk.file_id = snapshot.bundle.file.file_id;
        chunk.owner_user_id = actor_user_id;
        chunk.byte_offset = 0;
        chunk.chunk_size = 1024;
        chunk.checksum_algorithm = "sha256";
        chunk.checksum = std::string(64, 'b');
        chunk.storage_part_key = "uploads/" + std::to_string(upload_id) + "/chunks/0.part";
        chunk.status = tinyimx::file::UploadChunkStatus::kStored;
        snapshot.chunks.push_back(chunk);
        return snapshot;
    }

    std::size_t begin_calls{0};
    std::size_t get_calls{0};
    std::size_t cancel_calls{0};
    std::size_t chunk_calls{0};
    std::uint64_t last_actor{0};
    std::uint64_t last_chunk_actor{0};
    std::string last_client_upload_id;
};

class FakeStorage final : public tinyimx::file::FileStoragePort {
public:
    tinyimx::file::StoreChunkResult StoreChunkAtomically(
        const tinyimx::file::StoreChunkRequest& request
    ) override {
        tinyimx::file::StoreChunkResult result;
        result.status = tinyimx::file::FileStorageStatus::kSucceeded;
        result.bytes_written = request.data.size();
        result.verified_sha256 = request.expected_sha256;
        result.message = "stored";
        return result;
    }

    tinyimx::file::ComposeObjectResult ComposeObjectAtomically(
        const tinyimx::file::ComposeObjectRequest& request
    ) override {
        tinyimx::file::ComposeObjectResult result;
        result.status = tinyimx::file::FileStorageStatus::kSucceeded;
        result.bytes_written = request.expected_total_size;
        result.verified_sha256 = request.expected_sha256;
        result.message = "composed";
        return result;
    }
};


std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int size = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &size, EVP_sha256(), nullptr) != 1) return {};
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i) output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    return output.str();
}

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

}  // namespace

int main() {
    FakeFileRepository repository;
    FakeStorage storage;
    tinyimx::file::FileApplicationService application(&repository, &storage);
    tinyimx::file::FileServiceImpl service_impl(&application);
    tinyimx::file::FileServiceServer server(&service_impl);

    if (!server.Start("127.0.0.1:0")) {
        std::cerr << "[FAIL] FileService server start\n";
        return 1;
    }

    auto channel = grpc::CreateChannel(server.BoundTarget(), grpc::InsecureChannelCredentials());
    auto stub = tinyimx::file::v1::FileService::NewStub(channel);
    bool ok = true;

    tinyimx::file::v1::BeginUploadRequest begin;
    begin.set_actor_user_id(10001);
    begin.set_client_upload_id("created");
    begin.set_file_name("report.pdf");
    begin.set_content_type("application/pdf");
    begin.set_total_size(1024);
    begin.set_checksum_algorithm("sha256");
    begin.set_expected_checksum(std::string(64, 'a'));

    grpc::ClientContext begin_context;
    tinyimx::file::v1::BeginUploadResponse begin_response;
    const auto begin_status = stub->BeginUpload(&begin_context, begin, &begin_response);
    ok = Expect(begin_status.ok() &&
        begin_response.result() == tinyimx::file::v1::BEGIN_UPLOAD_RESULT_CREATED &&
        begin_response.session().upload_id() == 77 && repository.last_actor == 10001,
        "FileService.RealGrpcBeginUpload") && ok;

    begin.set_client_upload_id("conflict");
    grpc::ClientContext conflict_context;
    tinyimx::file::v1::BeginUploadResponse conflict_response;
    const auto conflict_status = stub->BeginUpload(&conflict_context, begin, &conflict_response);
    ok = Expect(conflict_status.ok() && conflict_response.result() ==
        tinyimx::file::v1::BEGIN_UPLOAD_RESULT_IDEMPOTENCY_CONFLICT,
        "FileService.IdempotencyConflictBusinessOutcome") && ok;

    tinyimx::file::v1::UploadChunkRequest chunk;
    chunk.set_actor_user_id(10001);
    chunk.set_upload_id(77);
    chunk.set_chunk_index(0);
    chunk.set_byte_offset(0);
    chunk.set_data("hello");
    chunk.set_checksum_algorithm("sha256");
    chunk.set_checksum("2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    grpc::ClientContext chunk_context;
    tinyimx::file::v1::UploadChunkResponse chunk_response;
    const auto chunk_status = stub->UploadChunk(&chunk_context, chunk, &chunk_response);
    ok = Expect(chunk_status.ok() &&
        chunk_response.result() == tinyimx::file::v1::UPLOAD_CHUNK_RESULT_STORED &&
        chunk_response.chunk().status() == tinyimx::file::v1::UPLOAD_CHUNK_STATUS_STORED &&
        repository.last_chunk_actor == 10001,
        "FileService.RealGrpcUploadChunk") && ok;

    grpc::ClientContext retry_context;
    tinyimx::file::v1::UploadChunkResponse retry_response;
    const auto retry_status = stub->UploadChunk(&retry_context, chunk, &retry_response);
    ok = Expect(retry_status.ok() &&
        retry_response.result() == tinyimx::file::v1::UPLOAD_CHUNK_RESULT_REUSED,
        "FileService.RealGrpcUploadChunkRetry") && ok;

    // Prove the server no longer has gRPC's usual ~4MiB receive ceiling for
    // B1 chunks. This remains bounded by the application 8MiB limit.
    tinyimx::file::v1::UploadChunkRequest large_chunk;
    large_chunk.set_actor_user_id(10001);
    large_chunk.set_upload_id(77);
    large_chunk.set_chunk_index(1);
    large_chunk.set_byte_offset(5);
    std::string large_payload(5ULL * 1024ULL * 1024ULL, 'L');
    large_chunk.set_data(large_payload);
    large_chunk.set_checksum_algorithm("sha256");
    large_chunk.set_checksum(Sha256Hex(large_payload));
    grpc::ClientContext large_context;
    tinyimx::file::v1::UploadChunkResponse large_response;
    const auto large_status = stub->UploadChunk(&large_context, large_chunk, &large_response);
    ok = Expect(large_status.ok() &&
        large_response.result() == tinyimx::file::v1::UPLOAD_CHUNK_RESULT_REUSED,
        "FileService.RealGrpcChunkAbove4MiB") && ok;

    tinyimx::file::v1::GetUploadSessionRequest get;
    get.set_actor_user_id(10001);
    get.set_upload_id(77);
    grpc::ClientContext get_context;
    tinyimx::file::v1::GetUploadSessionResponse get_response;
    const auto get_status = stub->GetUploadSession(&get_context, get, &get_response);
    ok = Expect(get_status.ok() && get_response.file().owner_user_id() == 10001 &&
        get_response.session().upload_id() == 77, "FileService.RealGrpcGetUploadSession") && ok;

    tinyimx::file::v1::GetUploadProgressRequest progress_request;
    progress_request.set_actor_user_id(10001);
    progress_request.set_upload_id(77);
    grpc::ClientContext progress_context;
    tinyimx::file::v1::GetUploadProgressResponse progress_response;
    const auto progress_status = stub->GetUploadProgress(
        &progress_context, progress_request, &progress_response
    );
    ok = Expect(progress_status.ok() &&
        progress_response.progress().expected_chunk_count() == 1 &&
        progress_response.progress().stored_chunk_count() == 1 &&
        progress_response.progress().missing_ranges_size() == 0 &&
        progress_response.progress().ready_to_finalize(),
        "FileService.RealGrpcGetUploadProgress") && ok;

    tinyimx::file::v1::FinalizeUploadRequest finalize_request;
    finalize_request.set_actor_user_id(10001);
    finalize_request.set_upload_id(77);
    grpc::ClientContext finalize_context;
    tinyimx::file::v1::FinalizeUploadResponse finalize_response;
    const auto finalize_status = stub->FinalizeUpload(
        &finalize_context, finalize_request, &finalize_response
    );
    ok = Expect(finalize_status.ok() &&
        finalize_response.result() == tinyimx::file::v1::FINALIZE_UPLOAD_RESULT_COMPLETED &&
        finalize_response.file().status() == tinyimx::file::v1::FILE_STATUS_AVAILABLE &&
        finalize_response.session().status() == tinyimx::file::v1::UPLOAD_SESSION_STATUS_COMPLETED &&
        finalize_response.verified_checksum() == std::string(64, 'a'),
        "FileService.RealGrpcFinalizeUpload") && ok;

    get.set_upload_id(999);
    grpc::ClientContext missing_context;
    tinyimx::file::v1::GetUploadSessionResponse missing_response;
    const auto missing_status = stub->GetUploadSession(&missing_context, get, &missing_response);
    ok = Expect(missing_status.error_code() == grpc::StatusCode::NOT_FOUND,
                "FileService.NotFoundStatusMapping") && ok;

    tinyimx::file::v1::CancelUploadRequest cancel;
    cancel.set_actor_user_id(10001);
    cancel.set_upload_id(77);
    grpc::ClientContext cancel_context;
    tinyimx::file::v1::CancelUploadResponse cancel_response;
    const auto cancel_status = stub->CancelUpload(&cancel_context, cancel, &cancel_response);
    ok = Expect(cancel_status.ok() &&
        cancel_response.result() == tinyimx::file::v1::CANCEL_UPLOAD_RESULT_APPLIED &&
        cancel_response.file().status() == tinyimx::file::v1::FILE_STATUS_CANCELED,
        "FileService.RealGrpcCancelUpload") && ok;

    server.Shutdown();
    server.Wait();
    return ok ? 0 : 1;
}
