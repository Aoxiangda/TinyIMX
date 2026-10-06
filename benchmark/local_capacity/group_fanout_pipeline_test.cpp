#include "gateway/GroupFanoutCoordinator.h"
#include "gateway/GroupFanoutPipeline.h"
#include "tests/concurrency/TestFramework.h"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
namespace tinyimx::test { void RegisterGroupFanoutCoordinatorTests(TestRunner&); }
using namespace tinyimx;
namespace {
struct Fixture {
    bool fail_claim{false}, uncertain{false}, oversized{false};
    std::string token;
    std::vector<std::string> events;
    std::vector<rpc::CompleteGroupMessageDeliveryAttemptRpcRequest> completed;
    GroupFanoutCoordinatorDependencies Dependencies() {
        GroupFanoutCoordinatorDependencies d;
        d.claim = [&](const rpc::ClaimGroupMessageDeliveriesRpcRequest& q,
                      const rpc::RpcCallOptions& o) {
            TINYIMX_EXPECT_EQ(q.lease_owner,std::string("pipe-unit"));
            TINYIMX_EXPECT_EQ(q.limit,static_cast<std::uint32_t>(4));
            TINYIMX_EXPECT_EQ(q.lease_ms,static_cast<std::uint32_t>(5000));
            TINYIMX_EXPECT_EQ(o.remaining_timeout,std::chrono::milliseconds(2500));
            token=q.lease_token; TINYIMX_EXPECT_TRUE(!token.empty());
            if (fail_claim) return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Failure(
                rpc::RpcErrorCode::kUnavailable,"claim uncertain");
            rpc::ClaimGroupMessageDeliveriesRpcResponse response;
            for (std::uint64_t uid=10;uid<(oversized?15:14);++uid) {
                rpc::GroupDeliveryWorkRpcRecord w;
                w.message.message_id=7001;w.delivery.message_id=uid==13?7002:7001;
                w.delivery.recipient_user_id=uid;response.work_items.push_back(w);
            }
            return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Success(response);
        };
        d.dispatch = [&](const rpc::GroupDeliveryWorkRpcRecord& w) {
            const auto uid=w.delivery.recipient_user_id;events.push_back("d"+std::to_string(uid));
            GroupFanoutDispatchResult result;result.gateway_id="pipe-target";
            if (uid==10) result.status=GroupFanoutDispatchStatus::kSubmitted;
            else if (uid==11) result.status=GroupFanoutDispatchStatus::kOffline;
            else if (uid==12) result.status=GroupFanoutDispatchStatus::kRetryableFailure;
            else result.status=GroupFanoutDispatchStatus::kAlreadyDelivered;
            return result;
        };
        d.complete = [&](const rpc::CompleteGroupMessageDeliveryAttemptRpcRequest& q,
                         const rpc::RpcCallOptions& o) {
            events.push_back("c"+std::to_string(q.recipient_user_id));completed.push_back(q);
            TINYIMX_EXPECT_EQ(q.lease_token,token);
            TINYIMX_EXPECT_EQ(q.message_id,static_cast<std::uint64_t>(7001));
            TINYIMX_EXPECT_EQ(q.gateway_id,std::string("pipe-target"));
            TINYIMX_EXPECT_EQ(o.remaining_timeout,std::chrono::milliseconds(2500));
            TINYIMX_EXPECT_EQ(o.caller_instance,std::string("pipe-unit"));
            if (uncertain && q.recipient_user_id==10) return rpc::MessageMutationRpcCallResult::Failure(
                rpc::RpcErrorCode::kDeadlineExceeded,"uncertain completion",true);
            rpc::MessageMutationRpcResponse response;response.affected_rows=q.recipient_user_id==11?0:1;
            return rpc::MessageMutationRpcCallResult::Success(response);
        };
        return d;
    }
    std::size_t Iterate() {
        GroupFanoutCoordinatorOptions o;o.gateway_id="pipe-unit";o.batch_size=4;
        GroupFanoutCoordinator c(Dependencies(),o);return c.RunOneIterationForTest();
    }
};
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    const bool enabled=std::string(argv[1])=="1";
    if(GroupFanoutDeferCompletionEnabled()!=enabled)return 3;
    tinyimx::test::TestRunner runner;tinyimx::test::RegisterGroupFanoutCoordinatorTests(runner);
    runner.Add("Pipeline.BoundedMixedOutcomesInvalidWorkAndOriginalFences",[&] {
        Fixture f;TINYIMX_EXPECT_EQ(f.Iterate(),static_cast<std::size_t>(4));
        const std::vector<std::string> wanted=enabled
            ?std::vector<std::string>{"d10","d11","d12","c10","c11","c12"}
            :std::vector<std::string>{"d10","c10","d11","c11","d12","c12"};
        TINYIMX_EXPECT_TRUE(f.events==wanted);TINYIMX_EXPECT_EQ(f.completed.size(),static_cast<std::size_t>(3));
        TINYIMX_EXPECT_EQ(f.completed[0].outcome,rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted);
        TINYIMX_EXPECT_EQ(f.completed[0].retry_after_ms,static_cast<std::uint32_t>(3000));
        TINYIMX_EXPECT_EQ(f.completed[1].outcome,rpc::GroupDeliveryAttemptRpcOutcome::kOffline);
        TINYIMX_EXPECT_EQ(f.completed[1].retry_after_ms,static_cast<std::uint32_t>(0));
        TINYIMX_EXPECT_EQ(f.completed[1].error_code,std::string("recipient_offline"));
        TINYIMX_EXPECT_EQ(f.completed[2].outcome,rpc::GroupDeliveryAttemptRpcOutcome::kRetryableFailure);
        TINYIMX_EXPECT_EQ(f.completed[2].retry_after_ms,static_cast<std::uint32_t>(1000));
        TINYIMX_EXPECT_EQ(f.completed[2].error_code,std::string("delivery_retryable_failure"));
    });
    runner.Add("Pipeline.UncertainCompletionAndAckWinnerPreserveRemainingWork",[&] {
        Fixture f;f.uncertain=true;TINYIMX_EXPECT_EQ(f.Iterate(),static_cast<std::size_t>(4));
        TINYIMX_EXPECT_EQ(f.completed.size(),static_cast<std::size_t>(3));
        TINYIMX_EXPECT_EQ(f.completed.back().recipient_user_id,static_cast<std::uint64_t>(12));
    });
    runner.Add("Pipeline.FailedClaimDoesNotDispatchOrComplete",[&] {
        Fixture f;f.fail_claim=true;TINYIMX_EXPECT_EQ(f.Iterate(),static_cast<std::size_t>(0));
        TINYIMX_EXPECT_TRUE(f.events.empty()&&f.completed.empty());
    });
    runner.Add("Pipeline.OversizedResponsePreservesOriginalBoundedFallback",[&] {
        Fixture f;f.oversized=true;TINYIMX_EXPECT_EQ(f.Iterate(),static_cast<std::size_t>(5));
        TINYIMX_EXPECT_TRUE(f.events==std::vector<std::string>(
            {"d10","c10","d11","c11","d12","c12","d14","c14"}));
        TINYIMX_EXPECT_EQ(f.completed.size(),static_cast<std::size_t>(4));
        TINYIMX_EXPECT_EQ(f.completed.back().outcome,rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted);
    });
    const int failures=runner.RunAll("Group dispatch pipeline native");
    std::cout<<"{\"status\":\""<<(failures?"FAIL":"GROUP_FANOUT_PIPELINE_NATIVE_PASS")
             <<"\",\"enabled\":"<<(enabled?"true":"false")
             <<",\"tests\":6,\"failures\":"<<failures<<"}\n";
    return failures==0?0:1;
}
