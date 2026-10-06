#include "gateway/GroupFanoutCoordinator.h"
#include "gateway/GroupFanoutWakeup.h"
#include "tests/concurrency/TestFramework.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
using namespace std::chrono_literals;
namespace tinyimx::test { void RegisterGroupFanoutCoordinatorTests(TestRunner&); }
namespace {
struct Probe {
    std::mutex mutex;
    std::condition_variable cv;
    int claims{0};
    int completed{0};
    bool hold_first{false};
    bool release{false};
    bool full_first_five{false};
    tinyimx::GroupFanoutCoordinatorDependencies Dependencies() {
        tinyimx::GroupFanoutCoordinatorDependencies d;
        d.claim = [this](const tinyimx::rpc::ClaimGroupMessageDeliveriesRpcRequest& q,
                        const tinyimx::rpc::RpcCallOptions&) {
            TINYIMX_EXPECT_EQ(q.lease_owner, std::string("wake-unit"));
            TINYIMX_EXPECT_EQ(q.lease_ms, static_cast<std::uint32_t>(5000));
            TINYIMX_EXPECT_TRUE(!q.lease_token.empty());
            std::unique_lock<std::mutex> lock(mutex);
            const int n = ++claims; cv.notify_all();
            if (hold_first && n == 1) cv.wait_for(lock, 5s, [&] { return release; });
            tinyimx::rpc::ClaimGroupMessageDeliveriesRpcResponse response;
            if (full_first_five && n <= 5) {
                tinyimx::rpc::GroupDeliveryWorkRpcRecord item;
                item.message.message_id = static_cast<std::uint64_t>(n);
                item.message.group_id = 901;
                item.message.from_user_id = 701;
                item.delivery.message_id = item.message.message_id;
                item.delivery.group_id = item.message.group_id;
                item.delivery.recipient_user_id = 702;
                item.delivery.delivery_state = tinyimx::rpc::GroupDeliveryRpcState::kPending;
                response.work_items.push_back(item);
            }
            return tinyimx::rpc::RpcResult<tinyimx::rpc::ClaimGroupMessageDeliveriesRpcResponse>::Success(std::move(response));
        };
        d.dispatch = [](const tinyimx::rpc::GroupDeliveryWorkRpcRecord&) {
            tinyimx::GroupFanoutDispatchResult result;
            result.status = tinyimx::GroupFanoutDispatchStatus::kSubmitted;
            result.gateway_id = "wake-unit";
            return result;
        };
        d.complete = [this](const tinyimx::rpc::CompleteGroupMessageDeliveryAttemptRpcRequest& q,
                           const tinyimx::rpc::RpcCallOptions&) {
            TINYIMX_EXPECT_EQ(q.recipient_user_id, static_cast<std::uint64_t>(702));
            TINYIMX_EXPECT_TRUE(!q.lease_token.empty());
            TINYIMX_EXPECT_EQ(q.outcome, tinyimx::rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted);
            { std::lock_guard<std::mutex> lock(mutex); ++completed; cv.notify_all(); }
            tinyimx::rpc::MessageMutationRpcResponse response;
            response.affected_rows = 1;
            return tinyimx::rpc::MessageMutationRpcCallResult::Success(response);
        };
        return d;
    }
    bool Wait(int n, std::chrono::milliseconds duration=3s) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock,duration,[&]{return claims>=n;});
    }
    int Count() { std::lock_guard<std::mutex> lock(mutex); return claims; }
    void Release() { {std::lock_guard<std::mutex> lock(mutex); release=true;} cv.notify_all(); }
};
tinyimx::GroupFanoutCoordinatorOptions Options(std::chrono::milliseconds interval=10s) {
    tinyimx::GroupFanoutCoordinatorOptions o;
    o.gateway_id="wake-unit"; o.batch_size=1; o.recovery_interval=interval;
    return o;
}
void CheckStop(tinyimx::GroupFanoutCoordinator& c) {
    const auto start=std::chrono::steady_clock::now(); c.Stop();
    TINYIMX_EXPECT_TRUE(std::chrono::steady_clock::now()-start<3s);
    TINYIMX_EXPECT_TRUE(!c.IsRunning());
}
}
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    const bool enabled=std::string(argv[1])=="1";
    if (tinyimx::GroupFanoutCommitWakeEnabled()!=enabled) return 3;
    tinyimx::test::TestRunner runner;
    tinyimx::test::RegisterGroupFanoutCoordinatorTests(runner);
    runner.Add("Wake.SleepingWorkerAndPromptStop",[&] {
        Probe p; tinyimx::GroupFanoutCoordinator c(p.Dependencies(),Options());
        TINYIMX_EXPECT_TRUE(c.Start()); TINYIMX_EXPECT_TRUE(p.Wait(1));
        tinyimx::NotifyGroupFanoutCommitted();
        const bool woke=p.Wait(2,enabled?3s:200ms);
        CheckStop(c); TINYIMX_EXPECT_EQ(woke,enabled);
    });
    runner.Add("Wake.CommitDuringClaimIsNotLostAndHintsCoalesce",[&] {
        Probe p; p.hold_first=true;
        tinyimx::GroupFanoutCoordinator c(p.Dependencies(),Options());
        TINYIMX_EXPECT_TRUE(c.Start()); const bool entered=p.Wait(1);
        for(int i=0;i<2000;++i) tinyimx::NotifyGroupFanoutCommitted();
        p.Release(); const bool woke=p.Wait(2,enabled?3s:200ms);
        std::this_thread::sleep_for(80ms); const int count=p.Count(); CheckStop(c);
        TINYIMX_EXPECT_TRUE(entered); TINYIMX_EXPECT_EQ(woke,enabled);
        TINYIMX_EXPECT_EQ(count,enabled?2:1);
    });
    runner.Add("Wake.RecoveryPollWithoutNotificationIsPreserved",[&] {
        Probe p; tinyimx::GroupFanoutCoordinator c(p.Dependencies(),Options(100ms));
        TINYIMX_EXPECT_TRUE(c.Start()); const bool recovered=p.Wait(2);
        CheckStop(c); TINYIMX_EXPECT_TRUE(recovered);
    });
    runner.Add("Wake.FullBatchesDrainThroughOriginalLeaseCompletion",[&] {
        Probe p; p.full_first_five=true;
        tinyimx::GroupFanoutCoordinator c(p.Dependencies(),Options());
        TINYIMX_EXPECT_TRUE(c.Start()); TINYIMX_EXPECT_TRUE(p.Wait(1));
        const bool drained=p.Wait(6,enabled?3s:200ms); CheckStop(c);
        TINYIMX_EXPECT_EQ(drained,enabled);
        {std::lock_guard<std::mutex> lock(p.mutex);TINYIMX_EXPECT_EQ(p.completed,enabled?5:1);}
    });
    const int failures=runner.RunAll("Group commit wake native");
    std::cout << "{\"status\":\"" << (failures?"FAIL":"GROUP_FANOUT_WAKE_NATIVE_PASS")
              << "\",\"enabled\":" << (enabled?"true":"false")
              << ",\"tests\":6,\"failures\":" << failures << "}\n";
    return failures==0?0:1;
}
