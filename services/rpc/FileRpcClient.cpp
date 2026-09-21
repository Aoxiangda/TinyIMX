#include "services/rpc/FileRpcClient.h"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace tinyimx::rpc {
namespace {

template <typename T>
RpcResult<T> Failure(RpcErrorCode code, std::string message) {
    return RpcResult<T>::Failure(code, std::move(message));
}

void FillMeta(const RpcCallOptions& options, tinyimx::common::v1::RequestMeta* meta) {
    if (!meta) return;
    meta->set_request_id(options.request_id);
    meta->set_trace_id(options.trace_id);
    meta->set_caller_service(options.caller_service);
    meta->set_caller_instance(options.caller_instance);
}

std::optional<FileRpcStatus> ToFileStatus(tinyimx::file::v1::FileStatus value) {
    using P = tinyimx::file::v1::FileStatus;
    switch (value) {
        case P::FILE_STATUS_UPLOADING: return FileRpcStatus::kUploading;
        case P::FILE_STATUS_VERIFYING: return FileRpcStatus::kVerifying;
        case P::FILE_STATUS_AVAILABLE: return FileRpcStatus::kAvailable;
        case P::FILE_STATUS_FAILED: return FileRpcStatus::kFailed;
        case P::FILE_STATUS_CANCELED: return FileRpcStatus::kCanceled;
        case P::FILE_STATUS_EXPIRED: return FileRpcStatus::kExpired;
        case P::FILE_STATUS_DELETED: return FileRpcStatus::kDeleted;
        default: return std::nullopt;
    }
}

std::optional<UploadSessionRpcStatus> ToUploadStatus(
    tinyimx::file::v1::UploadSessionStatus value) {
    using P = tinyimx::file::v1::UploadSessionStatus;
    switch (value) {
        case P::UPLOAD_SESSION_STATUS_ACTIVE: return UploadSessionRpcStatus::kActive;
        case P::UPLOAD_SESSION_STATUS_FINALIZING: return UploadSessionRpcStatus::kFinalizing;
        case P::UPLOAD_SESSION_STATUS_COMPLETED: return UploadSessionRpcStatus::kCompleted;
        case P::UPLOAD_SESSION_STATUS_CANCELED: return UploadSessionRpcStatus::kCanceled;
        case P::UPLOAD_SESSION_STATUS_EXPIRED: return UploadSessionRpcStatus::kExpired;
        default: return std::nullopt;
    }
}

std::optional<FileUploadBundleRpcView> ToBundle(
    const tinyimx::file::v1::FileRecord& file,
    const tinyimx::file::v1::UploadSessionRecord& session) {
    const auto fs = ToFileStatus(file.status());
    const auto us = ToUploadStatus(session.status());
    if (!fs || !us || file.file_id() == 0 || session.upload_id() == 0 ||
        file.file_id() != session.file_id() ||
        file.owner_user_id() == 0 || file.owner_user_id() != session.owner_user_id() ||
        file.total_size() == 0 || file.total_size() != session.total_size() ||
        session.chunk_size() == 0 || file.version() == 0 || session.version() == 0) {
        return std::nullopt;
    }
    FileUploadBundleRpcView out;
    out.file.file_id = file.file_id();
    out.file.owner_user_id = file.owner_user_id();
    out.file.file_name = file.file_name();
    out.file.content_type = file.content_type();
    out.file.total_size = file.total_size();
    out.file.checksum_algorithm = file.checksum_algorithm();
    out.file.expected_checksum = file.expected_checksum();
    out.file.verified_checksum = file.verified_checksum();
    out.file.storage_backend = file.storage_backend();
    out.file.storage_key = file.storage_key();
    out.file.status = *fs;
    out.file.version = file.version();
    out.file.created_at = file.created_at();
    out.file.updated_at = file.updated_at();
    out.file.available_at = file.available_at();
    out.file.expires_at = file.expires_at();
    out.session.upload_id = session.upload_id();
    out.session.file_id = session.file_id();
    out.session.owner_user_id = session.owner_user_id();
    out.session.client_upload_id = session.client_upload_id();
    out.session.total_size = session.total_size();
    out.session.chunk_size = session.chunk_size();
    out.session.status = *us;
    out.session.version = session.version();
    out.session.created_at = session.created_at();
    out.session.updated_at = session.updated_at();
    out.session.expires_at = session.expires_at();
    out.session.completed_at = session.completed_at();
    return out;
}

}  // namespace

