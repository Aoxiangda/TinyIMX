#pragma once

#include "services/file/application/FileApplicationTypes.h"
#include "services/file/application/FileRepositoryPort.h"

namespace tinyimx::file {
class FileStoragePort;

class FileApplicationService final {
public:
    explicit FileApplicationService(
        FileRepositoryPort* repository,
        FileStoragePort* storage = nullptr
    );

    FileApplicationService(const FileApplicationService&) = delete;
    FileApplicationService& operator=(const FileApplicationService&) = delete;

    [[nodiscard]] BeginUploadResult BeginUpload(BeginUploadCommand command);
    [[nodiscard]] GetUploadSessionResult GetUploadSession(
        const GetUploadSessionQuery& query
    );
    [[nodiscard]] CancelUploadResult CancelUpload(
        const CancelUploadCommand& command
    );
    [[nodiscard]] UploadChunkResult UploadChunk(UploadChunkCommand command);
    [[nodiscard]] GetUploadProgressResult GetUploadProgress(
        const GetUploadProgressQuery& query
    );
    [[nodiscard]] FinalizeUploadResult FinalizeUpload(
        const FinalizeUploadCommand& command
    );

private:
    FileRepositoryPort* repository_{nullptr};  // non-owning
    FileStoragePort* storage_{nullptr};  // non-owning
};

}  // namespace tinyimx::file
