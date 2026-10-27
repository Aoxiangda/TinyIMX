#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "common/observability/SnapshotRpcTiming.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <iostream>
#include <stdexcept>

using namespace tinyimx::projection::unread;
using Timing=tinyimx::diagnostics::SnapshotRpcTiming;
namespace v1=tinyimx::message::v1;
namespace {
void Check(bool yes,const char* name){if(!yes)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
class Source final:public UnreadProjectionSource {
public:
    enum Mode {Good,Failed,Wrong,Throw};std::atomic<Mode> mode{Good};std::atomic<unsigned> calls{0};
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t u,std::uint64_t p)override{
        ++calls;const auto m=mode.load();
        if(m==Throw)throw std::runtime_error("owned snapshot failure");
        if(m==Failed)return{};
        return{true,{u,p,m==Wrong?9:2,5},{}};
    }
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t u)override{return{true,{u,{{2,2},{3,3}},5},{}};}
};
// Reserve up to a deterministic sampled next client call. Server never invokes
// NewKey, so the following actual client RPC owns exactly this expected key.
Timing::Key SelectNext(int method){
    for(unsigned i=0;i<100000;++i){
        auto k=Timing::NewKey();++k.sequence;
        if(k.sequence&&tinyimx::diagnostics::MessageTraceSelected(k.origin^k.sequence)){
            std::cout<<"PAIR_EXPECT method="<<method<<" origin="<<k.origin<<" call="<<k.sequence<<'\n';return k;
        }
    }
    throw std::runtime_error("no diagnostic sample key found");
}
}
int main(int argc,char**argv){try{
    Check(argc==3,"explicit logger path and expected diagnostic state supplied");
    const bool enabled=std::string(argv[2])=="on";
    Check(Timing::Enabled()==enabled,"snapshot diagnostic environment matches declared mode");
    Check(tinyimx::Logger::Instance().Init("warn",argv[1],false),"actual product logger initialized");
    Source source;UnreadSnapshotRpcService service(&source);
    grpc::ServerBuilder builder;int port=0;
    builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);builder.RegisterService(&service);
    auto server=builder.BuildAndStart();Check(server&&port,"actual snapshot service bound to owned loopback only");
    const auto target="127.0.0.1:"+std::to_string(port);
    UnreadSnapshotRpcClient client(target,std::chrono::milliseconds(300),true);
    if(enabled)SelectNext(1);
    const auto dialog=client.LoadDialogSnapshot(1,2);
    Check(dialog.success&&dialog.value.private_unread==2&&dialog.value.total_unread==5,"single dialog result unchanged over real transport");
    if(enabled)SelectNext(2);
    const auto user=client.LoadUserSnapshot(1);
    Check(user.success&&user.value.peer_counts.size()==2&&user.value.total_unread==5,"complete user result unchanged over real transport");
    if(enabled)SelectNext(3);
    const auto batch=client.LoadDialogSnapshots({{1,2},{4,2},{1,2}});
    Check(batch.size()==3&&batch[0].success&&batch[1].success&&batch[2].success&&batch[1].value.receiver_user_id==4,"batch order and duplicate positions unchanged");
    auto stub=v1::UnreadSnapshotService::NewStub(grpc::CreateChannel(target,grpc::InsecureChannelCredentials()));
    for(const auto* value:{"bad","0:1","1:0","1:18446744073709551616","1:2:3"}){
        grpc::ClientContext c;c.AddMetadata(Timing::MetadataHeader,value);
        v1::ReadDialogUnreadSnapshotRequest q;q.set_receiver_user_id(1);q.set_peer_user_id(2);v1::ReadDialogUnreadSnapshotResponse r;
        Check(stub->ReadDialogUnreadSnapshot(&c,q,&r).ok()&&r.private_unread()==2,"untrusted malformed diagnostic metadata cannot change business result");
    }
    {
        grpc::ClientContext c;c.AddMetadata(Timing::MetadataHeader,"1:2");c.AddMetadata(Timing::MetadataHeader,"3:4");
        v1::ReadDialogUnreadSnapshotRequest q;q.set_receiver_user_id(1);q.set_peer_user_id(2);v1::ReadDialogUnreadSnapshotResponse r;
        Check(stub->ReadDialogUnreadSnapshot(&c,q,&r).ok(),"duplicate diagnostic tags ignored without changing RPC status");
    }
    for(const auto mode:{Source::Failed,Source::Wrong,Source::Throw}){
        source.mode=mode;Check(!client.LoadDialogSnapshot(1,2).success,"unavailable corrupt and throwing sources remain explicit client failures");
    }
    source.mode=Source::Good;
    const auto before=source.calls.load();
    Check(!client.LoadDialogSnapshot(1,1).success&&source.calls==before,"invalid local dialog still makes no RPC or durable read");
    server->Shutdown();server->Wait();
    Check(!client.LoadDialogSnapshot(1,2).success,"stopped server never becomes a successful empty snapshot");
    tinyimx::Logger::Instance().Flush();tinyimx::Logger::Instance().Shutdown();
    std::cout<<"SNAPSHOT_RPC_TIMING_REAL_WIRE_"<<(enabled?"ON":"OFF")<<"_PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
