#include "gateway/PrivateReplayAdmission.h"
#include "gateway/business/BusinessExecutor.h"
#include "gateway/ReceiverDeliveryTracker.h"
#include "common/logging/Logger.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using namespace tinyimx;
using namespace std::chrono_literals;
namespace {
void Require(bool ok, const char* text) {
    if (!ok) { std::cerr << "FAIL " << text << '\n'; std::exit(1); }
}
template<class P> void Wait(P predicate, const char* text) {
    const auto limit = BusinessClock::now() + 5s;
    while (!predicate() && BusinessClock::now() < limit) std::this_thread::sleep_for(1ms);
    Require(predicate(), text);
}
struct Fixture {
    std::atomic<std::int64_t> ms{100000};
    std::shared_ptr<PrivateReplayAdmission> manager =
        std::make_shared<PrivateReplayAdmission>([this] {
            return PrivateReplayAdmission::TimePoint{std::chrono::milliseconds{ms.load()}};
        });
    void Advance() { ms.fetch_add(2200); }
};
BusinessExecutorOptions Options(bool hot = false) {
    BusinessExecutorOptions o;
    o.name = "p1b-r1-test-only";
    o.worker_threads = 1; o.max_pending_tasks = hot ? 8 : 1;
    o.stripe_count = 2; o.per_stripe_queue_capacity = 1;
    o.default_deadline = 5s; o.shutdown_timeout = 5s;
    return o;
}
struct Barrier {
    std::promise<void> entered, release;
    std::shared_future<void> can_leave = release.get_future().share();
    void Hold(BusinessExecutor& e) {
        auto ready = entered.get_future();
        BusinessExecutor::TaskSpec t;
        t.request.operation = "test.barrier"; t.request.ordering_key = 0;
        t.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        t.work = [this](const BusinessExecutor::ExecutionContext&) {
            entered.set_value(); can_leave.wait(); return BusinessExecutor::Completion{};
        };
        Require(e.Submit(std::move(t)) == BusinessSubmitStatus::kAccepted, "barrier accepted");
        Require(ready.wait_for(5s) == std::future_status::ready, "barrier started");
    }
};
BusinessSubmitStatus Submit(BusinessExecutor& e, PrivateReplayAdmission::AttemptPtr lease,
                            std::atomic<int>& runs, BusinessTimePoint deadline = {},
                            bool valid = true) {
    BusinessExecutor::TaskSpec t;
    t.request.operation = "gateway.pending_replay";
    t.request.ordering_key = lease->Identity().user_id;
    t.request.deadline = deadline;
    t.still_valid = [valid] { return valid; };
    t.work = [lease, &runs](const BusinessExecutor::ExecutionContext&) {
        if (lease->MarkStarted()) ++runs;
        return BusinessExecutor::Completion{};
    };
    // Deliberately no deadline callback: the exact TaskSpec ownership lifecycle
    // must re-arm the intent when the work closure is destroyed without running.
    return e.Submit(std::move(t));
}
void Coalescing() {
    Fixture f;
    Require(!f.manager->Ensure(0, 1), "invalid user");
    Require(f.manager->Ensure(7, 10), "first ensure");
    auto held = f.manager->Claim(7, 10);
    Require(bool(held), "first claim");
    for (int n = 0; n < 1000; ++n) {
        f.manager->Ensure(7, 10);
        Require(!f.manager->Claim(7, 10), "cannot duplicate in-flight claim");
    }
    Require(f.manager->GetStats().retained == 1, "one intent per user");
    Require(held->MarkStarted(), "mark genuine work start");
    Require(!held->MarkStarted(), "cannot start attempt twice");
    Require(f.manager->GetStats().retained == 0, "start discharges admission only");
}
void Rejection(bool hot) {
    Fixture f;
    BusinessExecutor e(Options(hot)); Require(e.Start(), "executor start");
    Barrier b; b.Hold(e);
    const std::uint64_t user = hot ? 2 : 1;
    f.manager->Ensure(user, 10);
    auto lease = f.manager->Claim(user, 10);
    std::atomic<int> runs{0};
    const auto status = Submit(e, lease, runs);
    Require(status == (hot ? BusinessSubmitStatus::kHotKeyOverloaded : BusinessSubmitStatus::kOverloaded), "forced rejection");
    lease.reset();
    Require(f.manager->GetStats().deferred == 1, "rejection retained");
    Require(f.manager->TakeDue(128, 32).empty(), "backoff prevents busy retry");
    b.release.set_value();
    Wait([&]{ return e.GetStats().current_pending_tasks == 0; }, "barrier released");
    f.Advance();
    auto due = f.manager->TakeDue(128, 32);
    Require(due.size() == 1, "scheduler independently finds deferred intent");
    Require(Submit(e, due.front(), runs) == BusinessSubmitStatus::kAccepted, "readmission accepted");
    due.clear();
    Wait([&]{ return runs.load() == 1; }, "existing work automatically starts");
    Require(f.manager->GetStats().retained == 0, "admission discharged");
    Require(f.manager->GetStats().readmissions == 1, "readmission accounted");
    Require(e.ShutdownGraceful(), "executor drain");
}
void NeverStarted(bool expired) {
    Fixture f;
    auto o = Options(true); o.per_stripe_queue_capacity = 2;
    BusinessExecutor e(o); Require(e.Start(), "executor start");
    Barrier b; b.Hold(e);
    f.manager->Ensure(1, 10);
    auto lease = f.manager->Claim(1, 10);
    std::atomic<int> runs{0};
    auto deadline = expired ? BusinessClock::now() + 30ms : BusinessTimePoint{};
    Require(Submit(e, lease, runs, deadline, expired) == BusinessSubmitStatus::kAccepted, "never-started task accepted");
    lease.reset();
    if (expired) std::this_thread::sleep_until(deadline + 5ms);
    b.release.set_value();
    Wait([&]{return f.manager->GetStats().deferred == 1;}, "destroyed unstarted work re-arms intent");
    Require(runs.load() == 0, "no expired/cancelled work ran");
    const auto stats = e.GetStats();
    Require(expired ? stats.deadline_expired_before_start_total == 1 : stats.cancelled_before_start_total == 1, "executor exact terminal");
    f.Advance(); auto due = f.manager->TakeDue(128, 32);
    Require(due.size() == 1, "fresh attempt after nonexecution");
    Require(Submit(e, due.front(), runs) == BusinessSubmitStatus::kAccepted, "fresh task accepted");
    due.clear(); Wait([&]{return runs.load() == 1;}, "recovered work start");
    Require(e.ShutdownGraceful(), "drain");
}
void GenerationFence() {
    Fixture f;
    f.manager->Ensure(7, 10); auto old = f.manager->Claim(7, 10);
    f.manager->Ensure(7, 11); auto current = f.manager->Claim(7, 11);
    Require(!f.manager->Ensure(7, 10), "late old ensure cannot replace current intent");
    Require(!old->MarkStarted(), "old generation cannot start");
    old.reset();
    f.manager->Retire(7, 10);
    Require(f.manager->GetStats().retained == 1, "old retire cannot remove new intent");
    Require(current->MarkStarted(), "current still starts");
}
void LateDestructor() {
    Fixture f;
    f.manager->Ensure(7, 10); auto old = f.manager->Claim(7, 10);
    f.manager->Ensure(7, 11); auto current = f.manager->Claim(7, 11);
    old.reset();
    Require(f.manager->GetStats().deferred == 0, "old destruction does not defer new generation");
    Require(current->MarkStarted(), "new lease remains in-flight");
}
void Disconnect() {
    Fixture f;
    f.manager->Ensure(7, 10); auto held = f.manager->Claim(7, 10);
    f.manager->Retire(7, 10); held.reset(); f.Advance();
    Require(f.manager->TakeDue(128, 32).empty(), "disconnect cannot resurrect intent");
}
void RepeatedFailure() {
    Fixture f; f.manager->Ensure(7, 10);
    for (int n = 0; n < 100; ++n) {
        auto held = f.manager->Claim(7, 10); Require(bool(held), "retry not exhausted");
        held.reset();
        Require(!f.manager->Claim(7, 10), "retry obeys delay");
        f.Advance();
    }
    auto recovered = f.manager->Claim(7, 10);
    Require(recovered && recovered->MarkStarted(), "recovery after many rejected attempts");
    Require(f.manager->GetStats().peak_retained == 1, "no retry queue growth");
}
void FairBudget() {
    Fixture f;
    for (std::uint64_t u = 1; u <= 257; ++u) f.manager->Ensure(u, 10);
    std::vector<PrivateReplayAdmission::AttemptPtr> in_flight;
    for (int round = 0; round < 80; ++round) {
        auto due = f.manager->TakeDue(16, 4);
        Require(due.size() <= 4, "submit budget bounded");
        for (auto& item : due) in_flight.push_back(std::move(item));
    }
    Require(in_flight.size() == 257, "round-robin visits all retained users");
    Require(f.manager->TakeDue(512, 32).empty(), "no duplicate queued attempt");
    for (auto& item : in_flight) Require(item->MarkStarted(), "fair claim starts once");
    Require(f.manager->GetStats().retained == 0, "fair round discharged");
}
void StopFence() {
    Fixture f;
    auto o = Options(true); o.per_stripe_queue_capacity = 2;
    BusinessExecutor e(o); Require(e.Start(), "start"); Barrier b; b.Hold(e);
    f.manager->Ensure(1, 10); auto held = f.manager->Claim(1, 10);
    std::atomic<int> runs{0};
    Require(Submit(e, held, runs) == BusinessSubmitStatus::kAccepted, "queued before stop");
    held.reset(); f.manager->Stop(); b.release.set_value();
    Require(e.ShutdownGraceful(), "drain queued lease safely");
    Require(runs.load() == 0, "stopped generation does not invoke replay");
    Require(!f.manager->Ensure(2, 12), "no intent after stop");
    Require(f.manager->TakeDue(128, 32).empty(), "no task after stop");
}
void OwnerLifetime() {
    PrivateReplayAdmission::AttemptPtr held;
    {
        auto owner = std::make_shared<PrivateReplayAdmission>();
        owner->Ensure(1, 10); held = owner->Claim(1, 10);
    }
    Require(!held->MarkStarted(), "expired manager cannot start work");
    held.reset();
}
void RestartGeneration() {
    Fixture f;
    f.manager->Ensure(7, 10); auto old = f.manager->Claim(7, 10);
    f.manager->Stop(); f.manager->Resume();
    f.manager->Ensure(7, 10); auto current = f.manager->Claim(7, 10);
    Require(old->Identity().generation != current->Identity().generation,
            "restart must not reuse generation identities");
    Require(!old->MarkStarted(), "old lease fenced across restart");
    Require(current->MarkStarted(), "resumed admission starts new generation");
}
void ThreadSafety() {
    Fixture f;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back([&, t] {
        const std::uint64_t u = static_cast<std::uint64_t>(t + 1);
        for (std::uint64_t epoch = 1; epoch <= 500; ++epoch) {
            f.manager->Ensure(u, epoch);
            if (auto held = f.manager->Claim(u, epoch)) held->MarkStarted();
        }
    });
    for (auto& thread : threads) thread.join();
    Require(f.manager->GetStats().started == 2000, "concurrent bookkeeping accounted");
}
void ExistingAckSemantics() {
    ReceiverDeliveryTracker tracker;
    Require(tracker.RegisterAttempt(246881, 503276, 101) == ReceiverDeliveryRegisterStatus::kRegistered, "original delivery");
    Require(tracker.RegisterRetryAttempt(246881, 503276, 101, 102, 3) == ReceiverDeliveryRetryRegisterStatus::kRetryRegistered, "fresh retry");
    Require(tracker.Acknowledge(246881, 503276, 999) == ReceiverDeliveryAckStatus::kUnknownAttempt, "reject unknown ACK");
    Require(tracker.Acknowledge(246881, 503276, 101) == ReceiverDeliveryAckStatus::kConfirmed, "late known ACK still confirms");
    Require(tracker.Acknowledge(246881, 503276, 102) == ReceiverDeliveryAckStatus::kDuplicate, "duplicate ACK remains idempotent");
}
}
int main() {
    Require(Logger::Instance().Init("error", "", true), "logger init");
    const std::vector<std::pair<const char*, void(*)()>> tests{
        {"coalesced_single_intent", Coalescing},
        {"global_rejection_readmit", [] { Rejection(false); }},
        {"stripe_collision_readmit", [] { Rejection(true); }},
        {"accepted_deadline_lease_rearms", [] { NeverStarted(true); }},
        {"cancelled_task_lease_rearms", [] { NeverStarted(false); }},
        {"stale_epoch_fence", GenerationFence},
        {"old_completion_does_not_delete_new", LateDestructor},
        {"disconnect_retire", Disconnect},
        {"capped_backoff_no_retry_abandonment", RepeatedFailure},
        {"fair_bounded_timer_batch", FairBudget},
        {"stop_before_executor_drain", StopFence},
        {"weak_owner_lifetime", OwnerLifetime},
        {"restart_preserves_generation_fence", RestartGeneration},
        {"concurrent_bookkeeping", ThreadSafety},
        {"unchanged_late_ack_semantics", ExistingAckSemantics}
    };
    for (const auto& [name, run] : tests) { run(); std::cout << "PASS " << name << '\n'; }
    std::cout << "R1_COMPONENT_PASS tests=" << tests.size()
              << " gateway_e2e=NOT_RUN mysql_convergence=NOT_RUN p1b_final=NOT_PASSED\n";
    Logger::Instance().Shutdown();
}
