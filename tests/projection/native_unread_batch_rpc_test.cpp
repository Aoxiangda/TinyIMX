#include "services/projection/unread/UnreadSnapshotRpc.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace tinyimx::projection::unread;
namespace v1=tinyimx::message::v1;
using Dialogs=std::vector<std::pair<std::uint64_t,std::uint64_t>>;
using Results=std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>>;
namespace {
void Check(bool yes,const char*name){if(!yes)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
bool AllFailed(const Results&r,std::size_t n){return r.size()==n&&std::all_of(r.begin(),r.end(),[](const auto&v){return !v.success;});}
bool Matches(const Results&r,const Dialogs&d){
    if(r.size()!=d.size())return false;
    for(std::size_t i=0;i<d.size();++i)if(!r[i].success||r[i].value.receiver_user_id!=d[i].first||r[i].value.peer_user_id!=d[i].second||r[i].value.private_unread!=2||r[i].value.total_unread!=5)return false;
    return true;
}
std::unique_ptr<grpc::Server> Start(grpc::Service*s,int&port){
    grpc::ServerBuilder b;b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);b.RegisterService(s);
    auto server=b.BuildAndStart();if(!server||!port)throw std::runtime_error("loopback bind failed");return server;
}
std::string Target(int p){return "127.0.0.1:"+std::to_string(p);}
class Source final:public UnreadProjectionSource {
public:
    enum Mode {Good,Short,Failed,Wrong,Throw,Slow};
    std::atomic<Mode> mode{Good};std::atomic<unsigned> singles{0},batches{0};
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t u,std::uint64_t p)override{++singles;return{true,{u,p,2,5},{}};}
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t)override{return{};}
    Results LoadDialogSnapshots(const Dialogs&d)override{
        ++batches;const auto m=mode.load();
        if(m==Throw)throw std::runtime_error("injected durable read failure");
        if(m==Slow)std::this_thread::sleep_for(std::chrono::milliseconds(80));
        auto r=UnreadProjectionSource::LoadDialogSnapshots(d);
        if(m==Short)r.pop_back();
        if(m==Failed)r.back().success=false;
        if(m==Wrong)r.back().value.total_unread=1;
        return r;
    }
};
class WireProbe final:public v1::UnreadSnapshotService::Service {
public:
    explicit WireProbe(Source*s):owner(s){}
    UnreadSnapshotRpcService owner;std::atomic<unsigned> singles{0},batches{0};
    grpc::Status ReadDialogUnreadSnapshot(grpc::ServerContext*c,const v1::ReadDialogUnreadSnapshotRequest*q,v1::ReadDialogUnreadSnapshotResponse*r)override{++singles;return owner.ReadDialogUnreadSnapshot(c,q,r);}
    grpc::Status ReadDialogUnreadSnapshots(grpc::ServerContext*c,const v1::ReadDialogUnreadSnapshotsRequest*q,v1::ReadDialogUnreadSnapshotsResponse*r)override{++batches;return owner.ReadDialogUnreadSnapshots(c,q,r);}
};
class Legacy final:public v1::UnreadSnapshotService::Service {
public:
    std::atomic<unsigned> singles{0};
    std::chrono::milliseconds delay{0};
    grpc::Status ReadDialogUnreadSnapshot(grpc::ServerContext*,const v1::ReadDialogUnreadSnapshotRequest*q,v1::ReadDialogUnreadSnapshotResponse*r)override{
        ++singles;if(delay.count())std::this_thread::sleep_for(delay);
        r->set_receiver_user_id(q->receiver_user_id());r->set_peer_user_id(q->peer_user_id());r->set_private_unread(2);r->set_total_unread(5);return grpc::Status::OK;
    }
};
class BadWire final:public v1::UnreadSnapshotService::Service {
public:
    enum Mode {Short,Actor,Counts,Reordered,Unavailable};Mode mode{Short};
    std::atomic<unsigned> fallback_calls{0};
    grpc::Status ReadDialogUnreadSnapshot(grpc::ServerContext*,const v1::ReadDialogUnreadSnapshotRequest*,v1::ReadDialogUnreadSnapshotResponse*)override{++fallback_calls;return grpc::Status::OK;}
    grpc::Status ReadDialogUnreadSnapshots(grpc::ServerContext*,const v1::ReadDialogUnreadSnapshotsRequest*q,v1::ReadDialogUnreadSnapshotsResponse*r)override{
        if(mode==Unavailable)return{grpc::StatusCode::UNAVAILABLE,"owner unavailable"};
        for(const auto&d:q->dialogs()){auto*row=r->add_snapshots();row->set_receiver_user_id(d.receiver_user_id());row->set_peer_user_id(d.peer_user_id());row->set_private_unread(2);row->set_total_unread(5);}
        if(mode==Short)r->mutable_snapshots()->RemoveLast();
        if(mode==Actor)r->mutable_snapshots(0)->set_receiver_user_id(99);
        if(mode==Counts)r->mutable_snapshots(0)->set_total_unread(1);
        if(mode==Reordered)r->mutable_snapshots()->SwapElements(0,1);
        return grpc::Status::OK;
    }
};
}
int main(){try{
    const Dialogs d{{1,2},{3,2},{1,2},{std::numeric_limits<std::uint64_t>::max(),2}};
    Source source;WireProbe wire(&source);int port=0;auto server=Start(&wire,port);
    UnreadSnapshotRpcClient batch(Target(port),std::chrono::milliseconds(300),true),baseline(Target(port));
    Check(Matches(batch.LoadDialogSnapshots(d),d),"one real wire batch preserves position, duplicates and 64-bit actor IDs");
    Check(wire.batches==1&&wire.singles==0&&source.batches==1,"batch uses one owner RPC, not parallel single-entry calls");
    Check(Matches(baseline.LoadDialogSnapshots(d),d)&&wire.singles==d.size(),"default OFF retains original per-dialog path");
    Dialogs max(256,{1,2});Check(Matches(batch.LoadDialogSnapshots(max),max),"maximum 256 rows traverses actual gRPC transport");
    const auto calls=wire.batches.load();
    Check(batch.LoadDialogSnapshots({}).empty()&&batch.LoadDialogSnapshots(Dialogs(257,{1,2})).empty(),"empty and oversized local batches do not make RPCs");
    Check(AllFailed(batch.LoadDialogSnapshots({{1,2},{0,3}}),2)&&wire.batches==calls,"all local IDs validated before any batch is sent");
    auto stub=v1::UnreadSnapshotService::NewStub(grpc::CreateChannel(Target(port),grpc::InsecureChannelCredentials()));
    for(int n:{0,257}){
        grpc::ClientContext c;v1::ReadDialogUnreadSnapshotsRequest q;v1::ReadDialogUnreadSnapshotsResponse r;
        for(int i=0;i<n;++i){q.add_dialogs()->set_receiver_user_id(1);q.mutable_dialogs(i)->set_peer_user_id(2);}
        const auto before=source.batches.load();Check(stub->ReadDialogUnreadSnapshots(&c,q,&r).error_code()==grpc::StatusCode::INVALID_ARGUMENT&&source.batches==before,"server rejects invalid cardinality before durable reads");
    }
    {
        grpc::ClientContext c;v1::ReadDialogUnreadSnapshotsRequest q;v1::ReadDialogUnreadSnapshotsResponse r;
        auto*row=q.add_dialogs();row->set_receiver_user_id(1);row->set_peer_user_id(2);
        row=q.add_dialogs();row->set_receiver_user_id(3);row->set_peer_user_id(3);
        const auto before=source.batches.load();
        Check(stub->ReadDialogUnreadSnapshots(&c,q,&r).error_code()==grpc::StatusCode::INVALID_ARGUMENT&&source.batches==before,"server validates all actor pairs before reading the first row");
    }
    {
        grpc::ClientContext c;v1::ReadDialogUnreadSnapshotsRequest q;v1::ReadDialogUnreadSnapshotsResponse r;
        auto*row=q.add_dialogs();row->set_receiver_user_id(1);row->set_peer_user_id(2);
        q.GetReflection()->MutableUnknownFields(&q)->AddLengthDelimited(99,std::string(8192,'x'));
        const auto before=source.batches.load();
        Check(stub->ReadDialogUnreadSnapshots(&c,q,&r).error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED&&source.batches==before,"unknown fields cannot bypass the request byte budget");
    }
    for(auto m:{Source::Short,Source::Failed,Source::Wrong,Source::Throw}){
        source.mode=m;Check(AllFailed(batch.LoadDialogSnapshots(d),d.size()),"incomplete, failed, corrupt or throwing source never yields partial success");
    }
    source.mode=Source::Good;
    std::atomic<bool> correct{true};std::vector<std::thread> threads;
    for(int i=0;i<4;++i)threads.emplace_back([&]{for(int j=0;j<20;++j)if(!Matches(batch.LoadDialogSnapshots(d),d))correct=false;});
    for(auto&t:threads)t.join();Check(correct,"shared batch client supports concurrent projector workers");
    source.mode=Source::Slow;UnreadSnapshotRpcClient deadline(Target(port),std::chrono::milliseconds(5),true);
    Check(AllFailed(deadline.LoadDialogSnapshots(d),d.size()),"batch deadline remains an explicit failure for every requested dialog");
    server->Shutdown();server->Wait();Check(AllFailed(batch.LoadDialogSnapshots(d),d.size()),"normal stopped owner cannot become successful empty snapshots");
    Legacy legacy;int oldport=0;auto oldserver=Start(&legacy,oldport);UnreadSnapshotRpcClient rolling(Target(oldport),std::chrono::milliseconds(300),true);
    Check(Matches(rolling.LoadDialogSnapshots(d),d)&&legacy.singles==d.size(),"UNIMPLEMENTED old owner safely falls back during rolling deployment");
    oldserver->Shutdown();oldserver->Wait();
    Legacy slow_legacy;slow_legacy.delay=std::chrono::milliseconds(80);int slowport=0;auto slowserver=Start(&slow_legacy,slowport);
    UnreadSnapshotRpcClient bounded_fallback(Target(slowport),std::chrono::milliseconds(120),true);
    const auto start=std::chrono::steady_clock::now();const auto timed=bounded_fallback.LoadDialogSnapshots(d);
    Check(AllFailed(timed,d.size())&&slow_legacy.singles<=2&&std::chrono::steady_clock::now()-start<std::chrono::milliseconds(300),"legacy fallback shares one batch deadline and discards partial reads before shutdown");
    slowserver->Shutdown();slowserver->Wait();
    BadWire malicious;int badport=0;auto badserver=Start(&malicious,badport);UnreadSnapshotRpcClient checked(Target(badport),std::chrono::milliseconds(300),true);
    for(auto m:{BadWire::Short,BadWire::Actor,BadWire::Counts,BadWire::Reordered,BadWire::Unavailable}){
        malicious.mode=m;Check(AllFailed(checked.LoadDialogSnapshots(d),d.size()),"client rejects missing, reordered, wrong-actor, inconsistent and unavailable wire outcomes");
    }
    Check(malicious.fallback_calls==0,"ordinary owner errors never trigger a misleading fallback success");
    badserver->Shutdown();badserver->Wait();
    std::cout<<"NATIVE_UNREAD_BATCH_REAL_WIRE_CONTRACT_PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