FileRpcClient::FileRpcClient(std::shared_ptr<const ServiceEndpointProvider> endpoint_provider)
    : endpoint_provider_(std::move(endpoint_provider)) {}

RpcResult<BeginUploadRpcResponse> FileRpcClient::BeginUpload(
    const BeginUploadRpcRequest& request, const RpcCallOptions& options) const {
    if (request.actor_user_id == 0 || request.client_upload_id.empty() ||
        request.file_name.empty() || request.total_size == 0 ||
        request.checksum_algorithm.empty() || request.expected_checksum.empty()) {
        return Failure<BeginUploadRpcResponse>(RpcErrorCode::kInvalidArgument,
                                               "invalid BeginUpload request");
    }
    if (options.remaining_timeout <= std::chrono::milliseconds::zero())
        return Failure<BeginUploadRpcResponse>(RpcErrorCode::kDeadlineExceeded,
                                               "BeginUpload remaining RPC budget is exhausted");
    if (!endpoint_provider_)
        return Failure<BeginUploadRpcResponse>(RpcErrorCode::kUnavailable,
                                               "FileService endpoint provider is not configured");
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kFile);
    if (!endpoint || endpoint->target.empty())
        return Failure<BeginUploadRpcResponse>(RpcErrorCode::kUnavailable,
                                               "FileService endpoint is unavailable");
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) return Failure<BeginUploadRpcResponse>(RpcErrorCode::kUnavailable,
                                                      "FileService gRPC stub could not be created");

    tinyimx::file::v1::BeginUploadRequest in;
    FillMeta(options, in.mutable_meta());
    in.set_actor_user_id(request.actor_user_id);
    in.set_client_upload_id(request.client_upload_id);
    in.set_file_name(request.file_name);
    in.set_content_type(request.content_type);
    in.set_total_size(request.total_size);
    in.set_checksum_algorithm(request.checksum_algorithm);
    in.set_expected_checksum(request.expected_checksum);
    in.set_preferred_chunk_size(request.preferred_chunk_size);
    tinyimx::file::v1::BeginUploadResponse out;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    const auto status = stub->BeginUpload(&context, in, &out);
    if (!status.ok()) { const auto mapped = MapGrpcStatus(status); return Failure<BeginUploadRpcResponse>(mapped.code, mapped.message); }

    BeginUploadRpcResponse response;
    switch (out.result()) {
        case tinyimx::file::v1::BEGIN_UPLOAD_RESULT_CREATED:
            response.outcome = BeginUploadRpcOutcome::kCreated; break;
        case tinyimx::file::v1::BEGIN_UPLOAD_RESULT_REUSED:
            response.outcome = BeginUploadRpcOutcome::kReused; break;
        case tinyimx::file::v1::BEGIN_UPLOAD_RESULT_IDEMPOTENCY_CONFLICT:
            response.outcome = BeginUploadRpcOutcome::kIdempotencyConflict;
            response.message = out.message();
            return RpcResult<BeginUploadRpcResponse>::Success(std::move(response));
        default:
            return Failure<BeginUploadRpcResponse>(RpcErrorCode::kDataLoss,
                                                   "FileService returned unspecified BeginUpload result");
    }
    auto bundle = ToBundle(out.file(), out.session());
    if (!bundle) return Failure<BeginUploadRpcResponse>(RpcErrorCode::kDataLoss,
                                                        "FileService returned invalid upload bundle");
    response.bundle = std::move(*bundle);
    response.message = out.message();
    return RpcResult<BeginUploadRpcResponse>::Success(std::move(response));
}

