#include "services/message/local/LocalMessageRepositoryAdapter.h"
#include "services/message/local/LocalUnreadProjectionSource.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::projection::unread;
namespace {
void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
std::unique_ptr<grpc::Server> Start(grpc::Service* service,int&port){
    grpc::ServerBuilder builder;builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);builder.RegisterService(service);
    auto server=builder.BuildAndStart();if(!server||!port)throw std::runtime_error("owned loopback server failed");return server;
}
std::string Target(int port){return "127.0.0.1:"+std::to_string(port);}
class WrongWire final:public v1::UnreadSnapshotService::Service {
    grpc::Status ReadDialogUnreadSnapshot(grpc::ServerContext*,const v1::ReadDialogUnreadSnapshotRequest*q,v1::ReadDialogUnreadSnapshotResponse*r)override{
        r->set_receiver_user_id(q->receiver_user_id()+1);r->set_peer_user_id(q->peer_user_id());return grpc::Status::OK;
    }
    grpc::Status ReadUserUnreadSnapshot(grpc::ServerContext*,const v1::ReadUserUnreadSnapshotRequest*q,v1::ReadUserUnreadSnapshotResponse*r)override{
        r->set_receiver_user_id(q->receiver_user_id());r->set_total_unread(0);r->add_peers()->set_peer_user_id(500003);r->add_peers()->set_peer_user_id(500001);return grpc::Status::OK;
    }
};
class InvalidSource final:public UnreadProjectionSource {
public:
    bool slow{false};
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t receiver,std::uint64_t peer)override{
        if(slow)std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return{true,{receiver,peer,slow?0:2,slow?0:1},""};
    }
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t receiver)override{return{true,{receiver,{{500001,1}},0},""};}
};
}
int main(int argc,char**argv){
    try{
        if(argc!=2)return 2;const std::filesystem::path root=argv[1];
        Check(root.is_absolute()&&(root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf105-")||root.string().starts_with("/mnt/tinyimx-benchmark/perf943-contracts-"))&&std::filesystem::canonical(root)==root,"owned physical loopback fixture");
        LocalPrivateStoreOptions options;options.directory=(root/"rpc-store").string();options.storage_identity="perf105-rpc-"+root.filename().string();
        options.first_message_id=7090000000000000ULL;options.message_events_topic="codex_perf105_events_20261020_r1";
        options.initial_user_ids={500001,500002,500003,500004};options.block_cache_bytes=8*1024*1024;options.exact_user_locking=true;
        LocalPrivateStore store(options);MessageRepositoryAdapter group(nullptr,nullptr,nullptr);
        LocalMessageRepositoryAdapter adapter(&store,&group);MessageApplicationService app(&adapter);LocalUnreadProjectionSource source(&store);
        MessageServiceImpl messages(&app);UnreadSnapshotRpcService snapshots(&source);MessageServiceServer server(&messages);
        Check(server.SetAuxiliaryService(&snapshots)&&!server.SetAuxiliaryService(&snapshots),"one auxiliary service configured before socket starts");
        Check(server.Start("127.0.0.1:0")&&server.SetReady(true)&&!server.SetAuxiliaryService(&snapshots),"original MessageService starts with appended snapshot service");
        auto channel=grpc::CreateChannel(server.BoundTarget(),grpc::InsecureChannelCredentials());
        Check(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(3)),"real loopback channel connected");
        auto client=v1::MessageService::NewStub(channel);UnreadSnapshotRpcClient reader(server.BoundTarget(),std::chrono::milliseconds(300));
        UnreadSnapshotRpcClient batch_reader(server.BoundTarget(),std::chrono::milliseconds(300),true);
        const std::vector<std::pair<std::uint64_t,std::uint64_t>> dialogs{{500002,500001},{500003,500001},{500002,500001}};
        v1::PersistPrivateMessageRequest q;q.set_from_user_id(500001);q.set_to_user_id(500002);q.set_client_message_id("rpc-owned-cid");q.set_message_type(1);q.set_content("real loopback durable record");
        v1::PersistPrivateMessageResponse r;grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        Check(client->PersistPrivateMessage(&c,q,&r).ok()&&r.message_id()>=options.first_message_id,"existing persist RPC reaches native application owner");const auto mid=r.message_id();
        auto dialog=reader.LoadDialogSnapshot(500002,500001);auto user=reader.LoadUserSnapshot(500002);
        Check(dialog.success&&dialog.value.private_unread==1&&dialog.value.total_unread==1,"remote dialog snapshot reflects durable write");
        Check(user.success&&user.value.peer_counts==std::vector<std::pair<std::uint64_t,std::int64_t>>{{500001,1}}&&user.value.total_unread==1,"remote complete user snapshot matches owner");
        auto batch=batch_reader.LoadDialogSnapshots(dialogs);
        Check(batch.size()==3&&batch[0].success&&batch[1].success&&batch[2].success&&batch[0].value.private_unread==1&&batch[1].value.private_unread==0&&batch[2].value.private_unread==1&&batch[1].value.receiver_user_id==500003,"one real batch reads durable native owner, empty dialog and duplicate dialog positions");
        v1::ConfirmReceiverBatchRequest ack;ack.set_receiver_user_id(500002);ack.add_message_ids(mid);v1::MessageMutationResponse changed;grpc::ClientContext ac;
        Check(client->ConfirmReceiverBatch(&ac,ack,&changed).ok()&&changed.affected_rows()==1&&reader.LoadDialogSnapshot(500002,500001).value.private_unread==1,"original batch ACK RPC preserves unread semantics");
        v1::MarkDialogReadRequest read;read.set_reader_user_id(500002);read.set_peer_user_id(500001);grpc::ClientContext rc;changed.Clear();
        Check(client->MarkDialogRead(&rc,read,&changed).ok()&&changed.affected_rows()==1,"original dialog-read RPC updates native state");
        user=reader.LoadUserSnapshot(500002);Check(user.success&&user.value.total_unread==0&&user.value.peer_counts==std::vector<std::pair<std::uint64_t,std::int64_t>>{{500001,0}},"remote rebuild snapshot retains zero-count historical peer");
        batch=batch_reader.LoadDialogSnapshots(dialogs);
        Check(batch.size()==3&&batch[0].success&&batch[1].success&&batch[2].success&&batch[0].value.private_unread==0&&batch[2].value.total_unread==0,"batch reflects original mark-read mutation without SQL fallback");
        Check(reader.LoadUserSnapshot(500003).success&&reader.LoadUserSnapshot(500003).value.peer_counts.empty(),"remote empty user snapshot remains complete");
        Check(!reader.LoadDialogSnapshot(0,500001).success&&!reader.LoadDialogSnapshot(500002,500002).success&&!reader.LoadUserSnapshot(0).success,"invalid remote snapshot input rejected");
        v1::ListHistoryRequest history;history.set_actor_user_id(500002);history.set_peer_user_id(500001);history.set_limit(10);v1::ListHistoryResponse page;grpc::ClientContext hc;
        Check(client->ListHistory(&hc,history,&page).ok()&&page.messages_size()==1&&page.messages(0).message_id()==mid,"existing history RPC remains on the same owner");
        WrongWire bad;int bad_port=0;auto bad_server=Start(&bad,bad_port);UnreadSnapshotRpcClient bad_client(Target(bad_port),std::chrono::milliseconds(300));
        Check(!bad_client.LoadDialogSnapshot(500002,500001).success&&!bad_client.LoadUserSnapshot(500002).success,"client rejects OK wire responses with wrong actor or unordered peers");
        bad_server->Shutdown();bad_server->Wait();
        InvalidSource invalid;UnreadSnapshotRpcService checked(&invalid);int check_port=0;auto checked_server=Start(&checked,check_port);
        UnreadSnapshotRpcClient checked_client(Target(check_port),std::chrono::milliseconds(300));
        Check(!checked_client.LoadDialogSnapshot(500002,500001).success&&!checked_client.LoadUserSnapshot(500002).success,"server rejects inconsistent durable counts and totals");
        checked_server->Shutdown();checked_server->Wait();
        InvalidSource slow;slow.slow=true;UnreadSnapshotRpcService delayed(&slow);int slow_port=0;auto slow_server=Start(&delayed,slow_port);
        UnreadSnapshotRpcClient deadline(Target(slow_port),std::chrono::milliseconds(10));
        Check(!deadline.LoadDialogSnapshot(500002,500001).success,"deadline failure never becomes an empty successful snapshot");
        slow_server->Shutdown();slow_server->Wait();
        Check(store.Shutdown()&&!reader.LoadDialogSnapshot(500002,500001).success&&!reader.LoadUserSnapshot(500002).success,"closed owner fails remote reads without SQL fallback");
        batch=batch_reader.LoadDialogSnapshots(dialogs);
        Check(batch.size()==3&&!batch[0].success&&!batch[1].success&&!batch[2].success,"closed native owner fails entire batch without successful empty rows");
        server.Shutdown();server.Wait();Check(!reader.LoadUserSnapshot(500002).success,"stopped server remains an explicit read failure");
        batch=batch_reader.LoadDialogSnapshots(dialogs);
        Check(batch.size()==3&&!batch[0].success&&!batch[1].success&&!batch[2].success,"normal server shutdown leaves all batch reads explicitly failed");
        std::cout<<"LOCAL_UNREAD_OWNER_RPC_INTEGRATION_PASS\n";return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
