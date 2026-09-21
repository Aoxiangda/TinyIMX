#include "services/file/application/FileApplicationService.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

class FakeFileRepository final : public tinyimx::file::FileRepositoryPort {
public:
    tinyimx::file::BeginUploadResult begin_result;
    tinyimx::file::GetUploadSessionResult get_result;
    tinyimx::file::CancelUploadResult cancel_result;

    tinyimx::file::BeginUploadCommand last_begin;
    tinyimx::file::GetUploadSessionQuery last_get;
    tinyimx::file::CancelUploadCommand last_cancel;

    tinyimx::file::BeginUploadResult BeginUpload(
        const tinyimx::file::BeginUploadCommand& command
    ) override {
        last_begin = command;
        return begin_result;
    }

    tinyimx::file::GetUploadSessionResult GetUploadSession(
        const tinyimx::file::GetUploadSessionQuery& query
    ) override {
        last_get = query;
        return get_result;
    }

    tinyimx::file::CancelUploadResult CancelUpload(
        const tinyimx::file::CancelUploadCommand& command
    ) override {
        last_cancel = command;
        return cancel_result;
    }
};

int Fail(const char* message) {
    std::cerr << "[FAIL] " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using namespace tinyimx::file;

    FakeFileRepository repository;
    repository.begin_result.status = FileApplicationStatus::kSucceeded;
    repository.begin_result.outcome = BeginUploadOutcome::kCreated;
    repository.get_result.status = FileApplicationStatus::kSucceeded;
    repository.cancel_result.status = FileApplicationStatus::kSucceeded;
    repository.cancel_result.outcome = CancelUploadOutcome::kApplied;

    FileApplicationService service(&repository);

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

    if (service.GetUploadSession({0, 77}).status != FileApplicationStatus::kInvalidArgument)
        return Fail("GetUploadSession accepted zero actor");
    if (service.CancelUpload({10001, 0}).status != FileApplicationStatus::kInvalidArgument)
        return Fail("CancelUpload accepted zero upload_id");

    std::cout << "[PASS] M18-A1 file application validation/delegation\n";
    return 0;
}
