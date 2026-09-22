#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tinyimx::file {

enum class FileStorageStatus {
    kSucceeded = 0,
    kInvalidArgument,
    kIoError,
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
};

}  // namespace tinyimx::file
