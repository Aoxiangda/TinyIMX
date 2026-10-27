#include "common/observability/SnapshotRpcTiming.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <mutex>
#include <set>

namespace {
using Timing=tinyimx::diagnostics::SnapshotRpcTiming;
std::int64_t wall=0,cpu=0;unsigned clock_calls=0,emissions=0,failures=0;
Timing::Snapshot record;
std::int64_t Wall()noexcept{++clock_calls;return wall;}
std::int64_t Cpu()noexcept{++clock_calls;return cpu;}
void Capture(const Timing::Snapshot&s)noexcept{record=s;++emissions;}
void Check(bool yes,const char*name){std::cout<<(yes?"PASS ":"FAIL ")<<name<<'\n';if(!yes)++failures;}
}
int main(){
    const auto max=std::numeric_limits<std::uint64_t>::max();
    const auto key=Timing::ParseKey(Timing::EncodeKey({max,max}));
    Check(key.origin==max&&key.sequence==max,"full uint64 diagnostic key survives bounded text round trip");
    for(const auto* text:{"","1","1:",":1","0:1","1:0","-1:2","+1:2","1:2:3","1:2x"," 1:2","1: 2","18446744073709551616:1","1:18446744073709551616","111111111111111111111111111111111111111111111:1"})
        Check(!Timing::ParseKey(text).Valid(),"malformed, overlong, zero and overflowing keys ignored");
    wall=100;cpu=1;
    {
        Timing trace(Timing::Server,Timing::Dialog,7,8,1,{11,13},true,Capture,Wall,Cpu);
        wall=120;cpu=3;
        Check(trace.Measure([]{wall=320;cpu=8;return 17;})==17,"operation return value is unchanged");
        wall=400;cpu=10;trace.Result(0);trace.ResponseValid();
    }
    Check(record.entry_us==100&&record.finished_us==400&&record.operation_started_us==120&&record.operation_finished_us==320,"handler span and source span retain separate exact boundaries");
    Check(record.thread_cpu_us==9&&record.operation_cpu_us==5&&record.response_valid==1&&record.status==0,"thread CPU and response validation are explicit");
    Check(record.key.origin==11&&record.key.sequence==13&&record.receiver==7&&record.peer==8,"snapshot diagnostic identity retained without fabricating a MID");
    bool caught=false;wall=500;cpu=-1;
    {
        Timing trace(Timing::Server,Timing::Batch,0,0,4,{11,14},true,Capture,Wall,Cpu);
        try{trace.Measure([]()->int{wall=900;throw std::runtime_error("owned source fault");});}
        catch(const std::runtime_error&){caught=true;trace.Result(14);}
    }
    Check(caught&&record.operation_threw&&record.status==14&&record.operation_finished_us==900,"original source exception and measured failure survive");
    Check(record.thread_cpu_us==-1&&record.operation_cpu_us==-1,"unsupported CPU measurements stay missing rather than zero");
    {
        Timing trace(Timing::Server,Timing::User,0,0,1,{},true,Capture,Wall,Cpu);
        trace.Result(3);
    }
    Check(record.operation_started_us==-1&&record.operation_finished_us==-1&&!record.key.Valid(),"pre-source rejection has no invented read or correlation identity");
    const auto clocks=clock_calls,logs=emissions;
    {
        Timing trace(Timing::Client,Timing::Dialog,7,8,1,{},false,Capture,Wall,Cpu);
        Check(!trace.Active()&&!trace.CurrentKey().Valid(),"disabled trace creates no diagnostic key");
        Check(trace.Measure([]{return 29;})==29,"disabled trace still executes original operation");
        trace.Result(0);trace.ResponseValid();
    }
    Check(clocks==clock_calls&&logs==emissions,"disabled trace reads no wall/CPU clocks and emits no records");
    std::mutex mutex;std::set<std::pair<std::uint64_t,std::uint64_t>> keys;
    std::vector<std::thread> threads;
    for(int i=0;i<4;++i)threads.emplace_back([&]{
        for(int j=0;j<1000;++j){const auto k=Timing::NewKey();std::lock_guard lock(mutex);keys.emplace(k.origin,k.sequence);}
    });
    for(auto&t:threads)t.join();
    Check(keys.size()==4000,"concurrent clients reserve distinct process-local call sequences");
    std::cout<<"SNAPSHOT_RPC_TIMING_UNIT_"<<(failures?"FAIL":"PASS")<<'\n';return failures?1:0;
}
