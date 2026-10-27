#pragma once

#include "common/runtime/BoundedReadyBatch.h"
#include "common/runtime/BoundedKeyedReadyBatch.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/message/service/MessageReadyBatchPolicy.h"
#include "common/observability/RpcExecutionTrace.h"
#include "common/logging/LogMacros.h"
#include <mutex>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace tinyimx::message {
using MessageReadyCallbackBase=v1::MessageService::WithCallbackMethod_PersistPrivateMessage<MessageServiceImpl>;

// Experimental private-persist path only. Existing request/response messages,
// remaining unary methods and Gateway ordering/permission checks are retained.
// Unlike the caller-led batcher, no gRPC thread waits on the commit result.
class MessageReadyBatchService final:public MessageReadyCallbackBase {
    class Reactor;
    using Queue=BoundedReadyBatch<Reactor*,grpc::Status>;
    using KeyedQueue=BoundedKeyedReadyBatch<Reactor*,grpc::Status>;
public:
    // Observation only: lets a contract wait for server-side cancellation
    // instead of assuming the client's timeout notification reached us.
    using CancelObserver=std::function<void(std::string_view)>;
    MessageReadyBatchService(MessageApplicationService* app,rpc::GroupRpcClient* group,
                            std::size_t shards,std::size_t capacity=128,CancelObserver observer={})
        {
        ConfigureDependencies(app,group);
        if(observer)cancel_observer_=std::make_shared<const CancelObserver>(std::move(observer));
        const Queue::Options options{shards,capacity,ConfiguredPrivateReadyBatchLimit()};
        const auto execute=[this](const auto& calls){return Execute(calls);};
        const grpc::Status failure(grpc::StatusCode::UNAVAILABLE,"private ready batch failed; resolve durable CID");
        if(ConfiguredPrivateReadyKeyedScheduler())keyed_queue_=std::make_unique<KeyedQueue>(options,execute,failure);
        else queue_=std::make_unique<Queue>(options,execute,failure);
    }
    ~MessageReadyBatchService(){Drain();}
    grpc::ServerUnaryReactor* PersistPrivateMessage(grpc::CallbackServerContext* context,
        const v1::PersistPrivateMessageRequest* request,v1::PersistPrivateMessageResponse* response)override {
        auto* reactor=new Reactor(context,request,response,cancel_observer_);
        try {
            // CID uniqueness is scoped by sender even across conversations.
            // Both schedulers preserve sender CID order. The keyed scheduler
            // lets free workers process other ready senders while one waits.
            auto completion=[reactor](grpc::Status status)noexcept{reactor->Complete(std::move(status));};
            const auto admitted=keyed_queue_?keyed_queue_->Submit(request->from_user_id(),reactor,completion):
                queue_->Submit(request->from_user_id(),reactor,completion);
            if(admitted==Queue::Admission::Accepted)return reactor;
            reactor->Complete({grpc::StatusCode::RESOURCE_EXHAUSTED,
                admitted==Queue::Admission::Stopped?"private ready batch is draining":"private ready batch is full"});
        }catch(...){reactor->Complete({grpc::StatusCode::RESOURCE_EXHAUSTED,"private ready batch admission failed"});}
        return reactor;
    }
    void Drain()noexcept {
        std::call_once(drained_,[this]{
            if(keyed_queue_)keyed_queue_->Drain();else queue_->Drain();const auto s=Stats();
            try{
                std::string histogram;
                for(std::size_t i=1;i<s.batch_sizes.size();++i)if(s.batch_sizes[i]){
                    if(!histogram.empty())histogram+=',';
                    histogram+=std::to_string(i)+':'+std::to_string(s.batch_sizes[i]);
                }
                LOG_WARN("private_ready_batch_stopped accepted="<<s.accepted<<" completed="<<s.completed
                    <<" overloaded="<<s.overloaded<<" stopped="<<s.stopped<<" pending="<<s.pending
                    <<" peak_pending="<<s.peak_pending<<" batches="<<s.batches
                    <<" execute_failures="<<s.execute_failures<<" completion_failures="<<s.completion_failures
                    <<" batch_histogram="<<histogram
                    <<" scheduler="<<(keyed_queue_?"keyed_ready":"legacy_sender_modulo"));
            }catch(...){/* Final telemetry cannot change durable completion. */}
        });
    }
    Queue::Stats Stats()const noexcept{return keyed_queue_?keyed_queue_->Snapshot():queue_->Snapshot();}
private:
    class Reactor final:public grpc::ServerUnaryReactor {
    public:
        Reactor(grpc::CallbackServerContext* context,const v1::PersistPrivateMessageRequest* request,
                v1::PersistPrivateMessageResponse* response,std::shared_ptr<const CancelObserver> observer)
            :view{context,request,response},trace(1,true,0,request->from_user_id(),request->to_user_id()),cancel_observer_(std::move(observer)){}
        void Complete(grpc::Status status)noexcept {
            trace.Finish(static_cast<int>(status.error_code()),view.response->message_id());
            Finish(status); // OnDone may delete this; no accesses after Finish.
        }
        PrivatePersistCallView view;
        diagnostics::RpcExecutionTrace trace;
    private:
        void OnCancel()override{
            if(cancel_observer_&&view.request){
                try{(*cancel_observer_)(view.request->client_message_id());}catch(...){/* Observation never changes a mutation outcome. */}
            }
        }
        void OnDone()override{delete this;}
        std::shared_ptr<const CancelObserver>cancel_observer_;
    };
    std::vector<grpc::Status> Execute(const std::vector<Reactor*>& calls) {
        std::vector<PrivatePersistCallView> views;views.reserve(calls.size());
        for(auto* call:calls){call->trace.StartSharedBatch();views.push_back(call->view);}
        return PersistPrivateMessageBatchCore(views);
    }
    std::unique_ptr<Queue>queue_;
    std::unique_ptr<KeyedQueue>keyed_queue_;
    std::shared_ptr<const CancelObserver>cancel_observer_;
    std::once_flag drained_;
};
} // namespace tinyimx::message
