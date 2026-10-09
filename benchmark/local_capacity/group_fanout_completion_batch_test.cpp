#include "gateway/GroupFanoutCoordinator.h"
#include "gateway/GroupFanoutPipeline.h"
#include "tests/concurrency/TestFramework.h"
#include <iostream>
#include <vector>
using namespace tinyimx;
namespace {
struct Fixture {
    int count=4, invalid=3, single=0, batch=0;
    bool failed=false, uncertain=false, absent=false, all_ack=false, empty=false;
    std::string token;
    std::vector<std::string> events;
    std::vector<rpc::CompleteGroupMessageDeliveryAttemptRpcRequest> completed;
    GroupFanoutCoordinatorDependencies Deps() {
        GroupFanoutCoordinatorDependencies d;
        d.claim=[&](const auto& q,const auto&) {
            token=q.lease_token;
            if(failed) return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Failure(
                rpc::RpcErrorCode::kUnavailable,"claim failed");
            rpc::ClaimGroupMessageDeliveriesRpcResponse out;
            if(!empty) for(int i=0;i<count;++i) {
                rpc::GroupDeliveryWorkRpcRecord w;
                w.message.message_id=9007199254740993ULL;
                w.delivery.message_id=i==invalid?0:w.message.message_id;
                w.delivery.recipient_user_id=18446744073709550000ULL+i;
                out.work_items.push_back(w);
            }
            return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Success(out);
        };
        d.dispatch=[&](const auto& w) {
            auto i=w.delivery.recipient_user_id-18446744073709550000ULL;
            events.push_back("d"+std::to_string(i));
            GroupFanoutDispatchResult out;out.gateway_id="target'\\bytes";
            out.status=i==0?GroupFanoutDispatchStatus::kSubmitted:
                i==1?GroupFanoutDispatchStatus::kOffline:
                i==2?GroupFanoutDispatchStatus::kRetryableFailure:GroupFanoutDispatchStatus::kAlreadyDelivered;
            return out;
        };
        const auto result=[&] {
            if(uncertain)return rpc::MessageMutationRpcCallResult::Failure(
                rpc::RpcErrorCode::kUnavailable,"committed-or-not",true);
            rpc::MessageMutationRpcResponse out;out.affected_rows=all_ack?0:1;
            return rpc::MessageMutationRpcCallResult::Success(out);
        };
        d.complete=[&,result](const auto& q,const auto&) {
            ++single;events.push_back("c"+std::to_string(q.recipient_user_id-18446744073709550000ULL));
            completed.push_back(q);return result();
        };
        if(!absent)d.complete_batch=[&,result](const auto& q,const auto& options) {
            ++batch;events.push_back("batch");
            TINYIMX_EXPECT_EQ(options.remaining_timeout,std::chrono::milliseconds(2500));
            TINYIMX_EXPECT_EQ(options.caller_instance,std::string("complete-unit"));
            completed=q;return result();
        };
        return d;
    }
    std::size_t Run(std::size_t limit=4) {
        GroupFanoutCoordinatorOptions o;o.gateway_id="complete-unit";o.batch_size=limit;
        GroupFanoutCoordinator c(Deps(),o);return c.RunOneIterationForTest();
    }
};
}
int main(int argc,char**argv) {
    if(argc!=3)return 2;
    bool enabled=std::string(argv[1])=="1",defer=std::string(argv[2])=="1";
    if(GroupFanoutCompletionBatchEnabled()!=enabled||GroupFanoutDeferCompletionEnabled()!=defer)return 3;
    bool use=enabled&&defer;
    test::TestRunner t;
    t.Add("Completion.MixedDispatchesAndPhysicalCalls",[&] {
        Fixture f;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));
        auto wanted=use?std::vector<std::string>{"d0","d1","d2","batch"}:
            defer?std::vector<std::string>{"d0","d1","d2","c0","c1","c2"}:
                  std::vector<std::string>{"d0","c0","d1","c1","d2","c2"};
        TINYIMX_EXPECT_TRUE(f.events==wanted);
        TINYIMX_EXPECT_EQ(f.batch,use?1:0);TINYIMX_EXPECT_EQ(f.single,use?0:3);
        TINYIMX_EXPECT_EQ(f.completed.size(),std::size_t(3));
        for(const auto& q:f.completed) {
            TINYIMX_EXPECT_EQ(q.message_id,9007199254740993ULL);
            TINYIMX_EXPECT_EQ(q.lease_token,f.token);
            TINYIMX_EXPECT_EQ(q.gateway_id,std::string("target'\\bytes"));
        }
        TINYIMX_EXPECT_EQ(f.completed[0].retry_after_ms,std::uint32_t(3000));
        TINYIMX_EXPECT_EQ(f.completed[1].outcome,rpc::GroupDeliveryAttemptRpcOutcome::kOffline);
        TINYIMX_EXPECT_EQ(f.completed[1].retry_after_ms,std::uint32_t(0));
        TINYIMX_EXPECT_EQ(f.completed[1].error_code,std::string("recipient_offline"));
        TINYIMX_EXPECT_EQ(f.completed[2].retry_after_ms,std::uint32_t(1000));
        TINYIMX_EXPECT_EQ(f.completed[2].error_code,std::string("delivery_retryable_failure"));
    });
    t.Add("Completion.UncertainBatchNeverFallsBackOrReplays",[&] {
        Fixture f;f.uncertain=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));
        TINYIMX_EXPECT_EQ(f.batch,use?1:0);TINYIMX_EXPECT_EQ(f.single,use?0:3);
        TINYIMX_EXPECT_EQ(f.completed.size(),std::size_t(3));
    });
    t.Add("Completion.ACKZeroAffectedIsSuccessful",[&] {
        Fixture f;f.all_ack=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));
        TINYIMX_EXPECT_EQ(f.batch,use?1:0);TINYIMX_EXPECT_EQ(f.single,use?0:3);
    });
    t.Add("Completion.NoWorkNoRPC",[&] {
        Fixture f;f.empty=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(0));
        TINYIMX_EXPECT_TRUE(f.events.empty()&&f.batch==0&&f.single==0);
    });
    t.Add("Completion.FailedClaimNoRPC",[&] {
        Fixture f;f.failed=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(0));
        TINYIMX_EXPECT_TRUE(f.events.empty()&&f.batch==0&&f.single==0);
    });
    t.Add("Completion.AllInvalidNoRPC",[&] {
        Fixture f;f.count=1;f.invalid=0;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(1));
        TINYIMX_EXPECT_TRUE(f.events.empty()&&f.batch==0&&f.single==0);
    });
    t.Add("Completion.OversizedOriginalInterleavedFallback",[&] {
        Fixture f;f.count=5;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(5));
        TINYIMX_EXPECT_EQ(f.batch,0);TINYIMX_EXPECT_EQ(f.single,4);
        TINYIMX_EXPECT_TRUE(f.events==std::vector<std::string>(
            {"d0","c0","d1","c1","d2","c2","d4","c4"}));
    });
    t.Add("Completion.MissingCallbackKeepsSingleAPI",[&] {
        Fixture f;f.absent=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));
        TINYIMX_EXPECT_EQ(f.batch,0);TINYIMX_EXPECT_EQ(f.single,3);
    });
    t.Add("Completion.Maximum256AndAlreadyDeliveredOutcome",[&] {
        Fixture f;f.count=256;f.invalid=-1;TINYIMX_EXPECT_EQ(f.Run(500),std::size_t(256));
        TINYIMX_EXPECT_EQ(f.batch,use?1:0);TINYIMX_EXPECT_EQ(f.single,use?0:256);
        TINYIMX_EXPECT_EQ(f.completed.size(),std::size_t(256));
        TINYIMX_EXPECT_EQ(f.completed.back().outcome,rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted);
    });
    int fail=t.RunAll("Group completion batch native");
    std::cout<<"{\"status\":\""<<(fail?"FAIL":"GROUP_COMPLETION_COORDINATOR_PASS")
        <<"\",\"checks\":9,\"failures\":"<<fail<<",\"enabled\":"<<(enabled?"true":"false")
        <<",\"defer\":"<<(defer?"true":"false")<<"}\n";
    return fail?1:0;
}
