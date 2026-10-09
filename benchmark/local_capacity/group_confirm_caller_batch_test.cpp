#include "common/runtime/BoundedCallerBatch.h"
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
namespace {
int checks = 0;
void Check(bool yes, const char* name) {
    if (!yes) throw std::runtime_error(name);
    ++checks; std::cout << "[PASS] " << name << '\n';
}
using Batch = tinyimx::BoundedCallerBatch<int,int>;
void Concurrent() {
    Batch batch({8,64,2ms});
    std::barrier start(32);
    std::mutex mutex;
    std::vector<int> seen;
    std::atomic<int> calls{0};
    std::vector<int> results(32);
    std::vector<std::thread> threads;
    for (int i=0;i<32;++i) threads.emplace_back([&,i] {
        start.arrive_and_wait();
        results[i]=batch.Run(i,[&](const std::vector<int>& values) {
            ++calls;
            std::lock_guard lock(mutex);
            seen.insert(seen.end(),values.begin(),values.end());
            if (values.empty()||values.size()>8) throw std::runtime_error("unbounded batch");
            std::vector<int> out;
            for (int v:values) out.push_back(v+100);
            return out;
        },-1);
    });
    for (auto& thread:threads) thread.join();
    std::sort(seen.begin(),seen.end());
    Check(seen.size()==32,"each caller executes once");
    Check(std::adjacent_find(seen.begin(),seen.end())==seen.end(),"no duplicate execute");
    for (int i=0;i<32;++i) Check(results[i]==i+100,"per caller result identity preserved");
    Check(calls<32,"concurrent callbacks combined with bounded batch");
}
void Error() {
    Batch batch({4,16,2ms}); std::atomic<int> calls{0};
    auto result=batch.Run(1,[&](const auto&){++calls;throw std::runtime_error("after side effect");return std::vector<int>{};},-7);
    Check(result==-7&&calls==1,"uncertain callback error never reexecutes");
    result=batch.Run(2,[&](const auto&){++calls;return std::vector<int>{};},-8);
    Check(result==-8&&calls==2,"invalid result cardinality fails without replay");
    result=batch.Run(3,[&](const auto&v){++calls;return std::vector<int>{v[0]};},-9);
    Check(result==3&&calls==3,"batch remains usable after errors");
    std::barrier start(8); std::vector<int> errors(8); std::vector<std::thread> threads;
    for(int i=0;i<8;++i)threads.emplace_back([&,i]{start.arrive_and_wait();errors[i]=batch.Run(i,
        [&](const auto&){++calls;throw std::runtime_error("failure");return std::vector<int>{};},-10);});
    for(auto&thread:threads)thread.join();
    Check(std::all_of(errors.begin(),errors.end(),[](int v){return v==-10;}),"all failed batch waiters awaken");
}
void CapacityAndFifo() {
    Batch batch({1,2,0us}); std::mutex mutex; std::condition_variable cv;
    bool entered=false,release=false;std::vector<int> order;
    auto execute=[&](const std::vector<int>&v){
        std::unique_lock lock(mutex);order.push_back(v[0]);
        if(v[0]==0){entered=true;cv.notify_all();cv.wait(lock,[&]{return release;});}
        return v;
    };
    auto leader=std::async(std::launch::async,[&]{return batch.Run(0,execute,-1);});
    {std::unique_lock lock(mutex);Check(cv.wait_for(lock,2s,[&]{return entered;}),"leader callback entered");}
    auto waitQueued=[&](std::size_t count){
        const auto until=std::chrono::steady_clock::now()+2s;
        while(batch.QueuedCount()!=count&&std::chrono::steady_clock::now()<until)
            std::this_thread::yield();
        return batch.QueuedCount()==count;
    };
    auto a=std::async(std::launch::async,[&]{return batch.Run(1,execute,-1);});
    Check(waitQueued(1),"first follower admitted before second");
    auto b=std::async(std::launch::async,[&]{return batch.Run(2,execute,-1);});
    Check(waitQueued(2),"second follower admitted at bounded queue capacity");
    Check(batch.Run(3,execute,-11)==-11,"overload rejected before callback");
    {std::lock_guard lock(mutex);release=true;cv.notify_all();}
    Check(leader.get()==0&&a.get()==1&&b.get()==2,"leader and FIFO followers preserve individual results");
    Check(order==std::vector<int>({0,1,2}),"caller-led callbacks retain admitted FIFO");
    Check(batch.Run(4,execute,-1)==4,"idle coordinator accepts subsequent call");
}
void Lifetime() {
    std::weak_ptr<Batch> weak;
    {auto owner=std::make_shared<Batch>();weak=owner;
     Check(owner->Run(5,[](const auto&v){return v;},-1)==5,"last active owner receives completed result");}
    Check(weak.expired(),"no background ownership remains after calls");
    bool rejected=false;try{Batch bad({0,1,0us});}catch(const std::invalid_argument&){rejected=true;}
    Check(rejected,"zero batch limit rejected");
}
}
int main(){
 try{Concurrent();Error();CapacityAndFifo();Lifetime();
  std::cout<<"{\"status\":\"BOUNDED_CALLER_BATCH_NATIVE_PASS\",\"checks\":"<<checks<<",\"failures\":0}\n";return 0;
 }catch(const std::exception&e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
