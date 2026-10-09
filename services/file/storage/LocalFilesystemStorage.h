#pragma once

#include "services/file/storage/FileStoragePort.h"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tinyimx::file {

class LocalFilesystemStorage final : public FileStoragePort {
public:
    explicit LocalFilesystemStorage(std::filesystem::path root);

    [[nodiscard]] StoreChunkResult StoreChunkAtomically(
        const StoreChunkRequest& request
    ) override;

    [[nodiscard]] ComposeObjectResult ComposeObjectAtomically(
        const ComposeObjectRequest& request
    ) override;

    [[nodiscard]] VerifyObjectResult VerifyObject(
        const VerifyObjectRequest& request
    ) override;

    [[nodiscard]] ReadObjectRangeResult ReadObjectRange(
        const ReadObjectRangeRequest& request
    ) override;

    [[nodiscard]] const std::filesystem::path& Root() const noexcept {
        return root_;
    }

private:
    [[nodiscard]] bool ResolveSafePath(
        const std::string& storage_part_key,
        std::filesystem::path* output
    ) const;

    struct VerifiedObjectCacheEntry {
        std::string fingerprint;
        std::string whole_sha256;
        std::vector<std::string> block_sha256;
    };

    std::filesystem::path root_;
    // Cache is a bounded performance aid, not durable truth. A Range cache hit
    // still verifies the blocks it will serve against digests produced by the
    // last successful whole-object verification.
    mutable std::mutex verification_mutex_;
    std::unordered_map<std::string, VerifiedObjectCacheEntry> verified_objects_;
};

}  // namespace tinyimx::file
