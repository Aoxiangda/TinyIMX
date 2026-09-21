#pragma once

#include "services/file/application/FileApplicationService.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"

namespace tinyimx::file {

class FileServiceImpl final : public tinyimx::file::v1::FileService::Service {
public:
    explicit FileServiceImpl(FileApplicationService* application_service);

    grpc::Status BeginUpload(
        grpc::ServerContext* context,
        const tinyimx::file::v1::BeginUploadRequest* request,
        tinyimx::file::v1::BeginUploadResponse* response
    ) override;

    grpc::Status GetUploadSession(
        grpc::ServerContext* context,
        const tinyimx::file::v1::GetUploadSessionRequest* request,
        tinyimx::file::v1::GetUploadSessionResponse* response
    ) override;

    grpc::Status CancelUpload(
        grpc::ServerContext* context,
        const tinyimx::file::v1::CancelUploadRequest* request,
        tinyimx::file::v1::CancelUploadResponse* response
    ) override;

private:
    FileApplicationService* application_service_{nullptr};  // non-owning
};

}  // namespace tinyimx::file
