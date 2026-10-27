#include "services/message/local/NativePublicationPhaseTrace.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using Trace=tinyimx::message::local::NativePublicationPhaseTrace;
namespace {
std::int64_t wall=0,cpu=0;unsigned wall_calls=0,cpu_calls=0;
bool under_lock=false,logged_under_lock=false;
std::vector<Trace::Snapshot> snapshots;
std::int64_t Wall()noexcept{++wall_calls;return wall;}
std::int64_t Cpu()noexcept{++cpu_calls;return cpu;}
void Capture(const Trace::Snapshot&s)noexcept{logged_under_lock|=under_lock;snapshots.push_back(s);}
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);std::cout<<"PASS "<<text<<'\n';}
}
int main(){try {
    snapshots.reserve(16);
    {
        Trace off(Trace::Publish,"message.created.v1:123",1,false,Capture,Wall,Cpu);
        off.Start();off.Result(0,1);off.Stop();off.Emit();
    }
    Check(!wall_calls&&!cpu_calls&&snapshots.empty(),"disabled tracing reads no clocks and emits no snapshot");
    {
        Trace mutex(Trace::PollMutex,{},0,true,Capture,Wall,Cpu,false);
        Trace scan(Trace::Scan,{},0,true,Capture,Wall,Cpu,false);
        mutex.Start();wall+=40;under_lock=true;mutex.Stop();mutex.Result(0,1);
        scan.Start();wall+=100;cpu+=6;scan.Stop();scan.Result(0,32);
        wall+=500;under_lock=false;mutex.Emit();scan.Emit();
    }
    Check(!logged_under_lock&&snapshots.size()==2,"record under mutex, emit only after unlock, and never emit twice");
    Check(snapshots[0].finished_us-snapshots[0].started_us==40&&snapshots[0].cpu_us==0,
          "mutex wait excludes later scan and logging delay");
    Check(snapshots[1].finished_us-snapshots[1].started_us==100&&snapshots[1].cpu_us==6&&snapshots[1].units==32,
          "scan duration and thread CPU close at operation end, not deferred emission");
    {
        Trace unused(Trace::Scan,{},0,true,Capture,Wall,Cpu,false);
    }
    Check(snapshots.size()==2,"unstarted optional scan has no invented zero observation");
    try {
        Trace failure(Trace::Publish,"message.created.v1:9001",1,true,Capture,Wall,Cpu);
        wall+=25;cpu+=2;throw std::runtime_error("publisher sentinel");
    }catch(const std::runtime_error&e) {
        Check(std::string(e.what())=="publisher sentinel","tracing preserves publisher exception");
    }
    Check(snapshots.back().mid==9001&&snapshots.back().status==-1&&snapshots.back().units==1,
          "unwinding retains event MID and unknown failure instead of inventing success");
    for(const auto* event:{"message.created.v1:","message.created.v1:9x",
                          "message.created.v1:18446744073709551616","dialog.read_advanced.v1:1:2"}) {
        Trace invalid(Trace::ProgressWrite,event,0,true,Capture,Wall,Cpu);invalid.Result(0,0);
    }
    Check(snapshots.size()==7,"all malformed/nonmessage event diagnostic scopes close");
    for(std::size_t i=3;i<snapshots.size();++i)Check(snapshots[i].mid==0,"malformed identity never invents a message correlation");
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
