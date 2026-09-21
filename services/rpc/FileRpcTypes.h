#pragma once

#include <cstdint>
#include <string>

namespace tinyimx::rpc {

enum class FileRpcStatus : std::uint32_t {
    kUploading = 1,
    kVerifying = 2,
    kAvailable = 3,
    kFailed = 4,
    kCanceled = 5,
    kExpired = 6,
    kDeleted = 7,
};

enum class UploadSessionRpcStatus : std::uint32_t {
    kActive = 1,
    kFinalizing = 2,
    kCompleted = 3,
    kCanceled = 4,
    kExpired = 5,
};

enum class BeginUploadRpcOutcome : std::uint32_t {
    kCreated = 1,
    kReused = 2,
    kIdempotencyConflict = 3,
};

enum class CancelUploadRpcOutcome : std::uint32_t {
    kApplied = 1,
    kReused = 2,
};

struct FileRpcView {
    std::uint64_t file_id{0};
    std::uint64_t owner_user_id{0};
    std::string file_name;
    std::string content_type;
    std::uint64_t total_size{0};
    std::string checksum_algorithm;
    std::string expected_checksum;
    std::string verified_checksum;
    std::string storage_backend;
    std::string storage_key;
    FileRpcStatus status{FileRpcStatus::kUploading};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string available_at;
    std::string expires_at;
};

struct UploadSessionRpcView {
    std::uint64_t upload_id{0};
    std::uint64_t file_id{0};
    std::uint64_t owner_user_id{0};
    std::string client_upload_id;
    std::uint64_t total_size{0};
    std::uint64_t chunk_size{0};
    UploadSessionRpcStatus status{UploadSessionRpcStatus::kActive};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string expires_at;
    std::string completed_at;
};

struct FileUploadBundleRpcView {
    FileRpcView file;
    UploadSessionRpcView session;
};

struct BeginUploadRpcRequest {
    std::uint64_t actor_user_id{0};
    std::string client_upload_id;
    std::string file_name;
    std::string content_type;
    std::uint64_t total_size{0};
    std::string checksum_algorithm;
    std::string expected_checksum;
    std::uint64_t preferred_chunk_size{0};
};

struct BeginUploadRpcResponse {
    BeginUploadRpcOutcome outcome{BeginUploadRpcOutcome::kCreated};
    FileUploadBundleRpcView bundle;
    std::string message;
};

struct GetUploadSessionRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
};

struct GetUploadSessionRpcResponse {
    FileUploadBundleRpcView bundle;
};

struct CancelUploadRpcRequest {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
};

struct CancelUploadRpcResponse {
    CancelUploadRpcOutcome outcome{CancelUploadRpcOutcome::kApplied};
    FileUploadBundleRpcView bundle;
    std::string message;
};

}  // namespace tinyimx::rpc
