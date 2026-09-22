#pragma once

#include "services/file/storage/FileStoragePort.h"

#include <filesystem>

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

    std::filesystem::path root_;
};

}  // namespace tinyimx::file
