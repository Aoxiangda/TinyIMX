#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
namespace pb=tinyimx::message::v1;namespace rpc=tinyimx::rpc;
unsigned checks=0;
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);++checks;std::cout<<"PASS "<<why<<'\n';}
class Service final:public pb::MessageService::Service{
    grpc::Status PersistPrivateMessage(grpc::ServerContext*,const pb::PersistPrivateMessageRequest* q,pb::PersistPrivateMessageResponse* r)override{
        r->set_result(q->client_message_id()=="reused"?pb::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED:pb::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED);
        r->set_message_id(7100000000000001ULL);auto* row=r->mutable_record();row->set_message_id(r->message_id());
        row->set_from_user_id(q->from_user_id());row->set_to_user_id(q->to_user_id());row->set_client_message_id(q->client_message_id());
        row->set_message_type(q->message_type());row->set_content(q->content());row->set_delivery_state(pb::MESSAGE_DELIVERY_STATE_PENDING);
        if(q->client_message_id()!="old"){
            auto* s=r->mutable_committed_unread();s->set_message_id(r->message_id());s->set_receiver_user_id(q->to_user_id());s->set_peer_user_id(q->from_user_id());
            s->set_private_unread(2);s->set_total_unread(3);const auto& mode=q->client_message_id();
            if(mode=="wrong-mid")s->set_message_id(r->message_id()+1);
            if(mode=="wrong-receiver")s->set_receiver_user_id(500003);
            if(mode=="wrong-peer")s->set_peer_user_id(500003);
            if(mode=="zero")s->set_private_unread(0);
            if(mode=="inverted")s->set_private_unread(4);
            if(mode=="overflow")s->set_total_unread(std::numeric_limits<std::uint64_t>::max());
        }
        return grpc::Status::OK;
    }
};
}
int main(){try{
    using namespace std::chrono_literals;
    Service service;grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);b.RegisterService(&service);
    auto server=b.BuildAndStart();Check(server&&port,"actual owned loopback gRPC server starts");
    rpc::MessageRpcClient client(std::make_shared<rpc::StaticServiceEndpointProvider>("","","127.0.0.1:"+std::to_string(port)));
    for(bool async:{false,true})for(const auto* mode:{"valid","old","wrong-mid","wrong-receiver","wrong-peer","zero","inverted","overflow","reused"}){
        rpc::PersistPrivateMessageRpcRequest q;q.from_user_id=500001;q.to_user_id=500002;q.client_message_id=mode;q.message_type=1;q.content="owned";
        rpc::RpcCallOptions options;options.remaining_timeout=3s;
        rpc::PersistPrivateMessageRpcCallResult r;
        if(!async)r=client.PersistPrivateMessage(q,options);
        else {std::promise<rpc::PersistPrivateMessageRpcCallResult> promise;auto future=promise.get_future();client.PersistPrivateMessageAsync(q,options,[&](auto value){promise.set_value(std::move(value));});
            Check(future.wait_for(4s)==std::future_status::ready,"async transport completes within watchdog");r=future.get();}
        const bool old=std::string_view(mode)=="old",valid=std::string_view(mode)=="valid";
        if(old||valid)Check(r.ok()&&r.value->Created()&&r.value->committed_unread.has_value()==valid,"sync/async support both new and older reply schemas");
        else Check(!r.ok()&&r.status.code==rpc::RpcErrorCode::kDataLoss,"sync/async reject wrong identity, invalid counters or Reused metadata");
    }
    server->Shutdown();server->Wait();std::cout<<"COMMITTED_UNREAD_ACTUAL_RPC_CONTRACTS_PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
