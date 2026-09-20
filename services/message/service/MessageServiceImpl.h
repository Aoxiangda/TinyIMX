#pragma once

#include "tinyimx/message/v1/message_service.grpc.pb.h"

namespace tinyimx::rpc { class GroupRpcClient; }

namespace tinyimx::message {

class MessageApplicationService;

class MessageServiceImpl final
    : public tinyimx::message::v1::MessageService::Service {
public:
    explicit MessageServiceImpl(
        MessageApplicationService* application_service,
        tinyimx::rpc::GroupRpcClient* group_rpc_client = nullptr
    );

    grpc::Status PersistPrivateMessage(
        grpc::ServerContext* context,
        const tinyimx::message::v1::PersistPrivateMessageRequest* request,
        tinyimx::message::v1::PersistPrivateMessageResponse* response
    ) override;

    grpc::Status PersistGroupMessage(
        grpc::ServerContext* context,
        const tinyimx::message::v1::PersistGroupMessageRequest* request,
        tinyimx::message::v1::PersistGroupMessageResponse* response
    ) override;

    grpc::Status GetGroupMessageDelivery(
        grpc::ServerContext*,
        const tinyimx::message::v1::GetGroupMessageDeliveryRequest*,
        tinyimx::message::v1::GetGroupMessageDeliveryResponse*
    ) override;

    grpc::Status ClaimGroupMessageDeliveries(
        grpc::ServerContext*,
        const tinyimx::message::v1::ClaimGroupMessageDeliveriesRequest*,
        tinyimx::message::v1::ClaimGroupMessageDeliveriesResponse*
    ) override;

    grpc::Status ClaimGroupMessageDeliveriesForRecipient(
        grpc::ServerContext*,
        const tinyimx::message::v1::ClaimGroupMessageDeliveriesForRecipientRequest*,
        tinyimx::message::v1::ClaimGroupMessageDeliveriesResponse*
    ) override;

    grpc::Status CompleteGroupMessageDeliveryAttempt(
        grpc::ServerContext*,
        const tinyimx::message::v1::CompleteGroupMessageDeliveryAttemptRequest*,
        tinyimx::message::v1::MessageMutationResponse*
    ) override;

    grpc::Status ConfirmGroupMessageDelivery(
        grpc::ServerContext*,
        const tinyimx::message::v1::ConfirmGroupMessageDeliveryRequest*,
        tinyimx::message::v1::MessageMutationResponse*
    ) override;

    grpc::Status GetPrivateMessage(
        grpc::ServerContext* context,
        const tinyimx::message::v1::GetPrivateMessageRequest* request,
        tinyimx::message::v1::GetPrivateMessageResponse* response
    ) override;

    grpc::Status ListHistory(
        grpc::ServerContext* context,
        const tinyimx::message::v1::ListHistoryRequest* request,
        tinyimx::message::v1::ListHistoryResponse* response
    ) override;

    grpc::Status ListConversations(
        grpc::ServerContext* context,
        const tinyimx::message::v1::ListConversationsRequest* request,
        tinyimx::message::v1::ListConversationsResponse* response
    ) override;

    grpc::Status CountPending(
        grpc::ServerContext* context,
        const tinyimx::message::v1::CountPendingRequest* request,
        tinyimx::message::v1::CountPendingResponse* response
    ) override;

    grpc::Status ListPendingAfter(
        grpc::ServerContext* context,
        const tinyimx::message::v1::ListPendingAfterRequest* request,
        tinyimx::message::v1::ListPendingAfterResponse* response
    ) override;

    grpc::Status ConfirmReceiver(
        grpc::ServerContext* context,
        const tinyimx::message::v1::ConfirmReceiverRequest* request,
        tinyimx::message::v1::MessageMutationResponse* response
    ) override;

    grpc::Status ConfirmReceiverBatch(
        grpc::ServerContext* context,
        const tinyimx::message::v1::ConfirmReceiverBatchRequest* request,
        tinyimx::message::v1::MessageMutationResponse* response
    ) override;

    grpc::Status MarkDialogRead(
        grpc::ServerContext* context,
        const tinyimx::message::v1::MarkDialogReadRequest* request,
        tinyimx::message::v1::MessageMutationResponse* response
    ) override;

private:
    MessageApplicationService* application_service_{nullptr};  // non-owning
    tinyimx::rpc::GroupRpcClient* group_rpc_client_{nullptr};   // non-owning
};

}  // namespace tinyimx::message
