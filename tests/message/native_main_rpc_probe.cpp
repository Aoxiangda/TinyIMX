#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace tinyimx::message;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);std::cout<<"PASS "<<text<<std::endl;}
template<class F>grpc::Status Call(F&& f){grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));return f(&c);}
}
int main(int argc,char**argv){
    try{
        if(argc!=3)return 2;const std::string target=argv[1],mode=argv[2];if(mode!="seed"&&mode!="verify")return 2;
        auto channel=grpc::CreateChannel(target,grpc::InsecureChannelCredentials());Check(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(5)),"real independent Message process connected");auto client=v1::MessageService::NewStub(channel);
        std::vector<std::uint64_t> mids;
        for(unsigned i=0;i<12;++i){
            v1::PersistPrivateMessageRequest q;q.set_from_user_id(i<8?500001:500003);q.set_to_user_id(500002);q.set_client_message_id("native-main-r1-"+std::to_string(i));q.set_message_type(1);q.set_content("native main durable "+std::to_string(i));v1::PersistPrivateMessageResponse r;
            Check(Call([&](auto*c){return client->PersistPrivateMessage(c,q,&r);}).ok()&&r.result()==(mode=="seed"?v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED:v1::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED)&&r.record().content()==q.content(),"original persist RPC has expected durable/CID outcome");mids.push_back(r.message_id());
            if(i)Check(mids[i]>mids[i-1],"per-owner durable message identities ordered");
        }
        v1::PersistPrivateMessageRequest conflict;conflict.set_from_user_id(500001);conflict.set_to_user_id(500002);conflict.set_client_message_id("native-main-r1-0");conflict.set_message_type(1);conflict.set_content("conflicting body");v1::PersistPrivateMessageResponse cr;
        Check(Call([&](auto*c){return client->PersistPrivateMessage(c,conflict,&cr);}).ok()&&cr.result()==v1::PERSIST_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT&&cr.message_id()==mids[0],"original RPC rejects CID payload conflict without a second message");
        if(mode=="seed"){
            v1::ConfirmReceiverBatchRequest q;q.set_receiver_user_id(500003);for(unsigned i=0;i<4;++i)q.add_message_ids(mids[i]);v1::MessageMutationResponse r;
            Check(Call([&](auto*c){return client->ConfirmReceiverBatch(c,q,&r);}).error_code()==grpc::StatusCode::PERMISSION_DENIED,"foreign receiver batch rejected before any partial mutation");
            q.set_receiver_user_id(500002);Check(Call([&](auto*c){return client->ConfirmReceiverBatch(c,q,&r);}).ok()&&r.affected_rows()==4,"real owner confirms four receiver messages atomically");
            v1::MarkDialogReadRequest read;read.set_reader_user_id(500002);read.set_peer_user_id(500001);v1::MessageMutationResponse changed;
            Check(Call([&](auto*c){return client->MarkDialogRead(c,read,&changed);}).ok()&&changed.affected_rows()==8,"real read RPC updates all eight messages and unread truth");
        }
        v1::CountPendingRequest pending;pending.set_to_user_id(500002);v1::CountPendingResponse count;
        Check(Call([&](auto*c){return client->CountPending(c,pending,&count);}).ok()&&count.count()==4,"receiver pending index survives original RPC composition");
        v1::ListHistoryRequest history;history.set_actor_user_id(500002);history.set_peer_user_id(500001);history.set_limit(20);v1::ListHistoryResponse page;
        Check(Call([&](auto*c){return client->ListHistory(c,history,&page);}).ok()&&page.messages_size()==8,"original history RPC resolves the native owner instead of empty SQL");
        for(int i=1;i<page.messages_size();++i)Check(page.messages(i-1).message_id()<page.messages(i).message_id(),"history RPC preserves client-required ascending MID order");
        tinyimx::projection::unread::UnreadSnapshotRpcClient source(target);
        const auto a=source.LoadDialogSnapshot(500002,500001),b=source.LoadDialogSnapshot(500002,500003);const auto user=source.LoadUserSnapshot(500002);
        Check(a.success&&b.success&&user.success&&a.value.private_unread==0&&b.value.private_unread==4&&user.value.total_unread==4&&user.value.peer_counts.size()==2,"independent projector source exposes complete zero/nonzero peer snapshot");
        std::cout<<"NATIVE_MESSAGE_MAIN_RPC_PROBE_PASS "<<mode<<std::endl;return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
