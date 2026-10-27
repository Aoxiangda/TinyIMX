#pragma once

#include "services/rpc/SocialRpcTypes.h"
#include <functional>

namespace tinyimx::rpc {

// Optional same-process composition at the authenticated Gateway boundary.
// Bootstrap owns the provider until all accepted Gateway work has drained.
class PrivateChatPermissionPort {
public:
    using Completion=std::function<void(RpcResult<CheckPrivateChatPermissionRpcResponse>)>;
    virtual ~PrivateChatPermissionPort() = default;
    [[nodiscard]] virtual RpcResult<CheckPrivateChatPermissionRpcResponse>
    CheckPrivateChatPermission(const CheckPrivateChatPermissionRpcRequest& request,
                               const RpcCallOptions& options) = 0;
    [[nodiscard]] virtual bool SupportsAsync() const noexcept { return false; }
    // Callbacks are nonblocking continuations. Ownership remains with the
    // bootstrap until accepted reads and callbacks have drained.
    virtual void CheckPrivateChatPermissionAsync(
        const CheckPrivateChatPermissionRpcRequest&,const RpcCallOptions&,Completion done) {
        if(done)done(RpcResult<CheckPrivateChatPermissionRpcResponse>::Failure(
            RpcErrorCode::kUnavailable,"asynchronous local permission is unsupported"));
    }
};

}  // namespace tinyimx::rpc
