#pragma once

#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/UserRpcTypes.h"

#include "tinyimx/user/v1/user_service.grpc.pb.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace grpc {
class Channel;
}

namespace tinyimx::rpc {

class UserRpcClient final {
public:
    explicit UserRpcClient(
        std::shared_ptr<const ServiceEndpointProvider>
            endpoint_provider
    );

    UserRpcClient(const UserRpcClient&) = delete;
    UserRpcClient& operator=(const UserRpcClient&) = delete;

    [[nodiscard]] RpcResult<AuthenticateRpcResponse> Authenticate(
        const AuthenticateRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<GetUserProfileRpcResponse> GetUserProfile(
        const GetUserProfileRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    // M15-B acceptance introspection. These do not perform network I/O and
    // are intentionally read-only so tests can prove multi-target reuse and
    // bounded cache growth.
    [[nodiscard]] std::size_t CachedTargetCountForTest() const;
    [[nodiscard]] std::uint64_t StubCreationCountForTest() const;

private:
    using UserStubInterface =
        tinyimx::user::v1::UserService::StubInterface;

    [[nodiscard]] std::shared_ptr<UserStubInterface>
    GetOrCreateStub(
        const ServiceEndpoint& endpoint
    ) const;

    [[nodiscard]] static RpcStatus MapGrpcStatus(
        const grpc::Status& status
    );

private:
    const std::shared_ptr<const ServiceEndpointProvider>
        endpoint_provider_;

    struct CachedStubEntry {
        std::shared_ptr<grpc::Channel> channel;
        std::shared_ptr<UserStubInterface> stub;
        std::uint64_t last_used{0};
    };

    static constexpr std::size_t kMaxCachedTargets = 16;

    // Protects cache lookup/replacement only. Never held during a network RPC.
    mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::string, CachedStubEntry> stub_cache_;
    mutable std::uint64_t cache_use_sequence_{0};
    mutable std::uint64_t stub_creation_count_{0};
};

}  // namespace tinyimx::rpc
