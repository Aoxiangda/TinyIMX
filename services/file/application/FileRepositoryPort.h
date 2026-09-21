#pragma once

#include "services/file/application/FileApplicationTypes.h"

namespace tinyimx::file {

class FileRepositoryPort {
public:
    virtual ~FileRepositoryPort() = default;

    FileRepositoryPort() = default;
    FileRepositoryPort(const FileRepositoryPort&) = delete;
    FileRepositoryPort& operator=(const FileRepositoryPort&) = delete;

    [[nodiscard]] virtual BeginUploadResult BeginUpload(
        const BeginUploadCommand& command
    ) = 0;

    [[nodiscard]] virtual GetUploadSessionResult GetUploadSession(
        const GetUploadSessionQuery& query
    ) = 0;

    [[nodiscard]] virtual CancelUploadResult CancelUpload(
        const CancelUploadCommand& command
    ) = 0;
};

}  // namespace tinyimx::file
