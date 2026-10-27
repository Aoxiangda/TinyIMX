#include "gateway/business/BusinessExecutor.h"
#include "gateway/PrivateReceiverAck.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace tinyimx;
using namespace std::chrono_literals;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)

namespace {
BusinessExecutorOptions Options(const char* name, std::size_t pending) {
    BusinessExecutorOptions x;
    x.name = name; x.worker_threads = 1; x.max_pending_tasks = pending;
    x.stripe_count = 2; x.per_stripe_queue_capacity = pending;
    return x;
}

struct Fixture {
    std::promise<void> release, started;
    std::shared_future<void> gate{release.get_future().share()};
    BusinessExecutor message{Options("saturated-message", 1)};
    BusinessExecutor ack{Options("receiver-ack", 2)};
    struct ReleaseOnUnwind {
        std::promise<void>& promise;
        ~ReleaseOnUnwind() { try { promise.set_value(); } catch (const std::future_error&) {} }
    } release_on_unwind{release};
    Fixture() {
        REQUIRE(message.Start()); REQUIRE(ack.Start());
        auto ready = started.get_future();
        BusinessExecutor::TaskSpec task;
        task.request.operation = "blocked-sender";
        task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        task.work = [this](const auto&) { started.set_value(); gate.wait(); return BusinessExecutor::Completion{}; };
        REQUIRE(message.Submit(std::move(task)) == BusinessSubmitStatus::kAccepted);
        REQUIRE(ready.wait_for(1s) == std::future_status::ready);
    }
    void Release() { try { release.set_value(); } catch (const std::future_error&) {} }
    ~Fixture() { Release(); message.ShutdownGraceful(); ack.ShutdownGraceful(); }
};

BusinessExecutor::TaskSpec AckTask(ReceiverDeliveryTracker& tracker,
    std::uint64_t receiver, std::atomic<int>& storage_calls,
    std::promise<PrivateReceiverAckResult>& done) {
    BusinessExecutor::TaskSpec task;
    task.request.operation = "gateway.receiver_ack";
    task.request.ordering_key = 100;
    task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
    task.work = [&tracker, receiver, &storage_calls, &done](const auto&) {
        done.set_value(ProcessPrivateReceiverAck(tracker, 100, receiver, 9,
            [&storage_calls] { ++storage_calls; return true; }));
        return BusinessExecutor::Completion{};
    };
    return task;
}

void SaturatedSenderDoesNotStarveAck() {
    ReceiverDeliveryTracker tracker;
    REQUIRE(tracker.RegisterAttempt(100, 42, 9) == ReceiverDeliveryRegisterStatus::kRegistered);
    std::atomic<int> calls{0}; std::promise<PrivateReceiverAckResult> done;
    auto result = done.get_future(); Fixture f;
    REQUIRE(f.message.Submit(AckTask(tracker, 42, calls, done)) == BusinessSubmitStatus::kOverloaded);
    REQUIRE(calls == 0);
    REQUIRE(f.ack.Submit(AckTask(tracker, 42, calls, done)) == BusinessSubmitStatus::kAccepted);
    REQUIRE(result.wait_for(1s) == std::future_status::ready);
    REQUIRE(result.get().durable_confirmed && calls == 1);
    REQUIRE(f.message.GetStats().current_pending_tasks == 1);
}

void ForeignReceiverStillCannotConfirm() {
    ReceiverDeliveryTracker tracker;
    REQUIRE(tracker.RegisterAttempt(100, 42, 9) == ReceiverDeliveryRegisterStatus::kRegistered);
    std::atomic<int> calls{0}; std::promise<PrivateReceiverAckResult> done;
    auto result = done.get_future(); Fixture f;
    REQUIRE(f.ack.Submit(AckTask(tracker, 43, calls, done)) == BusinessSubmitStatus::kAccepted);
    REQUIRE(result.wait_for(1s) == std::future_status::ready);
    REQUIRE(result.get().status == ReceiverDeliveryAckStatus::kReceiverMismatch && calls == 0);
    ReceiverDeliverySnapshot s; REQUIRE(tracker.GetSnapshot(100, &s) && !s.confirmed);
}

void AckLaneIsBoundedAndDrains() {
    Fixture f; std::promise<void> started; auto ready = started.get_future();
    for (int i = 0; i != 2; ++i) {
        BusinessExecutor::TaskSpec task; task.request.operation = "blocked-ack";
        task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
        task.work = [&, i](const auto&) { if (i == 0) started.set_value(); f.gate.wait(); return BusinessExecutor::Completion{}; };
        REQUIRE(f.ack.Submit(std::move(task)) == BusinessSubmitStatus::kAccepted);
        if (i == 0) REQUIRE(ready.wait_for(1s) == std::future_status::ready);
    }
    BusinessExecutor::TaskSpec extra; extra.work = [](const auto&) { return BusinessExecutor::Completion{}; };
    REQUIRE(f.ack.Submit(std::move(extra)) == BusinessSubmitStatus::kOverloaded);
    REQUIRE(f.ack.GetStats().current_pending_tasks == 2);
    f.Release(); REQUIRE(f.ack.ShutdownGraceful());
    const auto stats = f.ack.GetStats();
    REQUIRE(stats.accepted_total == 2 && stats.completed_total == 2);
    REQUIRE(stats.current_pending_tasks == 0 && stats.peak_pending_tasks == 2);
}

void AcceptedMustRunAckSurvivesCancellationAndDrains() {
    ReceiverDeliveryTracker tracker;
    REQUIRE(tracker.RegisterAttempt(100, 42, 9) == ReceiverDeliveryRegisterStatus::kRegistered);
    std::atomic<int> calls{0}; std::promise<PrivateReceiverAckResult> done;
    auto result = done.get_future(); Fixture f;
    auto task = AckTask(tracker, 42, calls, done); task.still_valid = [] { return false; };
    REQUIRE(f.ack.Submit(std::move(task)) == BusinessSubmitStatus::kAccepted);
    REQUIRE(f.ack.ShutdownGraceful());
    REQUIRE(result.wait_for(1s) == std::future_status::ready && result.get().durable_confirmed);
    REQUIRE(calls == 1 && f.ack.GetStats().completed_total == 1);
}
} // namespace

int main() {
    int failed = 0;
    auto run = [&](const char* name, auto test) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "[FAIL] " << name << ": " << e.what() << '\n'; }
    };
    run("saturated_sender_does_not_starve_ack", SaturatedSenderDoesNotStarveAck);
    run("foreign_receiver_still_cannot_confirm", ForeignReceiverStillCannotConfirm);
    run("ack_lane_bounded_and_drains", AckLaneIsBoundedAndDrains);
    run("accepted_must_run_ack_survives_cancellation", AcceptedMustRunAckSurvivesCancellationAndDrains);
    return failed ? 1 : 0;
}
