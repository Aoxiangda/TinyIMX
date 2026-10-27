#include "common/runtime/BoundedReadyBatch.h"
#include "services/message/service/MessageReadyBatchPolicy.h"
#include <algorithm>
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
void Check(bool good,const char* why){if(!good)throw std::runtime_error(why);std::cout<<"PASS "<<why<<std::endl;}
template<class Fn>bool Wait(Fn fn){const auto end=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<end){if(fn())return true;std::this_thread::sleep_for(1ms);}return fn();}
void Exercise(std::size_t limit){
    using Q=tinyimx::BoundedReadyBatch<unsigned,unsigned>;
    std::mutex mutex;std::condition_variable changed;
    bool first_started=false,release_first=false,slow_started=false,release_slow=false;
    std::vector<unsigned> done;std::vector<std::size_t> groups;
    Q queue({1,4,limit},[&](const std::vector<unsigned>& inputs){
        std::unique_lock lock(mutex);groups.push_back(inputs.size());
        std::vector<unsigned> results;
        for(auto n:inputs){
            if(n==0){first_started=true;changed.notify_all();changed.wait(lock,[&]{return release_first;});}
            if(n==2){slow_started=true;changed.notify_all();changed.wait(lock,[&]{return release_slow;});}
            results.push_back(n);
        }
        return results;
    },99);
    struct Release{std::mutex&m;std::condition_variable&c;bool&a;bool&b;
        ~Release(){std::lock_guard lock(m);a=b=true;c.notify_all();}}release{mutex,changed,release_first,release_slow};
    auto submit=[&](unsigned n){return queue.Submit(500001,n,[&](unsigned result){
        std::lock_guard lock(mutex);done.push_back(result);changed.notify_all();});};
    Check(submit(0)==Q::Admission::Accepted&&Wait([&]{std::lock_guard lock(mutex);return first_started;}),
        "one exact accepted preparation is blocked before ready followers");
    Check(submit(1)==Q::Admission::Accepted&&submit(2)==Q::Admission::Accepted&&submit(3)==Q::Admission::Accepted,
        "three ready followers preserve their sender FIFO");
    Check(queue.Snapshot().pending==4&&submit(4)==Q::Admission::Overloaded,
        "admission counts in-flight and queued work with unchanged bound");
    {std::lock_guard lock(mutex);release_first=true;changed.notify_all();}
    Check(Wait([&]{std::lock_guard lock(mutex);return slow_started;}),"the second preparation reaches its controlled slow operation");
    {std::lock_guard lock(mutex);
        Check((std::find(done.begin(),done.end(),1)!=done.end())==(limit==1),
            "limit1 completes the earlier independent request before later preparation; legacy batch holds it");}
    auto draining=std::async(std::launch::async,[&]{queue.Drain();});
    const bool waited=draining.wait_for(15ms)==std::future_status::timeout;
    {std::lock_guard lock(mutex);release_slow=true;changed.notify_all();}
    draining.get();
    Check(waited&&done==std::vector<unsigned>({0,1,2,3}),"drain waits for accepted preparation and completes exact FIFO once");
    const auto stats=queue.Snapshot();
    Check(stats.accepted==4&&stats.completed==4&&stats.pending==0&&stats.peak_pending==4&&stats.overloaded==1&&
        stats.execute_failures==0&&stats.completion_failures==0,"all accepted outcomes and capacity reconcile exactly");
    Check(queue.Submit(500001,5,[](unsigned){})==Q::Admission::Stopped,"closed admission cannot reopen after drain");
    Check(limit==1?std::all_of(groups.begin(),groups.end(),[](auto n){return n==1;}):
        std::find(groups.begin(),groups.end(),3)!=groups.end(),"actual execute groups obey the selected limit");
}
}
int main()try{
    using tinyimx::message::ParsePrivateReadyBatchLimit;
    Check(ParsePrivateReadyBatchLimit(nullptr)==64&&ParsePrivateReadyBatchLimit("64")==64&&ParsePrivateReadyBatchLimit("1")==1,
        "default64 and exact experimental1 are explicit");
    for(const char*bad:{"","0","2","16","65","-1","01","1 "," 1","garbage"}){
        bool rejected=false;try{(void)ParsePrivateReadyBatchLimit(bad);}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"malformed or undeclared batch limit fails before worker creation");
    }
    Exercise(64);Exercise(1);
    std::cout<<"READY_BATCH_PREPARATION_LIMIT_FIFO_BOUND_DRAIN_AND_HOL_CONTRACTS_PASS"<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
