#pragma once
#include "services/message/service/MessageServiceImpl.h"
#include "services/message/service/BoundedUnaryRpcDispatcher.h"

namespace tinyimx::message {
using MessageHotCallbackBase = v1::MessageService::WithCallbackMethod_PersistPrivateMessage<
    v1::MessageService::WithCallbackMethod_GetPrivateMessage<
    v1::MessageService::WithCallbackMethod_ConfirmReceiver<MessageServiceImpl>>>;

// Only three hot unary RPCs change transport execution model. All other RPCs
// keep the original sync implementation. SQL/Outbox/application methods are shared.
class MessageCallbackService final : public MessageHotCallbackBase {
public:
    MessageCallbackService(MessageApplicationService* app, rpc::GroupRpcClient* group,
                           std::size_t workers, std::size_t queued = 128)
        : dispatcher_(workers, queued) { ConfigureDependencies(app, group); }
    void Drain() { dispatcher_.Drain(); }

    grpc::ServerUnaryReactor* PersistPrivateMessage(grpc::CallbackServerContext* c,
        const v1::PersistPrivateMessageRequest* q, v1::PersistPrivateMessageResponse* r) override {
        return dispatcher_.Dispatch(c, [this,c,q,r] { return PersistPrivateMessageCore(c,q,r); },
            1,0,q->from_user_id(),q->to_user_id(),[r] {return r->message_id();});
    }
    grpc::ServerUnaryReactor* GetPrivateMessage(grpc::CallbackServerContext* c,
        const v1::GetPrivateMessageRequest* q, v1::GetPrivateMessageResponse* r) override {
        return dispatcher_.Dispatch(c, [this,c,q,r] { return GetPrivateMessageCore(c,q,r); },3,q->message_id());
    }
    grpc::ServerUnaryReactor* ConfirmReceiver(grpc::CallbackServerContext* c,
        const v1::ConfirmReceiverRequest* q, v1::MessageMutationResponse* r) override {
        return dispatcher_.Dispatch(c, [this,c,q,r] { return ConfirmReceiverCore(c,q,r); },2,q->message_id());
    }
private:
    BoundedUnaryRpcDispatcher dispatcher_;
};
} // namespace tinyimx::message
