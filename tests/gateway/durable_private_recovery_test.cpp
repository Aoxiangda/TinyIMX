#include "gateway/DurablePrivateRecovery.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

namespace {
int failed = 0;
void Expect(bool value, const char* label) {
    std::cout << (value ? "[PASS] " : "[FAIL] ") << label << '\n';
    if (!value) ++failed;
}
}
int main() {
    using Recovery = tinyimx::DurablePrivateRecovery;
    using namespace std::chrono_literals;
    const auto now = Recovery::Clock::now();
    Recovery recovery;
    Expect(!recovery.AcquireQuery(now) && !recovery.BeginReplay(1, 1), "Recovery.StartsStopped");
    recovery.Resume();
    auto query = recovery.AcquireQuery(now);
    Expect(query && query->Cursor() == 0 && !recovery.AcquireQuery(now), "Recovery.DiscoverySingleFlight");
    std::vector<std::uint64_t> ids;
    for (std::uint64_t id = 1; id <= 256; ++id) ids.push_back(id);
    Expect(!query->Complete({1}, true, now) && !query->Complete({2, 1}, false, now) &&
               !query->Complete({0}, false, now) && !query->Complete({1, 1}, false, now),
           "Recovery.InvalidPageKeepsResponsibility");
    Expect(query->Complete(ids, true, now), "Recovery.ValidFullPageAccepted");
    Expect(!recovery.AcquireQuery(now + 1s), "Recovery.QueueBackpressuresDiscovery");
    std::vector<std::uint64_t> collected;
    for (int i = 0; i < 8; ++i) {
        auto part = recovery.TakeRecipients(10000);
        Expect(part.size() == 32, "Recovery.DispatchBudgetIsBounded");
        collected.insert(collected.end(), part.begin(), part.end());
    }
    Expect(collected == ids, "Recovery.BoundedDispatchPreservesEveryRecipient");
    query = recovery.AcquireQuery(now + 1s);
    Expect(query && query->Cursor() == 256 && !query->Complete({256}, false, now + 1s),
           "Recovery.CursorAdvancesOnlyPastLastReturnedRecipient");
    query->Fail(now + 1s);
    Expect(!recovery.AcquireQuery(now + 1100ms), "Recovery.FailureUsesBackoff");
    query = recovery.AcquireQuery(now + 1600ms);
    Expect(query && query->Cursor() == 256, "Recovery.QueryFailureRetainsCursor");
    Expect(query->Complete({}, false, now + 1600ms), "Recovery.EndOfScanWraps");
    query = recovery.AcquireQuery(now + 1800ms);
    Expect(query && query->Cursor() == 0 && query->Complete({1}, false, now + 1800ms),
           "Recovery.NextCycleCanObserveLateCommitBeforeOldCursor");
    recovery.TakeRecipients();
    query = recovery.AcquireQuery(now + 2s);
    recovery.Stop();
    recovery.Resume();
    auto fresh = recovery.AcquireQuery(now + 2s);
    Expect(fresh && !query->Complete({5}, false, now + 2s), "Recovery.StopResumeFencesOldDiscovery");
    query.reset();
    Expect(!recovery.AcquireQuery(now + 3s), "Recovery.OldLeaseCannotReleaseFreshQuery");
    fresh->Fail(now + 3s);
    fresh.reset();

    auto replay = recovery.BeginReplay(1, 1);
    Expect(replay && replay->Cursor() == 0 && !recovery.BeginReplay(1, 1), "Recovery.OneReplayPerSession");
    Expect(!replay->Complete(0, true) && replay->Complete(100, true), "Recovery.ReplayCursorNeedsProgress");
    replay = recovery.BeginReplay(1, 1);
    Expect(replay && replay->Cursor() == 100, "Recovery.HotRecipientResumesAfterOneBoundedPage");
    replay.reset();
    replay = recovery.BeginReplay(1, 1);
    Expect(replay && replay->Cursor() == 100, "Recovery.AbandonedReplayDoesNotSkipData");
    auto replacement = recovery.BeginReplay(1, 2);
    Expect(replacement && replacement->Cursor() == 0 && !replay->Complete(900, true) &&
               !recovery.BeginReplay(1, 1), "Recovery.NewEpochFencesOldCursorAndWorkers");
    replay.reset();
    recovery.Retire(1, 1);
    Expect(!recovery.BeginReplay(1, 2), "Recovery.StaleDisconnectCannotRetireNewEpoch");
    Expect(replacement->Complete(1, false), "Recovery.NewEpochCompletionRemainsValid");
    replacement = recovery.BeginReplay(1, 2);
    Expect(replacement && replacement->Cursor() == 0, "Recovery.ReplayTerminalPageWrapsForUnconfirmedRows");
    recovery.Retire(1, 2);
    Expect(recovery.SessionStates() == 0 && !replacement->Complete(9, true), "Recovery.DisconnectFencesInFlightPage");

    Recovery::Options options;
    options.max_session_states = 2;
    options.max_active_replays = 1;
    Recovery bounded(options);
    bounded.Resume();
    auto first = bounded.BeginReplay(1, 1);
    Expect(first && !bounded.BeginReplay(2, 2), "Recovery.GlobalReplayConcurrencyBounded");
    first.reset();
    auto second = bounded.BeginReplay(2, 2);
    second.reset();
    Expect(bounded.SessionStates() == 2 && !bounded.BeginReplay(3, 3), "Recovery.CursorStateMemoryBounded");
    bounded.Retire(1, 1);
    Expect(static_cast<bool>(bounded.BeginReplay(3, 3)), "Recovery.RetiredSessionReleasesStateCapacity");

    Recovery concurrent;
    concurrent.Resume();
    std::atomic<int> admitted{0};
    Recovery::QueryAttemptPtr held;
    std::mutex held_mutex;
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) workers.emplace_back([&] {
        auto attempt = concurrent.AcquireQuery(now);
        if (attempt) {
            ++admitted;
            std::lock_guard lock(held_mutex);
            held = std::move(attempt);
        }
    });
    for (auto& worker : workers) worker.join();
    Expect(admitted.load() == 1 && held, "Recovery.ConcurrentDiscoveryHasOneOwner");
    held.reset();
    Expect(!concurrent.AcquireQuery(Recovery::Clock::now()) &&
               static_cast<bool>(concurrent.AcquireQuery(Recovery::Clock::now() + 1s)),
           "Recovery.UnstartedRejectedTaskLeaseRearmsWithBackoff");
    recovery.Stop();
    Expect(!recovery.BeginReplay(1, 4) && recovery.TakeRecipients().empty(), "Recovery.StopRejectsNewWork");
    std::cout << "failed=" << failed << " ACTUAL_GATEWAY_RECOVERY=NOT_RUN\n";
    return failed ? 1 : 0;
}
