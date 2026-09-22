#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx::file {

enum class FileApplicationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kPermissionDenied,
    kFailedPrecondition,
    kInvalidRecord,
    kStorageError,
};

enum class FileStatus : std::uint32_t {
    kUploading = 1,
    kVerifying = 2,
    kAvailable = 3,
    kFailed = 4,
    kCanceled = 5,
    kExpired = 6,
    kDeleted = 7,
};

enum class UploadSessionStatus : std::uint32_t {
    kActive = 1,
    kFinalizing = 2,
    kCompleted = 3,
    kCanceled = 4,
    kExpired = 5,
};

enum class UploadChunkStatus : std::uint32_t {
    kReserved = 1,
    kStored = 2,
};

enum class BeginUploadOutcome {
    kCreated = 0,
    kReused,
    kIdempotencyConflict,
};

enum class CancelUploadOutcome {
    kApplied = 0,
    kReused,
};

enum class ReserveChunkOutcome {
    kCreated = 0,
    kReused,
    kIdempotencyConflict,
};

enum class UploadChunkOutcome {
    kStored = 0,
    kReused,
    kIdempotencyConflict,
};

struct FileView {
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
    FileStatus status{FileStatus::kUploading};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string available_at;
    std::string expires_at;
};

struct UploadSessionView {
    std::uint64_t upload_id{0};
    std::uint64_t file_id{0};
    std::uint64_t owner_user_id{0};
    std::string client_upload_id;
    std::uint64_t total_size{0};
    std::uint64_t chunk_size{0};
    UploadSessionStatus status{UploadSessionStatus::kActive};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string expires_at;
    std::string completed_at;
};

struct UploadChunkView {
    std::uint64_t upload_id{0};
    std::uint64_t chunk_index{0};
    std::uint64_t file_id{0};
    std::uint64_t owner_user_id{0};
    std::uint64_t byte_offset{0};
    std::uint64_t chunk_size{0};
    std::string checksum_algorithm;
    std::string checksum;
    std::string storage_part_key;
    UploadChunkStatus status{UploadChunkStatus::kReserved};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string stored_at;
};

struct UploadBundleView {
    FileView file;
    UploadSessionView session;
};

struct BeginUploadCommand {
    std::uint64_t actor_user_id{0};
    std::string client_upload_id;
    std::string file_name;
    std::string content_type;
    std::uint64_t total_size{0};
    std::string checksum_algorithm;
    std::string expected_checksum;
    std::uint64_t preferred_chunk_size{0};
};

struct GetUploadSessionQuery {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
};

struct CancelUploadCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
};

struct UploadChunkCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
    std::uint64_t chunk_index{0};
    std::uint64_t byte_offset{0};
    std::string data;
    std::string checksum_algorithm;
    std::string checksum;
};

struct ReserveChunkCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
    std::uint64_t chunk_index{0};
    std::uint64_t byte_offset{0};
    std::uint64_t chunk_size{0};
    std::string checksum_algorithm;
    std::string checksum;
};

struct MarkChunkStoredCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
    std::uint64_t chunk_index{0};
    std::uint64_t chunk_size{0};
    std::string checksum;
};

struct BeginUploadResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    BeginUploadOutcome outcome{BeginUploadOutcome::kCreated};
    std::optional<UploadBundleView> bundle;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Accepted() const noexcept {
        return Completed() && outcome != BeginUploadOutcome::kIdempotencyConflict &&
               bundle.has_value();
    }
};

struct GetUploadSessionResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<UploadBundleView> bundle;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == FileApplicationStatus::kSucceeded && bundle.has_value();
    }
};

struct CancelUploadResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    CancelUploadOutcome outcome{CancelUploadOutcome::kApplied};
    std::optional<UploadBundleView> bundle;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded;
    }
};

struct ReserveChunkResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    ReserveChunkOutcome outcome{ReserveChunkOutcome::kCreated};
    std::optional<UploadChunkView> chunk;
    std::string message;

    [[nodiscard]] bool Accepted() const noexcept {
        return status == FileApplicationStatus::kSucceeded &&
               outcome != ReserveChunkOutcome::kIdempotencyConflict &&
               chunk.has_value();
    }
};

struct MarkChunkStoredResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<UploadChunkView> chunk;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded && chunk.has_value();
    }
};

struct UploadChunkResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    UploadChunkOutcome outcome{UploadChunkOutcome::kStored};
    std::optional<UploadChunkView> chunk;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded;
    }

    [[nodiscard]] bool Accepted() const noexcept {
        return Completed() && outcome != UploadChunkOutcome::kIdempotencyConflict &&
               chunk.has_value();
    }
};

}  // namespace tinyimx::file
