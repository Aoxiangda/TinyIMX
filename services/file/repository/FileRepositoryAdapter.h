#pragma once

#include "services/file/application/FileRepositoryPort.h"

namespace tinyimx {
class FileRepository;
class MySqlConnectionPool;
}

namespace tinyimx::file {

class FileRepositoryAdapter final : public FileRepositoryPort {
public:
    FileRepositoryAdapter(
        tinyimx::FileRepository* repository,
        tinyimx::MySqlConnectionPool* pool
    );

    [[nodiscard]] BeginUploadResult BeginUpload(
        const BeginUploadCommand& command
    ) override;

    [[nodiscard]] GetUploadSessionResult GetUploadSession(
        const GetUploadSessionQuery& query
    ) override;

    [[nodiscard]] CancelUploadResult CancelUpload(
        const CancelUploadCommand& command
    ) override;

private:
    tinyimx::FileRepository* repository_{nullptr};  // non-owning
    tinyimx::MySqlConnectionPool* pool_{nullptr};  // non-owning
};

}  // namespace tinyimx::file
