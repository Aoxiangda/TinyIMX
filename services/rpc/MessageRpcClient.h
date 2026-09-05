#pragma once

#include "services/rpc/MessageRpcTypes.h"
#include "services/rpc/RpcCallOptions.h"
#include "services/rpc/ServiceEndpointProvider.h"

#include "tinyimx/message/v1/message_service.grpc.pb.h"

#include <memory>
#include <mutex>
#include <string>

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

private:
    using MessageStubInterface =
        tinyimx::message::v1::MessageService::StubInterface;

    [[nodiscard]] std::shared_ptr<MessageStubInterface> GetOrCreateStub(
        const ServiceEndpoint& endpoint
    ) const;

    static RpcStatus MapGrpcStatus(const grpc::Status& status);

private:
    const std::shared_ptr<const ServiceEndpointProvider> endpoint_provider_;

    // Protect cache replacement only. Never held during synchronous network IO.
    mutable std::mutex cache_mutex_;
    mutable std::string cached_target_;
    mutable std::shared_ptr<grpc::Channel> cached_channel_;
    mutable std::shared_ptr<MessageStubInterface> cached_stub_;
};

}  // namespace tinyimx::rpc
