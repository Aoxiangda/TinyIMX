#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tinyimx::file {

enum class FileStorageStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kIoError,
    kNotFound,
    kDataLoss,
    kChecksumMismatch,
};

struct StoreChunkRequest {
    std::string storage_part_key;
    std::string_view data;
    std::string expected_sha256;
};

struct StoreChunkResult {
    FileStorageStatus status{FileStorageStatus::kIoError};
    std::uint64_t bytes_written{0};
    std::string verified_sha256;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileStorageStatus::kSucceeded;
    }
};

struct StoredChunkPart {
    std::string storage_part_key;
    std::uint64_t expected_size{0};
    std::string expected_sha256;
};

struct ComposeObjectRequest {
    std::string storage_key;
    std::uint64_t expected_total_size{0};
    std::string expected_sha256;
    std::vector<StoredChunkPart> parts;
};

struct ComposeObjectResult {
    FileStorageStatus status{FileStorageStatus::kIoError};
    std::uint64_t bytes_written{0};
    std::string verified_sha256;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileStorageStatus::kSucceeded;
    }
};



struct VerifyObjectRequest {
    std::string storage_key;
    std::uint64_t expected_total_size{0};
    std::string expected_sha256;
};

struct VerifyObjectResult {
    FileStorageStatus status{FileStorageStatus::kIoError};
    std::uint64_t bytes_verified{0};
    std::string verified_sha256;
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileStorageStatus::kSucceeded;
    }
};

struct ReadObjectRangeRequest {
    std::string storage_key;
    std::uint64_t offset{0};
    std::uint64_t length{0};
    std::uint64_t expected_total_size{0};
    std::string expected_sha256;
};

struct ReadObjectRangeResult {
    FileStorageStatus status{FileStorageStatus::kIoError};
    std::uint64_t offset{0};
    std::string data;
    std::string range_sha256;
    bool eof{false};
    std::string message;

    [[nodiscard]] bool Succeeded() const noexcept {
        return status == FileStorageStatus::kSucceeded;
    }
};

class FileStoragePort {
public:
    virtual ~FileStoragePort() = default;

    FileStoragePort() = default;
    FileStoragePort(const FileStoragePort&) = delete;
    FileStoragePort& operator=(const FileStoragePort&) = delete;

    // Implementations must publish one complete chunk atomically. A retry of
    // the same server-generated key and bytes is allowed and must not append.
    [[nodiscard]] virtual StoreChunkResult StoreChunkAtomically(
        const StoreChunkRequest& request
    ) = 0;

    // Assemble an immutable final object from already-stored durable parts.
    // Implementations must verify every part and the whole-file checksum before
    // publishing storage_key via atomic replace. The caller deliberately does
    // not hold a database transaction across this potentially slow I/O.
    [[nodiscard]] virtual ComposeObjectResult ComposeObjectAtomically(
        const ComposeObjectRequest& request
    ) = 0;

    // Verify one immutable final object against durable size + whole-file
    // SHA-256. This is intentionally an open/resume control-path operation,
    // not a per-range hot-path scan.
    [[nodiscard]] virtual VerifyObjectResult VerifyObject(
        const VerifyObjectRequest& request
    ) = 0;

    // Read one bounded range from an immutable final object. Range reads are
    // stateless/idempotent; reconnect resumes by submitting the next offset.
    [[nodiscard]] virtual ReadObjectRangeResult ReadObjectRange(
        const ReadObjectRangeRequest& request
    ) = 0;
};

}  // namespace tinyimx::file
