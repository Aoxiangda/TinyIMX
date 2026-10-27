// Transport/scheduling mechanism experiment only. No repository or external DB.
#include "services/message/server/MessageRpcPollerPolicy.h"
#include "services/message/server/MessageServiceServer.h"
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
struct Fake final:tinyimx::message::v1::MessageService::Service {
    std::mutex mutex;std::unordered_set<long> tids;std::atomic<bool> measured{false};
    std::atomic<unsigned> calls{0},active{0},peak{0};
    grpc::Status GetGroupMessageDelivery(grpc::ServerContext*,const tinyimx::message::v1::GetGroupMessageDeliveryRequest* in,tinyimx::message::v1::GetGroupMessageDeliveryResponse*out)override {
        const bool selected=measured.load(std::memory_order_acquire);
        const auto current=active.fetch_add(1)+1;auto maximum=peak.load();
        while(current>maximum&&!peak.compare_exchange_weak(maximum,current)){}
        if(selected){std::lock_guard<std::mutex>lock(mutex);tids.insert(syscall(SYS_gettid));++calls;}
        std::this_thread::sleep_for(20ms);
        auto* w=out->mutable_work();w->mutable_message()->set_message_id(in->message_id());
        w->mutable_message()->set_content("mechanism-"+std::to_string(in->message_id()));
        w->mutable_delivery()->set_message_id(in->message_id());w->mutable_delivery()->set_recipient_user_id(in->recipient_user_id());
        --active;return grpc::Status::OK;
    }
};
struct Row{std::int64_t due=0,begin=0,end=0;bool ok=false;};
double Ms(std::int64_t us){return double(us)/1000;}
std::int64_t Micros(Clock::time_point p){return std::chrono::duration_cast<std::chrono::microseconds>(p.time_since_epoch()).count();}
double P99(std::vector<std::int64_t>v){std::sort(v.begin(),v.end());return Ms(v[(v.size()*99+99)/100-1]);}
double Cpu(const rusage&r){return r.ru_utime.tv_sec+r.ru_utime.tv_usec/1e6+r.ru_stime.tv_sec+r.ru_stime.tv_usec/1e6;}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    constexpr unsigned clients=32,count=4000,rate=1000;
    Fake fake;tinyimx::message::MessageServiceServer server(&fake);
    if(!server.Start("127.0.0.1:0")||!server.SetReady(true))return 3;
    auto channel=grpc::CreateChannel(server.BoundTarget(),grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+3s)){server.Shutdown();server.Wait();return 4;}
    auto stub=tinyimx::message::v1::MessageService::NewStub(channel);
    auto call=[&](unsigned id){tinyimx::message::v1::GetGroupMessageDeliveryRequest in;in.set_message_id(id+1);in.set_recipient_user_id(id+100001);tinyimx::message::v1::GetGroupMessageDeliveryResponse out;grpc::ClientContext ctx;ctx.set_deadline(std::chrono::system_clock::now()+2s);auto status=stub->GetGroupMessageDelivery(&ctx,in,&out);return status.ok()&&out.work().message().message_id()==id+1&&out.work().message().content()=="mechanism-"+std::to_string(id+1)&&out.work().delivery().message_id()==id+1&&out.work().delivery().recipient_user_id()==id+100001;};
    std::atomic<unsigned> warm_bad{0};std::vector<std::thread> threads;
    for(unsigned i=0;i<clients;++i)threads.emplace_back([&,i]{for(unsigned j=0;j<4;++j)if(!call(100000+i*4+j))++warm_bad;});
    for(auto&t:threads)t.join();threads.clear();if(warm_bad){server.Shutdown();server.Wait();return 5;}
    fake.peak=0;fake.measured.store(true,std::memory_order_release);
    std::vector<Row>rows(count);std::atomic<bool>go{false};std::atomic<unsigned>ready{0};Clock::time_point start;
    for(unsigned c=0;c<clients;++c)threads.emplace_back([&,c]{++ready;while(!go.load(std::memory_order_acquire))std::this_thread::yield();for(unsigned i=c;i<count;i+=clients){const auto due=start+std::chrono::microseconds(i*1000000ULL/rate);std::this_thread::sleep_until(due);auto&r=rows[i];r.due=Micros(due);r.begin=Micros(Clock::now());r.ok=call(i);r.end=Micros(Clock::now());}});
    while(ready.load()!=clients)std::this_thread::sleep_for(1ms);
    rusage before{},after{};getrusage(RUSAGE_SELF,&before);start=Clock::now()+100ms;go.store(true,std::memory_order_release);
    for(auto&t:threads)t.join();const auto finished=Clock::now();getrusage(RUSAGE_SELF,&after);
    fake.measured=false;server.Shutdown();server.Wait();
    std::ofstream raw(argv[1]);if(!raw)return 6;raw<<"index,due_us,begin_us,end_us,ok\n";
    std::vector<std::int64_t>rpc,scheduled,lateness;unsigned bad=0;
    for(unsigned i=0;i<count;++i){const auto&r=rows[i];if(!r.ok)++bad;rpc.push_back(r.end-r.begin);scheduled.push_back(r.end-r.due);lateness.push_back(r.begin-r.due);raw<<i<<','<<r.due<<','<<r.begin<<','<<r.end<<','<<r.ok<<'\n';}
    const auto policy=tinyimx::message::ConfiguredMessageRpcPollerPolicy();
    std::cout<<"{\"status\":\""<<(bad?"FAIL":"MECHANISM_RPC_ALL_IDENTITIES_PASS")<<"\",\"planned\":"<<count<<",\"completed\":"<<count-bad<<",\"failed\":"<<bad<<",\"rate\":"<<rate<<",\"client_threads\":"<<clients<<",\"fixed_handler_wait_ms\":20,\"min_pollers\":"<<policy.min_pollers<<",\"max_pollers\":"<<policy.max_pollers<<",\"measured_handler_calls\":"<<fake.calls<<",\"distinct_handler_os_tids\":"<<fake.tids.size()<<",\"peak_active_handlers\":"<<fake.peak<<",\"rpc_p99_ms\":"<<P99(rpc)<<",\"scheduled_return_p99_ms\":"<<P99(scheduled)<<",\"send_lateness_p99_ms\":"<<P99(lateness)<<",\"rpc_mean_ms\":"<<Ms(std::accumulate(rpc.begin(),rpc.end(),std::int64_t(0)))/count<<",\"process_cpu_seconds\":"<<Cpu(after)-Cpu(before)<<",\"wall_after_start_seconds\":"<<std::chrono::duration<double>(finished-start).count()<<",\"voluntary_context_switches\":"<<after.ru_nvcsw-before.ru_nvcsw<<",\"involuntary_context_switches\":"<<after.ru_nivcsw-before.ru_nivcsw<<"}\n";
    return bad?7:0;
}
