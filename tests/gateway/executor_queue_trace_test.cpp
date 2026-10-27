#include "gateway/ChatRequestPhaseTrace.h"
#include "gateway/business/BusinessExecutor.h"
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
namespace {
using namespace std::chrono_literals;
using tinyimx::BusinessClock;
using tinyimx::BusinessRequestContext;
using tinyimx::BusinessExecutor;
void Require(bool yes,const char* s){if(!yes)throw std::runtime_error(s);}
struct Gate {
    std::mutex mutex;std::condition_variable cv;bool release{false};
    void Release(){std::lock_guard lock(mutex);release=true;cv.notify_all();}
    void Wait(){std::unique_lock lock(mutex);cv.wait(lock,[&]{return release;});}
};
void Boundary(bool ordered,bool isolated) {
    tinyimx::BusinessExecutorOptions options;options.worker_threads=1;options.isolate_ordering_keys=isolated;
    BusinessExecutor executor(options);Require(executor.Start(),"start");
    auto gate=std::make_shared<Gate>();auto entered=std::make_shared<std::promise<void>>();
    auto observed=std::make_shared<std::promise<BusinessRequestContext>>();
    struct Guard{std::shared_ptr<Gate> gate;~Guard(){gate->Release();}} guard{gate};
    BusinessExecutor::TaskSpec first;if(ordered)first.request.ordering_key=7;
    first.work=[gate,entered](const auto&){entered->set_value();gate->Wait();return BusinessExecutor::Completion{};};
    Require(executor.Submit(std::move(first))==tinyimx::BusinessSubmitStatus::kAccepted,"first accepted");
    Require(entered->get_future().wait_for(2s)==std::future_status::ready,"first entered");
    BusinessExecutor::TaskSpec second;if(ordered)second.request.ordering_key=7;
    second.request.operation="queue-boundary";second.request.user_id=42;second.request.request_seq=17;second.request.session_epoch=9;
    second.request.received_at=BusinessClock::now();second.request.deadline=second.request.received_at+3s;
    second.request.executor_submitted_at=second.request.received_at+1h;
    second.request.executor_enqueued_at=second.request.executor_submitted_at;second.request.executor_started_at=second.request.executor_submitted_at;
    const auto original=second.request;auto future=observed->get_future();
    second.work=[observed](const auto& context){observed->set_value(context.Request());return BusinessExecutor::Completion{};};
    const auto before=BusinessClock::now();
    Require(executor.Submit(std::move(second))==tinyimx::BusinessSubmitStatus::kAccepted,"second accepted");
    const auto after=BusinessClock::now();
    Require(future.wait_for(0ms)==std::future_status::timeout,"queue bypassed blocked worker");
    const auto released=BusinessClock::now();gate->Release();
    Require(future.wait_for(2s)==std::future_status::ready,"second released");const auto actual=future.get();
    Require(before<=actual.executor_submitted_at && actual.executor_submitted_at<=actual.executor_enqueued_at &&
            actual.executor_enqueued_at<=after && released<=actual.executor_started_at,"timestamps not actual scheduling boundaries");
    Require(actual.received_at==original.received_at && actual.deadline==original.deadline && actual.operation==original.operation &&
            actual.ordering_key==original.ordering_key && actual.user_id==42 && actual.request_seq==17 && actual.session_epoch==9,"context mutated");
    tinyimx::ChatRequestPhaseTrace trace(actual,"queue",nullptr,BusinessClock::now(),true);
    Require(trace.Current().executor_queue_wait_us>=0 && trace.Current().executor_admission_us>=0,"trace missing");
    Require(executor.ShutdownGraceful(),"shutdown");const auto stats=executor.GetStats();
    Require(stats.accepted_total==2 && stats.completed_total==2 && !stats.current_pending_tasks && !stats.pending_completions,"lifecycle");
}
void Synthetic(){
    BusinessRequestContext r;r.received_at=BusinessClock::time_point{1s};r.executor_submitted_at=r.received_at+2ms;
    r.executor_enqueued_at=r.executor_submitted_at+3ms;r.executor_started_at=r.executor_enqueued_at+7ms;
    tinyimx::ChatRequestPhaseTrace trace(r,"synthetic",nullptr,r.executor_started_at+1ms,true);
    Require(trace.Current().executor_admission_us==3000 && trace.Current().executor_queue_wait_us==7000 && trace.Current().dispatch_age_us==13000,"mixed timing");
    BusinessRequestContext absent;
    tinyimx::ChatRequestPhaseTrace unknown(absent,"missing",nullptr,BusinessClock::now(),true);
    Require(unknown.Current().executor_queue_wait_us==-1,"missing falsely zero");
    r.executor_started_at=r.executor_enqueued_at-1ms;
    tinyimx::ChatRequestPhaseTrace invalid(r,"invalid",nullptr,BusinessClock::now(),true);
    Require(invalid.Current().executor_queue_wait_us==-1,"negative queue accepted");
}
}
int main(){try{Synthetic();Boundary(false,false);Boundary(true,false);Boundary(true,true);
    std::cout<<"PASS unordered/stripe/exact-key queue boundaries; context and lifecycle preserved\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
