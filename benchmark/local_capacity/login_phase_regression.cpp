#include "gateway/LoginRequestPhaseTrace.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
using Trace=tinyimx::LoginRequestPhaseTrace;
namespace {
std::int64_t wall=100,cpu=10;
unsigned reads=0,cpu_reads=0,emits=0,fails=0;
Trace::Snapshot got;
std::int64_t Wall() noexcept {++reads;return wall;}
std::int64_t Cpu() noexcept {++cpu_reads;return cpu;}
std::int64_t NoCpu() noexcept {return -1;}
void Sink(const Trace::Snapshot& x) noexcept {got=x;++emits;}
void Check(bool b,const char* msg){std::cout<<(b?"[PASS] ":"[FAIL] ")<<msg<<'\n';fails+=!b;}
}
int main(){
 static_assert(std::is_trivially_copyable_v<Trace::Snapshot>);
 tinyimx::BusinessRequestContext req;req.request_seq=7;
 req.received_at=tinyimx::BusinessTimePoint(std::chrono::microseconds(50));
 req.deadline=tinyimx::BusinessTimePoint(std::chrono::microseconds(999));
 Check(!Trace::Enabled(),"Default OFF");
 {Trace t(req,Sink,false,Wall,Cpu);auto x=t.Measure(Trace::Phase::Authenticate,[]{return std::make_unique<int>(9);});Check(*x==9,"Disabled preserves move-only result");t.SetUserId(32);t.ResponseStart(true);}
 Check(reads==0&&cpu_reads==0&&emits==0,"Disabled reads no diagnostic clock and emits nothing");
 {Trace t(req,Sink,true,Wall,Cpu);t.SetUserId(32);
  auto x=t.Measure(Trace::Phase::Authenticate,[]{wall+=30;cpu+=15;return std::make_unique<int>(8);});Check(*x==8,"Enabled preserves owned result");
  t.Measure(Trace::Phase::Presence,[]{wall+=10;cpu+=2;});
  t.Measure(Trace::Phase::Unread,[]{wall+=5;cpu+=1;return 12;});
  t.ResponseStart(true);wall+=8;cpu+=1;}
 Check(emits==1&&got.uid==32&&got.seq==7&&got.success&&!got.failed,"Identity and success recorded");
 Check(got.received_us==50&&got.started_us==100&&got.response_us==145&&got.finished_us==153,"Absolute request timeline preserved");
 Check(got.cpu_us==19&&got.phase_cpu_us==std::array<std::int64_t,3>{15,2,1},"CPU separated from wall");
 Check(got.phase_start_us==std::array<std::int64_t,3>{100,130,140}&&got.phase_end_us==std::array<std::int64_t,3>{130,140,145},"Every phase interval exact");
 Check(req.deadline==tinyimx::BusinessTimePoint(std::chrono::microseconds(999)),"Original deadline unchanged");
 {Trace t(req,Sink,true,Wall,Cpu);t.SetUserId(33);t.ResponseStart(true);}
 Check(emits==1,"Unsampled success omitted");
 {Trace t(req,Sink,true,Wall,Cpu);t.ResponseStart(true);}
 Check(emits==1,"Zero UID not sampled as success");
 {Trace t(req,Sink,true,Wall,Cpu);t.ResponseStart(false);}
 Check(emits==2&&got.failed&&!got.success,"Failed response eligible without private identity");
 {Trace t(req,Sink,true,Wall,NoCpu);t.SetUserId(48);t.Measure(Trace::Phase::Authenticate,[]{wall+=3;return true;});t.ResponseStart(true);}
 Check(emits==3&&got.cpu_us==-1&&got.phase_cpu_us[0]==-1&&got.phase_start_us[1]==-1,"Unavailable CPU and unexecuted phases explicit");
 try {Trace t(req,Sink,true,Wall,Cpu);t.Measure(Trace::Phase::Presence,[]{wall+=2;cpu+=1;throw std::runtime_error("original");});}
 catch(const std::runtime_error& e){Check(std::string(e.what())=="original","Original exception retained");}
 Check(emits==4&&got.failed&&got.phase_end_us[1]-got.phase_start_us[1]==2,"Throwing phase retained");
 {Trace t(req,Sink,true,Wall,Cpu);t.SetUserId(64);t.Measure(Trace::Phase::Unread,[]{cpu=-1;return 1;});t.ResponseStart(true);}
 Check(got.cpu_us==-1&&got.phase_cpu_us[2]==-1,"Invalid CPU delta never reported as zero");
 return fails?1:0;
}
