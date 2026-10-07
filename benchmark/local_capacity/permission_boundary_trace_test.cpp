#include "common/db/PermissionBoundaryTrace.h"
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <stdexcept>
#include <atomic>
using namespace tinyimx::diagnostics;
using Trace=PermissionBoundaryTrace;
std::atomic<std::int64_t> nowvalue{100},cpuvalue{10};
std::int64_t Now() noexcept {return nowvalue.fetch_add(10);}
std::int64_t Cpu() noexcept {return cpuvalue.fetch_add(3);}
std::mutex mutex;std::vector<Trace::Snapshot> rows;
void Capture(const Trace::Snapshot& s) noexcept {std::lock_guard<std::mutex> guard(mutex);rows.push_back(s);}
int checks=0,failures=0;
void Check(bool good,const char* text) {++checks;if(!good)++failures;std::cout<<(good?"[PASS] ":"[FAIL] ")<<text<<'\n';}
int main(int argc,char** argv){
 if(argc!=2) return 2;
 const auto expected=ParsePermissionEnabled(argv[1]);
 Check(PermissionEnabled()==expected,"ENV exact configured selector");
 {auto count=rows.size();Trace selected(1,1,519862,"selected","gateway",false,Capture,Now,Cpu);selected.Mark(1);}
 Check(rows.size()==(expected?1U:0U),"default constructor only enables exactoneflag");rows.clear();
 Check(!ParsePermissionEnabled(nullptr),"absent defaultsOFF");
 Check(!ParsePermissionEnabled(""),"empty defaultsOFF");
 Check(!ParsePermissionEnabled("0"),"zero defaultsOFF");
 Check(!ParsePermissionEnabled("01"),"leadingzero rejected");
 Check(!ParsePermissionEnabled("-1"),"negative rejected");
 Check(!ParsePermissionEnabled("1x"),"trailingtext rejected");
 Check(ParsePermissionEnabled("1"),"exact one enabled");
 Check(PermissionTraceHash("hello")==0xa430d84680aabd0bULL,"FNV knownvector exact");
 Check(PermissionTraceHash("request1")!=PermissionTraceHash("request2"),"distinct request correlation");
 auto n=nowvalue.load(),c=cpuvalue.load();
 {Trace off(1,1,2,"secret-notlogged","caller",false,Capture,Now,Cpu,false);off.Mark(1);off.Result(0);Trace::Scope scope(off);Trace child(3,1,2,Capture,Now,Cpu);child.Mark(2);}
 Check(rows.empty()&&nowvalue==n&&cpuvalue==c,"disabled haszero clocks/sink evenrepositoryscope");
 {Trace trace(1,10,20,"rid","caller",true,Capture,Now,Cpu,false);trace.Mark(1);trace.Mark(0);trace.Mark(6);trace.Mark(3);trace.Result(15);}
 Check(rows.size()==1 && rows.back().identity.from==10&&rows.back().identity.to==20,"identity retained");
 Check(rows.back().identity.rid_hash==PermissionTraceHash("rid")&&rows.back().identity.caller_hash==PermissionTraceHash("caller"),"only numeric correlation copied");
 Check(rows.back().marks[0]==rows.back().started_us+10&&rows.back().marks[2]==rows.back().started_us+20&&rows.back().marks[4]==-1,"marks exact andinvalid indexes ignored");
 Check(rows.back().total_us==30&&rows.back().cpu_us==3&&rows.back().status==15,"wall CPU andfailure retained");
 rows.clear();
 {Trace parent(2,100,200,"outer","gateway",true,Capture,Now,Cpu,false);Trace::Scope scope(parent);
  {Trace wrong(3,100,201,Capture,Now,Cpu);wrong.Mark(1);}Check(rows.empty(),"different recipient cannotinherit");
  {Trace wrong(3,101,200,Capture,Now,Cpu);wrong.Mark(1);}Check(rows.empty(),"different sender cannotinherit");
  {Trace child(3,100,200,Capture,Now,Cpu);child.Mark(1);child.Result(0);}Check(rows.size()==1&&rows.back().identity.rid_hash==PermissionTraceHash("outer"),"repository inherits exact parent");
  try {Trace inner(2,101,201,"inner","gateway",true,Capture,Now,Cpu,false);Trace::Scope in(inner);throw std::runtime_error("owned synthetic unwind");}catch(const std::runtime_error&){}
  {Trace child(3,100,200,Capture,Now,Cpu);}Check(rows.back().identity.rid_hash==PermissionTraceHash("outer"),"nested scope restored afterexception");
  {Trace disabled(2,100,200,"off","gateway",false,Capture,Now,Cpu,false);Trace::Scope in(disabled);auto count=rows.size();{Trace child(3,100,200,Capture,Now,Cpu);}Check(rows.size()==count,"disabled nested scope suppressesinherit");}
 }
 auto count=rows.size();{Trace orphan(3,100,200,Capture,Now,Cpu);}Check(rows.size()==count,"handler exit leavesno TLS context");
 rows.clear();std::atomic<int> ready{0};
 auto work=[&](std::uint64_t id){Trace parent(2,id,id+10,id==1?"one":"two","gateway",true,Capture,Now,Cpu,false);Trace::Scope scope(parent);++ready;while(ready.load()!=2)std::this_thread::yield();Trace child(3,id,id+10,Capture,Now,Cpu);child.Result(0);};
 std::thread a(work,1),b(work,2);a.join();b.join();int children=0;bool isolated=true;for(const auto& v:rows)if(v.side==3){++children;isolated=isolated&&v.identity.to==v.identity.from+10&&v.identity.rid_hash==PermissionTraceHash(v.identity.from==1?"one":"two");}
 Check(children==2&&isolated,"concurrent TLS identities isolated");
 PermissionTraceRateGate gate;int accepted=0;for(int i=0;i<16;++i)accepted+=gate.Allow(2000000);Check(accepted==8,"strict max8 records persecond");
 Check(!gate.Allow(1999999)&&!gate.Allow(-1),"backwards clock cannotresetlimit");
 Check(gate.Allow(3000000),"nextsecond allows newrecord");
 PermissionTraceRateGate concurrent;std::atomic<int> total{0};std::vector<std::thread> threads;for(int j=0;j<8;++j)threads.emplace_back([&]{for(int i=0;i<8;++i)if(concurrent.Allow(4000000))++total;});for(auto& t:threads)t.join();Check(total==8,"concurrent gate strictbound");
 std::cout<<"{\"status\":\"PERMISSION_BOUNDARY_NATIVE_"<<(failures?"FAIL":"PASS")<<"\",\"checks\":"<<checks<<",\"failures\":"<<failures<<"}"<<'\n';return failures?1:0;
}
