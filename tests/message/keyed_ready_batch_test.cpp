#include "common/runtime/BoundedKeyedReadyBatch.h"
#include "services/message/service/MessageReadyBatchPolicy.h"
#include <chrono>
#include <future>
#include <iostream>
#include <set>

namespace {
using namespace std::chrono_literals;
using Queue=tinyimx::BoundedKeyedReadyBatch<int,int>;
void Require(bool good,const char* text){if(!good)throw std::runtime_error(text);}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool entered=false,open=false;
    void Enter(){std::unique_lock lock(mutex);entered=true;cv.notify_all();Require(cv.wait_for(lock,5s,[&]{return open;}),"gate watchdog");}
    void Wait(){std::unique_lock lock(mutex);Require(cv.wait_for(lock,5s,[&]{return entered;}),"worker did not enter");}
    void Release(){std::lock_guard lock(mutex);open=true;cv.notify_all();}
};
struct ReleaseGate{Gate& gate;~ReleaseGate(){gate.Release();}};
void ReadyKeysCanMoveWhileCollidingSenderWaits(){
    Gate gate;std::atomic<bool>same_started{false};
    Queue queue({2,4,1},[&](const auto& inputs){
        Require(inputs.size()==1,"limit1 changed");
        if(inputs[0]==10)gate.Enter();if(inputs[0]==11)same_started=true;
        return inputs;
    },-1);
    ReleaseGate release{gate};std::promise<int>a,b,c;
    Require(queue.Submit(1,10,[&](int r){a.set_value(r);})==Queue::Admission::Accepted,"first rejected");gate.Wait();
    Require(queue.Submit(1,11,[&](int r){b.set_value(r);})==Queue::Admission::Accepted,"same sender rejected");
    // Keys1 and3 collide in the legacy two-worker modulo scheduler.
    Require(queue.Submit(3,30,[&](int r){c.set_value(r);})==Queue::Admission::Accepted,"different sender rejected");
    auto other=c.get_future();Require(other.wait_for(2s)==std::future_status::ready&&other.get()==30,"free worker could not take a colliding ready sender");
    Require(!same_started.load(),"same sender ran before its predecessor completed");
    gate.Release();queue.Drain();Require(a.get_future().get()==10&&b.get_future().get()==11,"sender FIFO result changed");
    const auto s=queue.Snapshot();Require(s.accepted==3&&s.completed==3&&!s.pending&&!s.execute_failures,"ready-key accounting");
}
void CapacityBatchFairnessAndDrain(){
    Gate gate;std::mutex mutex;std::vector<int>executed,completed;std::vector<std::size_t>sizes;
    Queue queue({1,5,64},[&](const auto& inputs){
        if(inputs.front()==0)gate.Enter();
        {std::lock_guard lock(mutex);sizes.push_back(inputs.size());executed.insert(executed.end(),inputs.begin(),inputs.end());}
        return inputs;
    },-1);ReleaseGate release{gate};
    auto done=[&](int r){std::lock_guard lock(mutex);completed.push_back(r);};
    Require(queue.Submit(7,0,done)==Queue::Admission::Accepted,"gate rejected");gate.Wait();
    for(const auto [key,value]:std::vector<std::pair<int,int>>{{8,1},{8,2},{9,3},{10,4}})
        Require(queue.Submit(key,value,done)==Queue::Admission::Accepted,"pending rejected below capacity");
    Require(queue.Submit(11,5,done)==Queue::Admission::Overloaded,"capacity omitted executing or queued input");
    std::promise<void>drained;std::thread stop([&]{queue.Drain();drained.set_value();});
    struct Join{std::thread& thread;Gate& gate;~Join(){gate.Release();if(thread.joinable())thread.join();}}join{stop,gate};
    auto future=drained.get_future();Require(future.wait_for(20ms)==std::future_status::timeout,"drain returned while accepted input blocked");
    gate.Release();Require(future.wait_for(3s)==std::future_status::ready,"drain lost an accepted key");stop.join();
    Require(executed==std::vector<int>({0,1,3,4,2})&&completed==executed,"per-key FIFO or ready-key fairness changed");
    Require(sizes==std::vector<std::size_t>({1,3,1}),"batch consumed a second item from an active sender");
    auto s=queue.Snapshot();Require(s.accepted==5&&s.completed==5&&s.overloaded==1&&s.pending==0&&s.peak_pending==5,"bound/drain accounting");
    Require(queue.Submit(7,6,done)==Queue::Admission::Stopped,"post-stop admission accepted");
}
void CallbackReentryAndFailureBoundaries(){
    std::atomic<int>executions{0},callbacks{0};std::promise<int>last;
    Queue queue({2,4,64},[&](const auto& in){++executions;return in;},-1);
    Require(queue.Submit(41,1,[&](int r){
        Require(r==1,"first result");++callbacks;
        Require(queue.Submit(41,2,[&](int next){++callbacks;last.set_value(next);})==Queue::Admission::Accepted,"inline same-key enqueue lost");
    })==Queue::Admission::Accepted,"reentry rejected");
    auto future=last.get_future();Require(future.wait_for(3s)==std::future_status::ready&&future.get()==2,"reentrant sender lost");
    queue.Drain();Require(callbacks==2&&executions==2,"callback reentry replayed work");
    for(bool throw_after_effect:{false,true}){
        std::atomic<int>effects{0},replies{0};std::promise<int>reply;
        Queue bad({1,2,1},[&](const auto&)->std::vector<int>{++effects;if(throw_after_effect)throw std::runtime_error("uncertain commit");return {};},-7);
        Require(bad.Submit(1,1,[&](int r){++replies;reply.set_value(r);})==Queue::Admission::Accepted,"failure case rejected");
        Require(reply.get_future().get()==-7,"uncertain/malformed execution falsely successful");bad.Drain();
        Require(effects==1&&replies==1&&bad.Snapshot().execute_failures==1&&!bad.Snapshot().pending,"uncertain execution was retried or dropped");
    }
    Queue throwing({1,2,1},[](const auto&in){return in;},-1);
    throwing.Submit(1,1,[](int){throw std::runtime_error("broken completion");});throwing.Drain();
    Require(throwing.Snapshot().completion_failures==1&&throwing.Snapshot().completed==1,"completion exception stranded a sender");
}
void Policy(){
    using namespace tinyimx::message;
    Require(!ParsePrivateReadyKeyedScheduler(nullptr)&&!ParsePrivateReadyKeyedScheduler("0")&&ParsePrivateReadyKeyedScheduler("1"),"scheduler flag policy");
    for(const auto*v:{"","2","true","01"," 1","1 "}){
        bool threw=false;try{(void)ParsePrivateReadyKeyedScheduler(v);}catch(const std::invalid_argument&){threw=true;}
        Require(threw,"malformed scheduler flag accepted");
    }
}
}
int main(){try{Policy();ReadyKeysCanMoveWhileCollidingSenderWaits();CapacityBatchFairnessAndDrain();CallbackReentryAndFailureBoundaries();
    std::cout<<"PASS keyed ready: colliding users independent, same-sender FIFO, bounded batches/capacity, shutdown, reentry and uncertain outcomes\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
