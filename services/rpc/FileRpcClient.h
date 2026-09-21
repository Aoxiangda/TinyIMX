#pragma once

#include "services/rpc/FileRpcTypes.h"
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace grpc { class Channel; }

namespace tinyimx::rpc {

class FileRpcClient final {
public:
    explicit FileRpcClient(std::shared_ptr<const ServiceEndpointProvider> endpoint_provider);
    FileRpcClient(const FileRpcClient&) = delete;
    FileRpcClient& operator=(const FileRpcClient&) = delete;

    [[nodiscard]] RpcResult<BeginUploadRpcResponse> BeginUpload(
        const BeginUploadRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<GetUploadSessionRpcResponse> GetUploadSession(
        const GetUploadSessionRpcRequest&, const RpcCallOptions&) const;
    [[nodiscard]] RpcResult<CancelUploadRpcResponse> CancelUpload(
        const CancelUploadRpcRequest&, const RpcCallOptions&) const;

    [[nodiscard]] std::size_t CachedTargetCountForTest() const;
    [[nodiscard]] std::uint64_t StubCreationCountForTest() const;

private:
    using StubInterface = tinyimx::file::v1::FileService::StubInterface;
    struct CachedStubEntry {
        std::shared_ptr<grpc::Channel> channel;
        std::shared_ptr<StubInterface> stub;
        std::uint64_t last_used{0};
    };
    static constexpr std::size_t kMaxCachedTargets = 16;

    [[nodiscard]] std::shared_ptr<StubInterface> GetOrCreateStub(
        const ServiceEndpoint& endpoint) const;
    [[nodiscard]] static RpcStatus MapGrpcStatus(const grpc::Status& status);

    std::shared_ptr<const ServiceEndpointProvider> endpoint_provider_;
    mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::string, CachedStubEntry> stub_cache_;
    mutable std::uint64_t cache_use_sequence_{0};
    mutable std::uint64_t stub_creation_count_{0};
};

}  // namespace tinyimx::rpc
