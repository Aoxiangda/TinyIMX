#include "gateway/business/BusinessExecutor.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using tinyimx::BusinessExecutor;
using tinyimx::BusinessSubmitStatus;

struct Event {
    std::mutex mutex;
    std::condition_variable cv;
    bool ready{false};
    void Set() { std::lock_guard lock(mutex); ready=true; cv.notify_all(); }
    bool Wait(std::chrono::milliseconds timeout=2000ms) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock,timeout,[&]{return ready;});
    }
};
struct ReleaseGuard {
    Event& event;
    ~ReleaseGuard() { event.Set(); }
};
tinyimx::BusinessExecutorOptions Options(std::size_t workers=2,std::size_t capacity=32) {
    tinyimx::BusinessExecutorOptions o;
    o.worker_threads=workers; o.max_pending_tasks=64;
    o.stripe_count=64; o.per_stripe_queue_capacity=capacity;
    o.default_deadline=3s; o.shutdown_timeout=3s;
    o.isolate_ordering_keys=true;
    return o;
}
BusinessExecutor::TaskSpec Task(std::uint64_t key,BusinessExecutor::Work work) {
    BusinessExecutor::TaskSpec task;
    task.request.ordering_key=key;
    task.request.operation="exact-key-contract";
    task.work=std::move(work);
    task.dispatcher=[](auto complete){if(complete)complete();};
    return task;
}
bool Accepted(BusinessSubmitStatus s) { return s==BusinessSubmitStatus::kAccepted; }
bool Clean(const BusinessExecutor& executor) {
    const auto s=executor.GetStats();
    return s.current_pending_tasks==0 && s.current_active_tasks==0 &&
        s.current_async_tasks==0 && s.pending_completions==0;
}

bool CollisionControl(bool isolate) {
    auto options=Options(); options.isolate_ordering_keys=isolate;
    Event entered,release,other,same;
    BusinessExecutor executor(options); ReleaseGuard guard{release};
    if(!executor.Start())return false;
    if(!Accepted(executor.Submit(Task(1,[&](const auto&){
        entered.Set(); if(!release.Wait())throw std::runtime_error("gate watchdog");
        return BusinessExecutor::Completion{};
    }))) || !entered.Wait())return false;
    if(!Accepted(executor.Submit(Task(65,[&](const auto&){other.Set();return BusinessExecutor::Completion{};}))))return false;
    if(!Accepted(executor.Submit(Task(1,[&](const auto&){same.Set();return BusinessExecutor::Completion{};}))))return false;
    // A real blocked work item, an idle second worker and two colliding keys.
    // The control must reproduce head-of-line blocking; the candidate may only
    // release the unrelated key. Same-key work is fenced in both modes.
    const bool progress=other.Wait(isolate?2000ms:50ms);
    const bool escaped=same.Wait(20ms);
    release.Set();
    return progress==isolate && !escaped && other.Wait() && same.Wait() &&
        executor.ShutdownGraceful() && Clean(executor);
}

bool FifoAndRetirement() {
    auto options=Options(4,64); options.max_pending_tasks=128;
    BusinessExecutor executor(options);
    if(!executor.Start())return false;
    std::mutex mutex; std::vector<int> order; std::atomic<int> active{0},overlap{0};
    for(int i=0;i<48;++i) {
        if(!Accepted(executor.Submit(Task(0,[&,i](const auto&){
            if(active.fetch_add(1)!=0)++overlap;
            {std::lock_guard lock(mutex);order.push_back(i);}
            --active;return BusinessExecutor::Completion{};
        }))))return false;
    }
    Event fence;
    if(!Accepted(executor.Submit(Task(0,[&](const auto&){fence.Set();return BusinessExecutor::Completion{};}))) || !fence.Wait())return false;
    // Repeatedly revisit a retired key while also creating distinct lanes.
    for(int i=1;i<=48;++i) {
        Event done;
        if(!Accepted(executor.Submit(Task(64U*i,[&](const auto&){done.Set();return BusinessExecutor::Completion{};}))) || !done.Wait())return false;
    }
    if(!executor.ShutdownGraceful() || !Clean(executor) || overlap!=0 || order.size()!=48)return false;
    for(int i=0;i<48;++i)if(order[i]!=i)return false;
    return true;
}

