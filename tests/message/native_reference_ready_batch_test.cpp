#include "services/message/local/NativeReferenceReadyBatch.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using tinyimx::message::local::NativeReferenceReadyBatch;
using Actors=NativeReferenceReadyBatch::Actors;
#if defined(TINYIMX_TEST_LEGACY_SINGLE_REFERENCE_BATCH) || defined(TINYIMX_TEST_LEGACY_BOOL_PIN)
using PinOutcome=bool;
PinOutcome Unavailable(){return false;}
#else
using PinOutcome=NativeReferenceReadyBatch::PinResult;
PinOutcome Unavailable(){return PinOutcome::Unavailable();}
#endif
using namespace std::chrono_literals;
namespace {
std::atomic<unsigned> checks{0};
void Check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;}
template<class F>void Until(F predicate,const char* text){
 const auto end=std::chrono::steady_clock::now()+10s;
 while(!predicate()){if(std::chrono::steady_clock::now()>=end){std::cerr<<"FAIL "<<text<<std::endl;std::exit(1);}std::this_thread::yield();}
}
struct Gate{
 std::mutex mutex;std::condition_variable changed;bool entered=false,released=false;
 void Hold(){std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait(lock,[&]{return released;});}
 bool Entered(){std::lock_guard lock(mutex);return entered;}
 void Release(){std::lock_guard lock(mutex);released=true;changed.notify_all();}
};
void SingleAndException(){
 auto owner=std::this_thread::get_id();bool same=false;
 NativeReferenceReadyBatch single([&](const Actors& ids){same=std::this_thread::get_id()==owner;return ids==Actors{1,2};});
 Check(single.Run({1,2})&&same,"one ready call executes on its caller without a new thread");
 Check(!single.Run({0,1})&&!single.Run({1,2,3}),"bound actor cardinality and reject zero before SQL");
 Check(single.Run({})&&single.Snapshot().sql_calls==1,"empty set performs no SQL");
 unsigned calls=0;
 NativeReferenceReadyBatch exceptional([&](const Actors&){if(++calls==1)throw std::runtime_error("uncertain SQL");return true;});
 Check(!exceptional.Run({1,2}),"throw reports failure without retrying an uncertain SQL operation");
 Check(exceptional.Run({3,4})&&calls==2,"next caller progresses after an exception");
}
void Concurrent(bool missing,bool uncertain){
 Gate gate;std::set<std::uint64_t> pinned;unsigned sql=0;bool ambiguous=false;
 NativeReferenceReadyBatch batch([&](const Actors& ids) -> PinOutcome {
  if(++sql==1)gate.Hold();
  if(missing&&ids.contains(9999))return false;
  pinned.insert(ids.begin(),ids.end());
  if(uncertain&&ids.size()>2&&!ambiguous){ambiguous=true;return Unavailable();}
  return true;
 });
 auto leader=std::async(std::launch::async,[&]{return batch.Run({1,2});});
 Until([&]{return gate.Entered();},"first real operation enters");
 std::vector<std::future<bool>> callers;
 for(unsigned i=0;i<32;++i){
  Actors ids{10+2*i,11+2*i};if(missing&&i==31)ids={1000,9999};
  callers.push_back(std::async(std::launch::async,[&,ids]{return batch.Run(ids);}));
 }
 Until([&]{return batch.Snapshot().queued==32;},"all32 accepted callers actually queued");
 gate.Release();Check(leader.get(),"leader completes once");
 for(unsigned i=0;i<32;++i)Check(callers[i].get()==(!uncertain&&!(missing&&i==31)),"each caller receives its own reference result");
 const auto s=batch.Snapshot();
 Check(s.queued==0&&s.largest_batch==32&&s.calls==33,"only already queued work drains as a bounded batch");
 Check(s.succeeded==(uncertain?1:(missing?32:33)),"missing parents isolate failure and SQL unavailability fails the batch closed");
 Check(s.sql_calls==(missing?34:2),"unavailable SQL is never expanded into per-caller retry traffic");
 Check(s.fallback_calls==(missing?32:0),"only confirmed missing parents use per-caller isolation");
 Check(pinned.size()==(missing?64:66),"idempotent retry has one logical row per valid actor");
}
void Overload(){
 Gate gate;unsigned sql=0;
 NativeReferenceReadyBatch batch([&](const Actors& ids){
  Check(ids.size()<=128,"SQL union stays within128 actor rows");
  if(++sql==1)gate.Hold();return true;
 });
 auto leader=std::async(std::launch::async,[&]{return batch.Run({1,2});});
 Until([&]{return gate.Entered();},"overload leader enters");
 std::vector<std::future<bool>> callers;
 for(unsigned i=0;i<63;++i)callers.push_back(std::async(std::launch::async,[&,i]{return batch.Run({10+2*i,11+2*i});}));
 Until([&]{return batch.Snapshot().queued==63;},"pending bound includes the running call and63 queued calls");
 Check(!batch.Run({9997,9998})&&sql==1,"overflow returns failure and performs no SQL");
 gate.Release();Check(leader.get(),"overload leader completes");
 for(auto& f:callers)Check(f.get(),"every accepted call completes under overload");
 const auto s=batch.Snapshot();Check(s.queued==0&&s.calls==65&&s.succeeded==64&&s.largest_batch==63&&s.sql_calls==2,"queue drains with every accepted result preserved");
}
void IndependentBlockedSql(){
 Gate gate;
 auto pin=[&](const Actors& ids){if(ids.contains(1))gate.Hold();return true;};
#ifdef TINYIMX_TEST_LEGACY_SINGLE_REFERENCE_BATCH
 NativeReferenceReadyBatch batch(pin);
#else
 NativeReferenceReadyBatch batch(pin,2);
#endif
 auto blocked=std::async(std::launch::async,[&]{return batch.Run({1,2});});
 Until([&]{return gate.Entered();},"one SQL batch is genuinely held");
 auto other=std::async(std::launch::async,[&]{return batch.Run({3,4});});
 const bool progressed=other.wait_for(500ms)==std::future_status::ready;
 gate.Release();const bool first=blocked.get(),second=other.get();
 Check(progressed&&first&&second,"unrelated SQL reference call progresses while one batch is blocked");
}
#if !defined(TINYIMX_TEST_LEGACY_SINGLE_REFERENCE_BATCH) && !defined(TINYIMX_TEST_LEGACY_BOOL_PIN)
void ProductionEightLanes(){
 Gate io;std::atomic<unsigned> registrations{0};
#ifdef TINYIMX_TEST_POOL_MINUS_ONE_REFERENCE_BUDGET
 constexpr std::size_t parallel=32-1; // Exact pre-fix production rule.
#else
 constexpr auto parallel=NativeReferenceReadyBatch::ParallelSqlBatches(32);
#endif
 NativeReferenceReadyBatch batch([&](const Actors& ids){
  ++registrations;io.Hold();return !ids.empty()&&ids.size()<=16;
 },parallel);
 std::vector<std::future<bool>> callers;
 for(unsigned i=0;i<8;++i)callers.push_back(std::async(std::launch::async,[&,i]{return batch.Run({1+2*i,2+2*i});}));
 Until([&]{return batch.Snapshot().pending==8;},"all eight production-like callers admitted during SQL IO wait");
 const auto held=batch.Snapshot();io.Release();
 bool success=true;for(auto& result:callers)success=result.get()&&success;
 // Release and join every caller before reporting a failed legacy control.
 const auto s=batch.Snapshot();
 Check(success&&s.pending==0&&s.active_batches==0&&s.queued==0,"eight admitted callers complete and drain after SQL IO");
 Check(held.active_batches==4&&held.queued==4,"32 SQL slots retain four independent leaders and four ready callers");
 Check(s.calls==8&&s.succeeded==8&&registrations==5&&s.sql_calls==5&&s.largest_batch==4,
       "eight production lanes actually combine queued registrations into five batches");
}
#endif
}
int main(int argc,char** argv){try{
 if(argc==2&&std::string(argv[1])=="--independent-only"){IndependentBlockedSql();return 0;}
 if(argc==2&&std::string(argv[1])=="--unavailable-only"){Concurrent(false,true);return 0;}
#if !defined(TINYIMX_TEST_LEGACY_SINGLE_REFERENCE_BATCH) && !defined(TINYIMX_TEST_LEGACY_BOOL_PIN)
 if(argc==2&&std::string(argv[1])=="--production-budget-only"){ProductionEightLanes();return 0;}
 ProductionEightLanes();
#endif
 SingleAndException();Concurrent(false,false);Concurrent(true,false);Concurrent(false,true);Overload();IndependentBlockedSql();
 std::cout<<"PASS "<<checks.load()<<" native reference ready batch contracts"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
