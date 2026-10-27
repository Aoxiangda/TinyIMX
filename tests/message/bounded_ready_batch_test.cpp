#include "common/runtime/BoundedReadyBatch.h"
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using Queue=tinyimx::BoundedReadyBatch<int,int>;
void Require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool opened=false,entered=false;
    void Enter(){std::unique_lock lock(mutex);entered=true;cv.notify_all();
        if(!cv.wait_for(lock,3s,[&]{return opened;}))throw std::runtime_error("gate watchdog");}
    void Release(){std::lock_guard lock(mutex);opened=true;cv.notify_all();}
    void Wait(){std::unique_lock lock(mutex);Require(cv.wait_for(lock,2s,[&]{return entered;}),"worker did not enter");}
};
struct Release{Gate& gate;~Release(){gate.Release();}};
void BatchDrainAndBudget(){
    Gate gate;std::mutex mutex;std::vector<int> completed;std::vector<std::vector<int>> batches;
    Queue q({1,6,3},[&](const auto& inputs){
        if(inputs.front()==0)gate.Enter();
        std::lock_guard lock(mutex);batches.push_back(inputs);return inputs;
    },-1);Release release{gate};
    const auto submit=[&](int v){return q.Submit(7,v,[&](int result){std::lock_guard lock(mutex);completed.push_back(result);});};
    Require(submit(0)==Queue::Admission::Accepted,"first accepted without a collection timer");gate.Wait();
    for(int i=1;i<6;++i)Require(submit(i)==Queue::Admission::Accepted,"bounded queued work accepted");
    Require(submit(6)==Queue::Admission::Overloaded,"running task omitted from global budget");
    Require(completed.empty(),"completed before commit gate");gate.Release();q.Drain();
    Require(completed==std::vector<int>({0,1,2,3,4,5}),"FIFO result order");
    Require(batches==std::vector<std::vector<int>>({{0},{1,2,3},{4,5}}),"only ready requests drained under max batch");
    Require(submit(9)==Queue::Admission::Stopped,"drain admission not fenced");
    const auto s=q.Snapshot();Require(s.accepted==6&&s.completed==6&&s.pending==0&&s.peak_pending==6&&
        s.batch_sizes[1]==1&&s.batch_sizes[2]==1&&s.batch_sizes[3]==1&&s.overloaded==1,"lifecycle statistics");
}
void IndependentShards(){
    Gate gate;std::promise<int> second;Queue q({2,4,4},[&](const auto& inputs){if(inputs.front()==0)gate.Enter();return inputs;},-1);
    Release release{gate};Require(q.Submit(0,0,[](int){})==Queue::Admission::Accepted,"first shard");gate.Wait();
    auto ready=second.get_future();Require(q.Submit(1,1,[&](int n){second.set_value(n);})==Queue::Admission::Accepted,"second shard");
    Require(ready.wait_for(1s)==std::future_status::ready&&ready.get()==1,"unrelated shard blocked behind first");gate.Release();q.Drain();
}
void ExecuteFailures(){
    for(int mode: {0,1}){
        int results=0,last=0;Queue q({1,2,2},[&](const auto& in)->std::vector<int>{if(mode==0)throw std::runtime_error("owned failure");return {};},-7);
        Require(q.Submit(0,1,[&](int n){++results;last=n;})==Queue::Admission::Accepted,"failure admitted");q.Drain();
        Require(results==1&&last==-7&&q.Snapshot().execute_failures==1&&q.Snapshot().pending==0,"failed execute retried or lost completion");
    }
    Queue q({1,3,2},[](const auto& in){return in;},-1);
    Require(q.Submit(0,1,[](int){throw std::runtime_error("owned callback failure");})==Queue::Admission::Accepted,"throwing callback admission");
    q.Drain();Require(q.Snapshot().completion_failures==1&&q.Snapshot().completed==1&&q.Snapshot().pending==0,"callback failure lost pending budget");
}
void InvalidBounds(){
    for(auto options: {Queue::Options{0,1,1},Queue::Options{17,1,1},Queue::Options{1,0,1},Queue::Options{1,513,1},Queue::Options{1,1,0},Queue::Options{1,1,65}}){
        bool rejected=false;try{Queue q(options,[](const auto& in){return in;},-1);}catch(const std::invalid_argument&){rejected=true;}
        Require(rejected,"invalid resource bound accepted");
    }
}
}
int main(){try{BatchDrainAndBudget();IndependentShards();ExecuteFailures();InvalidBounds();
    std::cout<<"PASS ready-only batches; global in-flight budget; per-key FIFO; independent shards; drain; failure exactly-once; bounds\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