bool AggregateBudget() {
    BusinessExecutor executor(Options(2,2)); Event release,first,second;
    ReleaseGuard guard{release};
    if(!executor.Start())return false;
    auto blocked=[&](Event& entered){return [&](const auto&){entered.Set();if(!release.Wait())throw std::runtime_error("gate watchdog");return BusinessExecutor::Completion{};};};
    if(!Accepted(executor.Submit(Task(1,blocked(first)))) || !first.Wait())return false;
    if(!Accepted(executor.Submit(Task(65,blocked(second)))) || !second.Wait())return false;
    auto denied=executor.Submit(Task(129,[](const auto&){return BusinessExecutor::Completion{};}));
    release.Set();
    const auto stats=executor.GetStats();
    return denied==BusinessSubmitStatus::kHotKeyOverloaded && stats.rejected_hot_key_total==1&&
        stats.rejected_pending_budget_total==0&&stats.rejected_pool_submission_total==0&&stats.rejected_key_allocation_total==0&&
        executor.ShutdownGraceful() && Clean(executor);
}

bool CancellationDeadlineAndMustRun() {
    BusinessExecutor executor(Options(2)); Event release,first,second;
    ReleaseGuard guard{release}; std::atomic<int> cancelled{0},expired{0},terminal{0},must{0},reply{0};
    if(!executor.Start())return false;
    auto block=[&](Event& entered){return [&](const auto&){entered.Set();release.Wait();return BusinessExecutor::Completion{};};};
    if(!Accepted(executor.Submit(Task(1,block(first)))) || !first.Wait() ||
       !Accepted(executor.Submit(Task(65,block(second)))) || !second.Wait())return false;
    auto cancel=Task(1,[&](const auto&){++cancelled;return BusinessExecutor::Completion{};});
    cancel.still_valid=[]{return false;};
    auto expiry=Task(65,[&](const auto&){++expired;return BusinessExecutor::Completion{};});
    expiry.request.deadline=tinyimx::BusinessClock::now()+30ms;
    expiry.deadline_expired_before_start_completion=[&]{++terminal;};
    auto durable=Task(1,[&](const auto&){++must;return BusinessExecutor::Completion([&]{++reply;});});
    durable.cancellation_policy=tinyimx::BusinessCancellationPolicy::kMustRun;
    durable.still_valid=[]{return false;}; durable.completion_still_valid=[]{return false;};
    if(!Accepted(executor.Submit(std::move(cancel))) || !Accepted(executor.Submit(std::move(expiry))) ||
       !Accepted(executor.Submit(std::move(durable))))return false;
    std::this_thread::sleep_for(50ms);release.Set();
    return executor.ShutdownGraceful() && Clean(executor) &&
        cancelled==0 && expired==0 && terminal==1 && must==1 && reply==0;
}

bool ExceptionProgress() {
    BusinessExecutor executor(Options()); Event later;std::atomic<int> completions{0};
    if(!executor.Start())return false;
    auto throws=Task(1,[](const auto&)->BusinessExecutor::Completion {throw std::runtime_error("owned failure");});
    auto completion=Task(1,[](const auto&){return BusinessExecutor::Completion([]{throw std::runtime_error("owned completion failure");});});
    auto last=Task(1,[&](const auto&){later.Set();return BusinessExecutor::Completion([&]{++completions;});});
    if(!Accepted(executor.Submit(std::move(throws))) || !Accepted(executor.Submit(std::move(completion))) ||
       !Accepted(executor.Submit(std::move(last))) || !later.Wait())return false;
    return executor.ShutdownGraceful() && Clean(executor) && completions==1;
}

