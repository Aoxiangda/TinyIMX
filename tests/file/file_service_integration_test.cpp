#include "services/file/application/FileApplicationService.h"
#include "services/file/server/FileServiceServer.h"
#include "services/file/service/FileServiceImpl.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <cstdint>
#include <iostream>
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

    static tinyimx::file::UploadBundleView MakeBundle(
        std::uint64_t actor_user_id,
        std::uint64_t upload_id
    ) {
        tinyimx::file::UploadBundleView bundle;
        bundle.file.file_id = 7000 + upload_id;
        bundle.file.owner_user_id = actor_user_id;
        bundle.file.file_name = "report.pdf";
        bundle.file.content_type = "application/pdf";
        bundle.file.total_size = 1024;
        bundle.file.checksum_algorithm = "sha256";
        bundle.file.expected_checksum =
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
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

    std::size_t begin_calls{0};
    std::size_t get_calls{0};
    std::size_t cancel_calls{0};
    std::uint64_t last_actor{0};
    std::string last_client_upload_id;
};

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
    tinyimx::file::FileApplicationService application(&repository);
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
    begin.set_expected_checksum(
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    grpc::ClientContext begin_context;
    tinyimx::file::v1::BeginUploadResponse begin_response;
    const auto begin_status = stub->BeginUpload(&begin_context, begin, &begin_response);
    ok = Expect(
        begin_status.ok() &&
        begin_response.result() == tinyimx::file::v1::BEGIN_UPLOAD_RESULT_CREATED &&
        begin_response.session().upload_id() == 77 && repository.last_actor == 10001,
        "FileService.RealGrpcBeginUpload"
    ) && ok;

    begin.set_client_upload_id("conflict");
    grpc::ClientContext conflict_context;
    tinyimx::file::v1::BeginUploadResponse conflict_response;
    const auto conflict_status = stub->BeginUpload(&conflict_context, begin, &conflict_response);
    ok = Expect(
        conflict_status.ok() &&
        conflict_response.result() ==
            tinyimx::file::v1::BEGIN_UPLOAD_RESULT_IDEMPOTENCY_CONFLICT,
        "FileService.IdempotencyConflictBusinessOutcome"
    ) && ok;

    tinyimx::file::v1::GetUploadSessionRequest get;
    get.set_actor_user_id(10001);
    get.set_upload_id(77);
    grpc::ClientContext get_context;
    tinyimx::file::v1::GetUploadSessionResponse get_response;
    const auto get_status = stub->GetUploadSession(&get_context, get, &get_response);
    ok = Expect(
        get_status.ok() && get_response.file().owner_user_id() == 10001 &&
        get_response.session().upload_id() == 77,
        "FileService.RealGrpcGetUploadSession"
    ) && ok;

    get.set_upload_id(999);
    grpc::ClientContext missing_context;
    tinyimx::file::v1::GetUploadSessionResponse missing_response;
    const auto missing_status = stub->GetUploadSession(
        &missing_context, get, &missing_response
    );
    ok = Expect(
        missing_status.error_code() == grpc::StatusCode::NOT_FOUND,
        "FileService.NotFoundStatusMapping"
    ) && ok;

    tinyimx::file::v1::CancelUploadRequest cancel;
    cancel.set_actor_user_id(10001);
    cancel.set_upload_id(77);
    grpc::ClientContext cancel_context;
    tinyimx::file::v1::CancelUploadResponse cancel_response;
    const auto cancel_status = stub->CancelUpload(&cancel_context, cancel, &cancel_response);
    ok = Expect(
        cancel_status.ok() &&
        cancel_response.result() == tinyimx::file::v1::CANCEL_UPLOAD_RESULT_APPLIED &&
        cancel_response.file().status() == tinyimx::file::v1::FILE_STATUS_CANCELED,
        "FileService.RealGrpcCancelUpload"
    ) && ok;

    server.Shutdown();
    server.Wait();
    return ok ? 0 : 1;
}
