#pragma once

#include "services/file/application/FileApplicationTypes.h"
#include "services/file/application/FileRepositoryPort.h"

namespace tinyimx::file {

class FileApplicationService final {
public:
    explicit FileApplicationService(FileRepositoryPort* repository);

    FileApplicationService(const FileApplicationService&) = delete;
    FileApplicationService& operator=(const FileApplicationService&) = delete;

    [[nodiscard]] BeginUploadResult BeginUpload(BeginUploadCommand command);
    [[nodiscard]] GetUploadSessionResult GetUploadSession(
        const GetUploadSessionQuery& query
    );
    [[nodiscard]] CancelUploadResult CancelUpload(
        const CancelUploadCommand& command
    );

private:
    FileRepositoryPort* repository_{nullptr};  // non-owning
};

}  // namespace tinyimx::file