RpcResult<GetUploadSessionRpcResponse> FileRpcClient::GetUploadSession(
    const GetUploadSessionRpcRequest& request, const RpcCallOptions& options) const {
    if (request.actor_user_id == 0 || request.upload_id == 0)
        return Failure<GetUploadSessionRpcResponse>(RpcErrorCode::kInvalidArgument,
                                                    "invalid GetUploadSession request");
    if (options.remaining_timeout <= std::chrono::milliseconds::zero())
        return Failure<GetUploadSessionRpcResponse>(RpcErrorCode::kDeadlineExceeded,
                                                    "GetUploadSession remaining RPC budget is exhausted");
    if (!endpoint_provider_) return Failure<GetUploadSessionRpcResponse>(RpcErrorCode::kUnavailable,
                                                                          "FileService endpoint provider is not configured");
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kFile);
    if (!endpoint || endpoint->target.empty()) return Failure<GetUploadSessionRpcResponse>(RpcErrorCode::kUnavailable,
                                                                                           "FileService endpoint is unavailable");
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) return Failure<GetUploadSessionRpcResponse>(RpcErrorCode::kUnavailable,
                                                           "FileService gRPC stub could not be created");
    tinyimx::file::v1::GetUploadSessionRequest in;
    FillMeta(options, in.mutable_meta()); in.set_actor_user_id(request.actor_user_id); in.set_upload_id(request.upload_id);
    tinyimx::file::v1::GetUploadSessionResponse out;
    grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    const auto status = stub->GetUploadSession(&context, in, &out);
    if (!status.ok()) { const auto mapped = MapGrpcStatus(status); return Failure<GetUploadSessionRpcResponse>(mapped.code, mapped.message); }
    auto bundle = ToBundle(out.file(), out.session());
    if (!bundle) return Failure<GetUploadSessionRpcResponse>(RpcErrorCode::kDataLoss,
                                                             "FileService returned invalid upload bundle");
    GetUploadSessionRpcResponse response; response.bundle = std::move(*bundle);
    return RpcResult<GetUploadSessionRpcResponse>::Success(std::move(response));
}

RpcResult<CancelUploadRpcResponse> FileRpcClient::CancelUpload(
    const CancelUploadRpcRequest& request, const RpcCallOptions& options) const {
    if (request.actor_user_id == 0 || request.upload_id == 0)
        return Failure<CancelUploadRpcResponse>(RpcErrorCode::kInvalidArgument,
                                                "invalid CancelUpload request");
    if (options.remaining_timeout <= std::chrono::milliseconds::zero())
        return Failure<CancelUploadRpcResponse>(RpcErrorCode::kDeadlineExceeded,
                                                "CancelUpload remaining RPC budget is exhausted");
    if (!endpoint_provider_) return Failure<CancelUploadRpcResponse>(RpcErrorCode::kUnavailable,
                                                                      "FileService endpoint provider is not configured");
    const auto endpoint = endpoint_provider_->Resolve(ServiceKind::kFile);
    if (!endpoint || endpoint->target.empty()) return Failure<CancelUploadRpcResponse>(RpcErrorCode::kUnavailable,
                                                                                       "FileService endpoint is unavailable");
    auto stub = GetOrCreateStub(*endpoint);
    if (!stub) return Failure<CancelUploadRpcResponse>(RpcErrorCode::kUnavailable,
                                                       "FileService gRPC stub could not be created");
    tinyimx::file::v1::CancelUploadRequest in;
    FillMeta(options, in.mutable_meta()); in.set_actor_user_id(request.actor_user_id); in.set_upload_id(request.upload_id);
    tinyimx::file::v1::CancelUploadResponse out;
    grpc::ClientContext context; context.set_deadline(std::chrono::system_clock::now() + options.remaining_timeout);
    const auto status = stub->CancelUpload(&context, in, &out);
    if (!status.ok()) { const auto mapped = MapGrpcStatus(status); return Failure<CancelUploadRpcResponse>(mapped.code, mapped.message); }
    CancelUploadRpcResponse response;
    switch (out.result()) {
        case tinyimx::file::v1::CANCEL_UPLOAD_RESULT_APPLIED: response.outcome = CancelUploadRpcOutcome::kApplied; break;
        case tinyimx::file::v1::CANCEL_UPLOAD_RESULT_REUSED: response.outcome = CancelUploadRpcOutcome::kReused; break;
        default: return Failure<CancelUploadRpcResponse>(RpcErrorCode::kDataLoss,
                                                         "FileService returned unspecified CancelUpload result");
    }
    auto bundle = ToBundle(out.file(), out.session());
    if (!bundle) return Failure<CancelUploadRpcResponse>(RpcErrorCode::kDataLoss,
                                                         "FileService returned invalid upload bundle");
    response.bundle = std::move(*bundle); response.message = out.message();
    return RpcResult<CancelUploadRpcResponse>::Success(std::move(response));
}

