// Isolated synchronous snapshot RPC mechanism probe, never IM capacity.
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "services/message/server/MessageRpcPollerPolicy.h"
#include "common/db/StorageWaitTiming.h"
#include <grpcpp/grpcpp.h>
#include <grpc/grpc.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace tinyimx::projection::unread;
namespace {
class Source final:public UnreadProjectionSource {
public:
    explicit Source(int delay):delay_ms(delay),tids(100000){}
    int delay_ms;std::atomic<unsigned> used{0};std::vector<std::uint64_t> tids;
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t u,std::uint64_t p)override{
        const auto slot=used.fetch_add(1,std::memory_order_relaxed);
        if(slot>=tids.size())throw std::runtime_error("probe record budget exceeded");
        tids[slot]=tinyimx::diagnostics::CurrentThreadId();
        if(delay_ms)std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        return{true,{u,p,2,5},{}};
    }
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t)override{return{};}
};
void Server(const char* retention,int delay){
    Source source(delay);UnreadSnapshotRpcService service(&source);grpc::ServerBuilder builder;
    const auto policy=tinyimx::message::ParseMessageRpcPollerRetention(retention);
    if(policy.enabled){
        builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS,policy.queues);
        builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS,policy.min_pollers);
        builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS,policy.max_pollers);
    }
    int port=0;builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);builder.RegisterService(&service);
    auto server=builder.BuildAndStart();if(!server||!port)throw std::runtime_error("probe loopback bind failed");
    std::cout<<"{\"port\":"<<port<<",\"core\":\""<<grpc_version_string()<<"\",\"retention_enabled\":"<<(policy.enabled?"true":"false")<<",\"min_pollers\":"<<policy.min_pollers<<",\"max_idle_pollers\":"<<policy.max_pollers<<",\"delay_ms\":"<<delay<<"}"<<std::endl;
    std::string command;
    while(std::getline(std::cin,command)){
        if(command=="begin"){source.used=0;std::cout<<"BEGIN"<<std::endl;}
        else if(command=="stop")break;
        else throw std::runtime_error("invalid probe control");
    }
    server->Shutdown();server->Wait();
    const auto count=source.used.load();std::set<std::uint64_t> unique;
    for(unsigned i=0;i<count&&i<source.tids.size();++i)unique.insert(source.tids[i]);
    std::cout<<"{\"completed_reads\":"<<count<<",\"distinct_handler_tids\":"<<unique.size()<<",\"tids\":[";
    bool first=true;for(const auto tid:unique){if(!first)std::cout<<',';first=false;std::cout<<tid;}
    std::cout<<"]}"<<std::endl;
}
void Client(const std::string&target,unsigned count,unsigned rate){
    if(target.rfind("127.0.0.1:",0)!=0||!count||count>20000||!rate||rate>5000)throw std::runtime_error("owned bounded loopback probe required");
    UnreadSnapshotRpcClient client(target,std::chrono::milliseconds(500));
    struct Row{std::int64_t scheduled,started,finished;bool success;};
    std::vector<Row> rows(count);std::atomic<unsigned> next{0};
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now()+std::chrono::milliseconds(50);
    const auto micros=[](Clock::time_point t){return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();};
    std::vector<std::thread> workers;
    for(int w=0;w<32;++w)workers.emplace_back([&]{
        for(;;){
            const auto i=next.fetch_add(1);if(i>=count)break;
            const auto scheduled=start+std::chrono::nanoseconds(static_cast<std::int64_t>(i)*1000000000/rate);
            std::this_thread::sleep_until(scheduled);
            const auto sent=Clock::now();const auto out=client.LoadDialogSnapshot(1,2);const auto finished=Clock::now();
            rows[i]={micros(scheduled),micros(sent),micros(finished),out.success&&out.value.private_unread==2&&out.value.total_unread==5};
        }
    });
    for(auto&w:workers)w.join();
    unsigned success=0;std::cout<<"index,scheduled_us,started_us,finished_us,success\n";
    for(unsigned i=0;i<count;++i){const auto&r=rows[i];success+=r.success;std::cout<<i<<','<<r.scheduled<<','<<r.started<<','<<r.finished<<','<<r.success<<'\n';}
    if(success!=count)throw std::runtime_error("snapshot probe incomplete replies");
}
}
int main(int argc,char**argv){try{
    if(argc==4&&std::string(argv[1])=="server")Server(argv[2],std::stoi(argv[3]));
    else if(argc==5&&std::string(argv[1])=="client")Client(argv[2],std::stoul(argv[3]),std::stoul(argv[4]));
    else throw std::runtime_error("expected server retention delay or client target count rate");
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
