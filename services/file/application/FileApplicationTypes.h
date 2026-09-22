#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tinyimx::file {

enum class FileApplicationStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kPermissionDenied,
    kFailedPrecondition,
    kInvalidRecord,
    kOutOfRange,
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

enum class FinalizePreparationOutcome {
    kStarted = 0,
    kResumed,
    kAlreadyCompleted,
    kNotReady,
    kChecksumMismatch,
};

enum class CompleteFinalizeOutcome {
    kCompleted = 0,
    kReused,
};

enum class FinalizeUploadOutcome {
    kCompleted = 0,
    kReused,
    kNotReady,
    kChecksumMismatch,
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



struct DownloadInfoView {
    std::uint64_t file_id{0};
    std::string file_name;
    std::string content_type;
    std::uint64_t total_size{0};
    std::string checksum_algorithm;
    std::string verified_checksum;
    std::uint64_t version{0};
    std::string available_at;
};

struct UploadBundleView {
    FileView file;
    UploadSessionView session;
};

struct UploadSnapshotView {
    UploadBundleView bundle;
    std::vector<UploadChunkView> chunks;
};

struct ChunkIndexRange {
    std::uint64_t start_index{0};
    std::uint64_t end_index{0};
};

struct UploadProgressView {
    UploadBundleView bundle;
    std::uint64_t expected_chunk_count{0};
    std::uint64_t stored_chunk_count{0};
    std::uint64_t reserved_chunk_count{0};
    std::vector<ChunkIndexRange> missing_ranges;
    bool ready_to_finalize{false};
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

struct GetUploadProgressQuery {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
};

struct FinalizeUploadCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
};

struct GetDownloadInfoQuery {
    std::uint64_t actor_user_id{0};
    std::uint64_t file_id{0};
};

struct ReadFileRangeQuery {
    std::uint64_t actor_user_id{0};
    std::uint64_t file_id{0};
    std::uint64_t offset{0};
    std::uint64_t length{0};
    std::string if_match_sha256;
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

struct CompleteFinalizeCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
    std::string verified_checksum;
};

struct FailFinalizeChecksumCommand {
    std::uint64_t actor_user_id{0};
    std::uint64_t upload_id{0};
    std::string actual_checksum;
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


struct GetUploadSnapshotResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<UploadSnapshotView> snapshot;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == FileApplicationStatus::kSucceeded && snapshot.has_value();
    }
};

struct GetUploadProgressResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<UploadProgressView> progress;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == FileApplicationStatus::kSucceeded && progress.has_value();
    }
};

struct FinalizePreparationResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    FinalizePreparationOutcome outcome{FinalizePreparationOutcome::kNotReady};
    std::optional<UploadSnapshotView> snapshot;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded;
    }
};

struct CompleteFinalizeResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    CompleteFinalizeOutcome outcome{CompleteFinalizeOutcome::kCompleted};
    std::optional<UploadBundleView> bundle;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded && bundle.has_value();
    }
};

struct FailFinalizeChecksumResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<UploadBundleView> bundle;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded && bundle.has_value();
    }
};

struct FinalizeUploadResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    FinalizeUploadOutcome outcome{FinalizeUploadOutcome::kNotReady};
    std::optional<UploadProgressView> progress;
    std::optional<UploadBundleView> bundle;
    std::string verified_checksum;
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded;
    }
};


struct GetDownloadFileResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<FileView> file;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == FileApplicationStatus::kSucceeded && file.has_value();
    }
};

struct GetDownloadInfoResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<DownloadInfoView> info;
    std::string message;

    [[nodiscard]] bool Found() const noexcept {
        return status == FileApplicationStatus::kSucceeded && info.has_value();
    }
};

struct ReadFileRangeResult {
    FileApplicationStatus status{FileApplicationStatus::kStorageError};
    std::optional<DownloadInfoView> info;
    std::uint64_t offset{0};
    std::string data;
    std::string range_sha256;
    bool eof{false};
    std::uint64_t next_offset{0};
    std::string message;

    [[nodiscard]] bool Completed() const noexcept {
        return status == FileApplicationStatus::kSucceeded && info.has_value();
    }
};

}  // namespace tinyimx::file
