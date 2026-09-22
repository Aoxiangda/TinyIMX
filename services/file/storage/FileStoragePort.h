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
};

}  // namespace tinyimx::file