bool AsyncFenceAndShutdown(bool fail_first) {
    BusinessExecutor executor(Options(1)); Event entered,unrelated,follower;
    std::mutex mutex;BusinessExecutor::AsyncDone done;std::atomic<int> completions{0};
    struct FinishGuard {
        std::mutex& mutex;BusinessExecutor::AsyncDone& done;
        ~FinishGuard() {
            BusinessExecutor::AsyncDone finish;
            {std::lock_guard lock(mutex);finish=done;}
            if(finish)finish({},std::make_exception_ptr(std::runtime_error("fixture cleanup")));
        }
    } guard{mutex,done};
    if(!executor.Start())return false;
    BusinessExecutor::TaskSpec task;task.request.ordering_key=0;task.request.operation="suspended-key";
    task.async_work=[&](const auto&,auto,BusinessExecutor::AsyncDone finish){
        {std::lock_guard lock(mutex);done=std::move(finish);}entered.Set();
    };
    task.dispatcher=[](auto completion){if(completion)completion();};
    if(!Accepted(executor.Submit(std::move(task))) || !entered.Wait())return false;
    if(!Accepted(executor.Submit(Task(64,[&](const auto&){unrelated.Set();return BusinessExecutor::Completion{};}))) || !unrelated.Wait())return false;
    if(!Accepted(executor.Submit(Task(0,[&](const auto&){follower.Set();return BusinessExecutor::Completion{};}))) || follower.Wait(20ms))return false;
    executor.BeginDrain();
    if(executor.Submit(Task(128,[](const auto&){return BusinessExecutor::Completion{};}))!=BusinessSubmitStatus::kShuttingDown)return false;
    auto shutdown=std::async(std::launch::async,[&]{return executor.ShutdownGraceful();});
    const bool waited=shutdown.wait_for(30ms)==std::future_status::timeout;
    BusinessExecutor::AsyncDone finish;{std::lock_guard lock(mutex);finish=done;}
    const auto error=fail_first?std::make_exception_ptr(std::runtime_error("owned async failure")):std::exception_ptr{};
    finish([&]{++completions;},error);
    finish([&]{++completions;},error); // Duplicate transport reaction is fenced.
    return waited && follower.Wait() && shutdown.get() && Clean(executor) &&
        completions==(fail_first?0:1);
}

bool InlineAsync() {
    auto o=Options(4,64);o.max_pending_tasks=128;BusinessExecutor executor(o);
    if(!executor.Start())return false;
    std::atomic<int> completed{0};
    for(int i=0;i<48;++i) {
        BusinessExecutor::TaskSpec task;task.request.ordering_key=std::uint64_t(i%2)*64;
        task.async_work=[&](const auto&,auto,auto done){done([&]{++completed;},{});};
        task.dispatcher=[](auto complete){if(complete)complete();};
        if(!Accepted(executor.Submit(std::move(task))))return false;
    }
    return executor.ShutdownGraceful() && Clean(executor) && completed==48;
}
}

int main() {
    const std::vector<std::pair<const char*,bool(*)()>> tests={
        {"baseline reproduces hash-stripe head-of-line blocking",[]{return CollisionControl(false);}},
        {"unrelated colliding key progresses; same key stays fenced",[]{return CollisionControl(true);}},
        {"FIFO, zero key and lane retirement",FifoAndRetirement},
        {"aggregate stripe admission budget unchanged",AggregateBudget},
        {"cancellation, deadline and durable-work completion fence",CancellationDeadlineAndMustRun},
        {"work and completion exceptions allow following work",ExceptionProgress},
        {"suspended async lane and graceful shutdown",[]{return AsyncFenceAndShutdown(false);}},
        {"async exception and duplicate completion release one lane",[]{return AsyncFenceAndShutdown(true);}},
        {"inline async completion race",InlineAsync}
    };
    int failed=0;
    for(const auto& [name,run]:tests) {
        bool ok=false;try{ok=run();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
        std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failed;
    }
    return failed?1:0;
}
