#include "common/runtime/ReadyPhysicalWriteGroup.h"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Input {unsigned id;unsigned kind;bool sync;};
using Group=tinyimx::ReadyPhysicalWriteGroup<Input>;
void Check(bool ok,const char* text){
    if(!ok)throw std::runtime_error(text);
    std::cout<<"PASS "<<text<<std::endl;
}
template<class Predicate>void Wait(Predicate predicate,const char* why){
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate()){
        if(std::chrono::steady_clock::now()>=until)throw std::runtime_error(why);
        std::this_thread::yield();
    }
}
void ReadyAndUncertain(bool ambiguous){
    Group group;
    std::promise<void> entered,release;
    const auto released=release.get_future().share();
    std::mutex mutex;std::set<unsigned> committed;
    std::vector<std::vector<unsigned>> physical;
    std::atomic<unsigned> failed{0},returned_without_commit{0};
    auto execute=[&](std::span<const Input> inputs){
        if(inputs.front().id==0){entered.set_value();released.wait();}
        {
            std::lock_guard lock(mutex);
            std::vector<unsigned> ids;
            for(auto input:inputs){committed.insert(input.id);ids.push_back(input.id);}
            physical.push_back(std::move(ids));
        }
        // Simulate uncertain reply after a completed side effect. A second
        // invocation would be an invalid replay, even if its effect is idempotent.
        if(ambiguous&&inputs.front().id!=0)throw std::runtime_error("uncertain physical reply");
    };
    auto call=[&](unsigned id){
        try{
            group.Run(Input{id,id%3,id%2==0},64,execute);
            std::lock_guard lock(mutex);
            if(!committed.contains(id))++returned_without_commit;
        }catch(const std::runtime_error& e){
            if(std::string(e.what())!="uncertain physical reply")throw;
            ++failed;
        }
    };
    std::vector<std::future<void>> calls;calls.reserve(16);
    calls.push_back(std::async(std::launch::async,call,0));
    entered.get_future().wait();
    try{
        for(unsigned i=1;i<16;++i)calls.push_back(std::async(std::launch::async,call,i));
        Wait([&]{return group.Snapshot().queued==15;},"ready followers were not admitted");
        for(unsigned i=1;i<16;++i)
            Check(calls[i].wait_for(std::chrono::milliseconds(0))==std::future_status::timeout,
                "waiting physical caller never returns before its commit");
    }catch(...){release.set_value();for(auto& call:calls)call.wait();throw;}
    release.set_value();for(auto& call:calls)call.get();
    const auto stats=group.Snapshot();
    Check(physical.size()==2&&physical[0].size()==1&&physical[1].size()==15&&committed.size()==16,
        "mixed create confirmation and progress inputs drain in one already-ready group");
    Check(stats.calls==16&&stats.execute_calls==2&&stats.grouped_inputs==15&&stats.largest_group==15&&
        stats.fallbacks==0&&stats.held_input_bytes==0&&stats.queued==0,
        "all admitted synchronous callers drain with bounded ownership");
    Check(returned_without_commit==0&&failed==(ambiguous?15u:0u),
        ambiguous?"uncertain result reaches every group caller without any replay":
                  "every caller succeeds only after its own committed record exists");
}
void OriginalEntranceFallback(bool bytes){
    Group group({64,bytes?128u:1u,bytes?2u:1024u});
    std::promise<void> entered,release;
    const auto released=release.get_future().share();
    std::mutex mutex;std::vector<unsigned> attempts;
    auto execute=[&](std::span<const Input> inputs){
        if(inputs.front().id==0){entered.set_value();released.wait();}
        std::lock_guard lock(mutex);
        for(auto input:inputs)attempts.push_back(input.id);
    };
    auto first=std::async(std::launch::async,[&]{group.Run(Input{0,0,true},bytes?2:1,execute);});
    entered.get_future().wait();
    std::future<void> queued;
    try{
        if(!bytes){
            queued=std::async(std::launch::async,[&]{group.Run(Input{1,1,true},1,execute);});
            Wait([&]{return group.Snapshot().queued==1;},"one follower was not queued");
        }
        group.Run(Input{2,2,false},1,execute);
        std::lock_guard lock(mutex);
        Check(attempts==std::vector<unsigned>{2},
            bytes?"serialized-byte cap uses original single-write entrance":
                  "full ready queue uses original single-write entrance");
    }catch(...){release.set_value();first.wait();if(queued.valid())queued.wait();throw;}
    release.set_value();first.get();if(queued.valid())queued.get();
    const auto stats=group.Snapshot();
    Check(stats.fallbacks==1&&stats.execute_calls==(bytes?2u:3u)&&stats.held_input_bytes==0&&stats.queued==0,
        "fallback invokes each distinct input once and leaves no held bytes");
}
}
int main(){
    try{
        ReadyAndUncertain(false);ReadyAndUncertain(true);
        OriginalEntranceFallback(false);OriginalEntranceFallback(true);
        bool rejected=false;
        try{Group invalid({65,128,1024});}catch(const std::invalid_argument&){rejected=true;}
        Check(rejected,"overlarge group configuration is rejected");
        std::cout<<"READY_PHYSICAL_WRITE_GROUP_CALLER_CONTRACTS_PASS"<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
