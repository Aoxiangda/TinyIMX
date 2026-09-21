#include "services/file/service/FileServiceImpl.h"

#include <grpcpp/grpcpp.h>

#include <string>
#include <utility>

namespace tinyimx::file {
namespace {

grpc::Status MapApplicationFailure(
    FileApplicationStatus status,
    const std::string& message
) {
    switch (status) {
        case FileApplicationStatus::kInvalidArgument:
            return {grpc::StatusCode::INVALID_ARGUMENT, message};
        case FileApplicationStatus::kNotFound:
            return {grpc::StatusCode::NOT_FOUND, message};
        case FileApplicationStatus::kPermissionDenied:
            return {grpc::StatusCode::PERMISSION_DENIED, message};
        case FileApplicationStatus::kFailedPrecondition:
            return {grpc::StatusCode::FAILED_PRECONDITION, message};
        case FileApplicationStatus::kInvalidRecord:
            return {grpc::StatusCode::DATA_LOSS, message};
        case FileApplicationStatus::kStorageError:
            return {grpc::StatusCode::UNAVAILABLE, message};
        case FileApplicationStatus::kSucceeded:
            return grpc::Status::OK;
    }
    return {grpc::StatusCode::INTERNAL, "unknown FileService application status"};
}

tinyimx::file::v1::FileStatus ToProtoStatus(FileStatus status) {
    switch (status) {
        case FileStatus::kUploading: return tinyimx::file::v1::FILE_STATUS_UPLOADING;
        case FileStatus::kVerifying: return tinyimx::file::v1::FILE_STATUS_VERIFYING;
        case FileStatus::kAvailable: return tinyimx::file::v1::FILE_STATUS_AVAILABLE;
        case FileStatus::kFailed: return tinyimx::file::v1::FILE_STATUS_FAILED;
        case FileStatus::kCanceled: return tinyimx::file::v1::FILE_STATUS_CANCELED;
        case FileStatus::kExpired: return tinyimx::file::v1::FILE_STATUS_EXPIRED;
        case FileStatus::kDeleted: return tinyimx::file::v1::FILE_STATUS_DELETED;
    }
    return tinyimx::file::v1::FILE_STATUS_UNSPECIFIED;
}

tinyimx::file::v1::UploadSessionStatus ToProtoStatus(UploadSessionStatus status) {
    switch (status) {
        case UploadSessionStatus::kActive:
            return tinyimx::file::v1::UPLOAD_SESSION_STATUS_ACTIVE;
        case UploadSessionStatus::kFinalizing:
            return tinyimx::file::v1::UPLOAD_SESSION_STATUS_FINALIZING;
        case UploadSessionStatus::kCompleted:
            return tinyimx::file::v1::UPLOAD_SESSION_STATUS_COMPLETED;
        case UploadSessionStatus::kCanceled:
            return tinyimx::file::v1::UPLOAD_SESSION_STATUS_CANCELED;
        case UploadSessionStatus::kExpired:
            return tinyimx::file::v1::UPLOAD_SESSION_STATUS_EXPIRED;
    }
    return tinyimx::file::v1::UPLOAD_SESSION_STATUS_UNSPECIFIED;
}

void FillProtoFile(const FileView& input, tinyimx::file::v1::FileRecord* output) {
    if (output == nullptr) return;
    output->set_file_id(input.file_id);
    output->set_owner_user_id(input.owner_user_id);
    output->set_file_name(input.file_name);
    output->set_content_type(input.content_type);
    output->set_total_size(input.total_size);
    output->set_checksum_algorithm(input.checksum_algorithm);
    output->set_expected_checksum(input.expected_checksum);
    output->set_verified_checksum(input.verified_checksum);
    output->set_storage_backend(input.storage_backend);
    output->set_storage_key(input.storage_key);
    output->set_status(ToProtoStatus(input.status));
    output->set_version(input.version);
    output->set_created_at(input.created_at);
    output->set_updated_at(input.updated_at);
    output->set_available_at(input.available_at);
    output->set_expires_at(input.expires_at);
}

void FillProtoSession(
    const UploadSessionView& input,
    tinyimx::file::v1::UploadSessionRecord* output
) {
    if (output == nullptr) return;
    output->set_upload_id(input.upload_id);
    output->set_file_id(input.file_id);
    output->set_owner_user_id(input.owner_user_id);
    output->set_client_upload_id(input.client_upload_id);
    output->set_total_size(input.total_size);
    output->set_chunk_size(input.chunk_size);
    output->set_status(ToProtoStatus(input.status));
    output->set_version(input.version);
    output->set_created_at(input.created_at);
    output->set_updated_at(input.updated_at);
    output->set_expires_at(input.expires_at);
    output->set_completed_at(input.completed_at);
}

void FillBundle(
    const UploadBundleView& bundle,
    tinyimx::file::v1::FileRecord* file,
    tinyimx::file::v1::UploadSessionRecord* session
) {
    FillProtoFile(bundle.file, file);
    FillProtoSession(bundle.session, session);
}

}  // namespace

FileServiceImpl::FileServiceImpl(FileApplicationService* application_service)
    : application_service_(application_service) {
}

grpc::Status FileServiceImpl::BeginUpload(
    grpc::ServerContext* context,
    const tinyimx::file::v1::BeginUploadRequest* request,
    tinyimx::file::v1::BeginUploadResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid BeginUpload RPC arguments"};
    }
    if (application_service_ == nullptr) {
        return {grpc::StatusCode::UNAVAILABLE, "FileService application service is unavailable"};
    }

