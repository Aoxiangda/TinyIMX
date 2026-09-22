#pragma once

#include "common/db/MySqlConnectionPool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {

enum class FileRepositoryStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kNotFound,
    kInvalidRecord,
    kStorageError,
};

struct FileRecord {
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
    std::uint32_t status{0};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string available_at;
    std::string expires_at;
};

struct FileUploadSessionRecord {
    std::uint64_t upload_id{0};
    std::uint64_t file_id{0};
    std::uint64_t owner_user_id{0};
    std::string client_upload_id;
    std::string request_fingerprint;
    std::uint64_t total_size{0};
    std::uint64_t chunk_size{0};
    std::uint32_t status{0};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string expires_at;
    std::string completed_at;
};

struct FileUploadBundleRecord {
    FileRecord file;
    FileUploadSessionRecord session;
};

struct FileBooleanResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    bool value{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
};

struct FileInsertResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    std::uint64_t file_id{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
};

struct UploadSessionInsertResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    bool inserted{false};
    std::uint64_t upload_id{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
};

struct FileUploadBundleFindResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    bool found{false};
    FileUploadBundleRecord record;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
    [[nodiscard]] bool Found() const noexcept {
        return Succeeded() && found;
    }
};


struct FileUploadChunkRecord {
    std::uint64_t upload_id{0};
    std::uint64_t chunk_index{0};
    std::uint64_t file_id{0};
    std::uint64_t owner_user_id{0};
    std::uint64_t byte_offset{0};
    std::uint64_t chunk_size{0};
    std::string checksum_algorithm;
    std::string checksum;
    std::string storage_part_key;
    std::uint32_t status{0};
    std::uint64_t version{0};
    std::string created_at;
    std::string updated_at;
    std::string stored_at;
};

struct FileUploadChunkFindResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    bool found{false};
    FileUploadChunkRecord record;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
    [[nodiscard]] bool Found() const noexcept {
        return Succeeded() && found;
    }
};


struct FileUploadChunkListResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    std::vector<FileUploadChunkRecord> records;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
};

struct FileUploadChunkInsertResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    bool inserted{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
};

struct FileMutationResult {
    FileRepositoryStatus status{FileRepositoryStatus::kStorageError};
    std::uint64_t affected_rows{0};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileRepositoryStatus::kSucceeded;
    }
};

class FileRepository final {
public:
    explicit FileRepository(MySqlConnectionPool* pool);

    FileRepository(const FileRepository&) = delete;
    FileRepository& operator=(const FileRepository&) = delete;

    [[nodiscard]] FileUploadBundleFindResult FindByClientUploadId(
        std::uint64_t owner_user_id,
        const std::string& client_upload_id
    );

    [[nodiscard]] FileUploadBundleFindResult FindUploadBundle(
        std::uint64_t owner_user_id,
        std::uint64_t upload_id
    );

    [[nodiscard]] FileUploadChunkFindResult FindChunk(
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        std::uint64_t chunk_index
    );

    [[nodiscard]] FileUploadChunkListResult FindChunks(
        std::uint64_t owner_user_id,
        std::uint64_t upload_id
    );

    [[nodiscard]] FileBooleanResult UserExistsOnConnection(
        MySqlConnection* connection,
        std::uint64_t user_id
    );

    [[nodiscard]] FileInsertResult InsertFileOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        const std::string& file_name,
        const std::string& content_type,
        std::uint64_t total_size,
        const std::string& checksum_algorithm,
        const std::string& expected_checksum
    );

    [[nodiscard]] FileMutationResult SetStorageKeyOnConnection(
        MySqlConnection* connection,
        std::uint64_t file_id,
        const std::string& storage_key
    );

    [[nodiscard]] UploadSessionInsertResult InsertUploadSessionIdempotentOnConnection(
        MySqlConnection* connection,
        std::uint64_t file_id,
        std::uint64_t owner_user_id,
        const std::string& client_upload_id,
        const std::string& request_fingerprint,
        std::uint64_t total_size,
        std::uint64_t chunk_size
    );

    [[nodiscard]] FileUploadBundleFindResult FindByClientUploadIdOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        const std::string& client_upload_id,
        bool for_update
    );

    [[nodiscard]] FileUploadBundleFindResult FindUploadBundleOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        bool for_update
    );


    [[nodiscard]] FileUploadChunkInsertResult InsertChunkIdempotentOnConnection(
        MySqlConnection* connection,
        std::uint64_t upload_id,
        std::uint64_t chunk_index,
        std::uint64_t file_id,
        std::uint64_t owner_user_id,
        std::uint64_t byte_offset,
        std::uint64_t chunk_size,
        const std::string& checksum_algorithm,
        const std::string& checksum,
        const std::string& storage_part_key
    );

    [[nodiscard]] FileUploadChunkFindResult FindChunkOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        std::uint64_t chunk_index,
        bool for_update
    );

    [[nodiscard]] FileUploadChunkListResult FindChunksOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        bool for_update
    );

    [[nodiscard]] FileMutationResult MarkChunkStoredOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        std::uint64_t chunk_index,
        std::uint64_t chunk_size,
        const std::string& checksum
    );

    [[nodiscard]] FileMutationResult BeginFinalizeOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        std::uint64_t file_id
    );

    [[nodiscard]] FileMutationResult CompleteFinalizeOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        std::uint64_t file_id,
        const std::string& verified_checksum
    );

    [[nodiscard]] FileMutationResult FailFinalizeChecksumOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t file_id,
        const std::string& actual_checksum
    );

    [[nodiscard]] FileMutationResult CancelUploadOnConnection(
        MySqlConnection* connection,
        std::uint64_t owner_user_id,
        std::uint64_t upload_id,
        std::uint64_t file_id
    );

private:
    MySqlConnectionPool* pool_{nullptr};  // non-owning
};

}  // namespace tinyimx
