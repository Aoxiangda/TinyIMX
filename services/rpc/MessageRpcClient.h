#pragma once

#include "services/rpc/MessageRpcTypes.h"
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"

#include "tinyimx/message/v1/message_service.grpc.pb.h"

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

class MessageRpcClient {
public:
    explicit MessageRpcClient(
        std::shared_ptr<const ServiceEndpointProvider> endpoint_provider
    );

    MessageRpcClient(const MessageRpcClient&) = delete;
    MessageRpcClient& operator=(const MessageRpcClient&) = delete;

    [[nodiscard]] PersistPrivateMessageRpcCallResult PersistPrivateMessage(
        const PersistPrivateMessageRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<GetPrivateMessageRpcResponse> GetPrivateMessage(
        const GetPrivateMessageRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<ListHistoryRpcResponse> ListHistory(
        const ListHistoryRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<ListConversationsRpcResponse> ListConversations(
        const ListConversationsRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<CountPendingRpcResponse> CountPending(
        const CountPendingRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] RpcResult<ListPendingAfterRpcResponse> ListPendingAfter(
        const ListPendingAfterRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] MessageMutationRpcCallResult ConfirmReceiver(
        const ConfirmReceiverRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] MessageMutationRpcCallResult ConfirmReceiverBatch(
        const ConfirmReceiverBatchRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    [[nodiscard]] MessageMutationRpcCallResult MarkDialogRead(
        const MarkDialogReadRpcRequest& request,
        const RpcCallOptions& options
    ) const;

    // M15-B acceptance introspection. These do not perform network I/O and
    // are intentionally read-only so tests can prove multi-target reuse and
    // bounded cache growth.
    [[nodiscard]] std::size_t CachedTargetCountForTest() const;
    [[nodiscard]] std::uint64_t StubCreationCountForTest() const;

private:
    using MessageStubInterface =
        tinyimx::message::v1::MessageService::StubInterface;

    [[nodiscard]] std::shared_ptr<MessageStubInterface> GetOrCreateStub(
        const ServiceEndpoint& endpoint
    ) const;

    static RpcStatus MapGrpcStatus(const grpc::Status& status);

private:
    const std::shared_ptr<const ServiceEndpointProvider> endpoint_provider_;

    struct CachedStubEntry {
        std::shared_ptr<grpc::Channel> channel;
        std::shared_ptr<MessageStubInterface> stub;
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