    BeginUploadCommand command;
    command.actor_user_id = request->actor_user_id();
    command.client_upload_id = request->client_upload_id();
    command.file_name = request->file_name();
    command.content_type = request->content_type();
    command.total_size = request->total_size();
    command.checksum_algorithm = request->checksum_algorithm();
    command.expected_checksum = request->expected_checksum();
    command.preferred_chunk_size = request->preferred_chunk_size();

    const auto result = application_service_->BeginUpload(std::move(command));
    if (!result.Completed()) {
        return MapApplicationFailure(result.status, result.message);
    }

    switch (result.outcome) {
        case BeginUploadOutcome::kCreated:
            response->set_result(tinyimx::file::v1::BEGIN_UPLOAD_RESULT_CREATED);
            break;
        case BeginUploadOutcome::kReused:
            response->set_result(tinyimx::file::v1::BEGIN_UPLOAD_RESULT_REUSED);
            break;
        case BeginUploadOutcome::kIdempotencyConflict:
            response->set_result(tinyimx::file::v1::BEGIN_UPLOAD_RESULT_IDEMPOTENCY_CONFLICT);
            break;
    }
    response->set_message(result.message);
    if (result.bundle.has_value()) {
        FillBundle(*result.bundle, response->mutable_file(), response->mutable_session());
    }
    return grpc::Status::OK;
}

grpc::Status FileServiceImpl::GetUploadSession(
    grpc::ServerContext* context,
    const tinyimx::file::v1::GetUploadSessionRequest* request,
    tinyimx::file::v1::GetUploadSessionResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid GetUploadSession RPC arguments"};
    }
    if (application_service_ == nullptr) {
        return {grpc::StatusCode::UNAVAILABLE, "FileService application service is unavailable"};
    }

    const auto result = application_service_->GetUploadSession(
        {request->actor_user_id(), request->upload_id()}
    );
    if (!result.Found()) {
        return MapApplicationFailure(result.status, result.message);
    }
    FillBundle(*result.bundle, response->mutable_file(), response->mutable_session());
    return grpc::Status::OK;
}

grpc::Status FileServiceImpl::CancelUpload(
    grpc::ServerContext* context,
    const tinyimx::file::v1::CancelUploadRequest* request,
    tinyimx::file::v1::CancelUploadResponse* response
) {
    if (context == nullptr || request == nullptr || response == nullptr) {
        return {grpc::StatusCode::INVALID_ARGUMENT, "invalid CancelUpload RPC arguments"};
    }
    if (application_service_ == nullptr) {
        return {grpc::StatusCode::UNAVAILABLE, "FileService application service is unavailable"};
    }

    const auto result = application_service_->CancelUpload(
        {request->actor_user_id(), request->upload_id()}
    );
    if (!result.Completed()) {
        return MapApplicationFailure(result.status, result.message);
    }
    response->set_result(
        result.outcome == CancelUploadOutcome::kApplied
            ? tinyimx::file::v1::CANCEL_UPLOAD_RESULT_APPLIED
            : tinyimx::file::v1::CANCEL_UPLOAD_RESULT_REUSED
    );
    response->set_message(result.message);
    if (result.bundle.has_value()) {
        FillBundle(*result.bundle, response->mutable_file(), response->mutable_session());
    }
    return grpc::Status::OK;
}

}  // namespace tinyimx::file