std::size_t FileRpcClient::CachedTargetCountForTest() const { std::lock_guard<std::mutex> lock(cache_mutex_); return stub_cache_.size(); }
std::uint64_t FileRpcClient::StubCreationCountForTest() const { std::lock_guard<std::mutex> lock(cache_mutex_); return stub_creation_count_; }

std::shared_ptr<FileRpcClient::StubInterface> FileRpcClient::GetOrCreateStub(const ServiceEndpoint& endpoint) const {
    if (endpoint.target.empty()) return nullptr;
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const std::uint64_t sequence = ++cache_use_sequence_;
    auto existing = stub_cache_.find(endpoint.target);
    if (existing != stub_cache_.end()) { existing->second.last_used = sequence; return existing->second.stub; }
    auto channel = grpc::CreateChannel(endpoint.target, grpc::InsecureChannelCredentials());
    if (!channel) return nullptr;
    auto unique_stub = tinyimx::file::v1::FileService::NewStub(channel);
    if (!unique_stub) return nullptr;
    std::shared_ptr<StubInterface> stub(std::move(unique_stub));
    if (stub_cache_.size() >= kMaxCachedTargets) {
        const auto victim = std::min_element(stub_cache_.begin(), stub_cache_.end(),
            [](const auto& a, const auto& b){ return a.second.last_used < b.second.last_used; });
        if (victim != stub_cache_.end()) stub_cache_.erase(victim);
    }
    CachedStubEntry entry; entry.channel = std::move(channel); entry.stub = stub; entry.last_used = sequence;
    stub_cache_.emplace(endpoint.target, std::move(entry)); ++stub_creation_count_; return stub;
}

RpcStatus FileRpcClient::MapGrpcStatus(const grpc::Status& status) {
    if (status.ok()) return RpcStatus::Ok();
    RpcErrorCode code = RpcErrorCode::kUnknown;
    switch (status.error_code()) {
        case grpc::StatusCode::INVALID_ARGUMENT: code = RpcErrorCode::kInvalidArgument; break;
        case grpc::StatusCode::CANCELLED: code = RpcErrorCode::kCancelled; break;
        case grpc::StatusCode::DEADLINE_EXCEEDED: code = RpcErrorCode::kDeadlineExceeded; break;
        case grpc::StatusCode::NOT_FOUND: code = RpcErrorCode::kNotFound; break;
        case grpc::StatusCode::ALREADY_EXISTS: code = RpcErrorCode::kAlreadyExists; break;
        case grpc::StatusCode::PERMISSION_DENIED: code = RpcErrorCode::kPermissionDenied; break;
        case grpc::StatusCode::UNAUTHENTICATED: code = RpcErrorCode::kUnauthenticated; break;
        case grpc::StatusCode::RESOURCE_EXHAUSTED: code = RpcErrorCode::kResourceExhausted; break;
        case grpc::StatusCode::FAILED_PRECONDITION: code = RpcErrorCode::kFailedPrecondition; break;
        case grpc::StatusCode::ABORTED: code = RpcErrorCode::kAborted; break;
        case grpc::StatusCode::OUT_OF_RANGE: code = RpcErrorCode::kOutOfRange; break;
        case grpc::StatusCode::UNIMPLEMENTED: code = RpcErrorCode::kUnimplemented; break;
        case grpc::StatusCode::UNAVAILABLE: code = RpcErrorCode::kUnavailable; break;
        case grpc::StatusCode::INTERNAL: code = RpcErrorCode::kInternal; break;
        case grpc::StatusCode::DATA_LOSS: code = RpcErrorCode::kDataLoss; break;
        case grpc::StatusCode::OK: code = RpcErrorCode::kOk; break;
        default: code = RpcErrorCode::kUnknown; break;
    }
    return {code, status.error_message()};
}

}  // namespace tinyimx::rpc
