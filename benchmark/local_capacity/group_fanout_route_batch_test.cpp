#include "gateway/GroupFanoutCoordinator.h"
#include "gateway/GroupFanoutPipeline.h"
#include "tests/concurrency/TestFramework.h"
#include <iostream>
#include <vector>
using namespace tinyimx;
namespace {
struct Fixture {
    int count=4,invalid=2,scalar=0,route_batches=0,completion_batches=0,single_completions=0;
    bool failed=false,empty=false,absent=false,bad_shape=false,zero_affected=false;
    std::vector<std::string> events;
    std::vector<rpc::CompleteGroupMessageDeliveryAttemptRpcRequest> completions;
    std::string token;
    GroupFanoutDispatchResult Dispatch(const rpc::GroupDeliveryWorkRpcRecord& w) {
        ++scalar;auto i=w.delivery.recipient_user_id-18446744073709550000ULL;
        events.push_back("d"+std::to_string(i));GroupFanoutDispatchResult r;r.gateway_id="route-target";
        r.status=i==0?GroupFanoutDispatchStatus::kSubmitted:i==1?GroupFanoutDispatchStatus::kOffline:GroupFanoutDispatchStatus::kRetryableFailure;
        return r;
    }
    GroupFanoutCoordinatorDependencies Deps() {
        GroupFanoutCoordinatorDependencies d;
        d.claim=[&](const auto&q,const auto&) {
            token=q.lease_token;if(failed)return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Failure(rpc::RpcErrorCode::kUnavailable,"own claim failed");
            rpc::ClaimGroupMessageDeliveriesRpcResponse r;
            if(!empty)for(int i=0;i<count;++i){rpc::GroupDeliveryWorkRpcRecord w;w.message.message_id=9007199254740993ULL;w.message.group_id=99;w.delivery.message_id=i==invalid?0:w.message.message_id;w.delivery.recipient_user_id=18446744073709550000ULL+i;r.work_items.push_back(w);}
            return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Success(r);
        };
        d.dispatch=[&](const auto&w){return Dispatch(w);};
        if(!absent)d.dispatch_batch=[&](const auto&items){++route_batches;events.push_back("route_batch");std::vector<GroupFanoutDispatchResult> r;for(auto&w:items)r.push_back(Dispatch(w));if(bad_shape&&!r.empty())r.pop_back();return r;};
        d.complete=[&](const auto&q,const auto&){++single_completions;events.push_back("c"+std::to_string(q.recipient_user_id-18446744073709550000ULL));completions.push_back(q);rpc::MessageMutationRpcResponse r;r.affected_rows=zero_affected?0:1;return rpc::MessageMutationRpcCallResult::Success(r);};
        d.complete_batch=[&](const auto&q,const auto&){++completion_batches;events.push_back("complete_batch");completions=q;rpc::MessageMutationRpcResponse r;r.affected_rows=zero_affected?0:q.size();return rpc::MessageMutationRpcCallResult::Success(r);};
        return d;
    }
    std::size_t Run(std::size_t limit=4){GroupFanoutCoordinatorOptions o;o.gateway_id="route-unit";o.batch_size=limit;GroupFanoutCoordinator c(Deps(),o);return c.RunOneIterationForTest();}
};
}
int main(int argc,char**argv){
 if(argc!=3)return 2;bool enabled=std::string(argv[1])=="1",defer=std::string(argv[2])=="1";
 if(GroupFanoutRouteBatchEnabled()!=enabled||GroupFanoutDeferCompletionEnabled()!=defer||!GroupFanoutCompletionBatchEnabled())return 3;
 bool use=enabled&&defer;test::TestRunner t;
 t.Add("RouteBatch.BoundedMixedKeepsIdentityOutcomeAndOrder",[&]{Fixture f;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));TINYIMX_EXPECT_EQ(f.route_batches,use?1:0);TINYIMX_EXPECT_EQ(f.scalar,3);TINYIMX_EXPECT_EQ(f.completions.size(),std::size_t(3));TINYIMX_EXPECT_EQ(f.completion_batches,defer?1:0);TINYIMX_EXPECT_EQ(f.single_completions,defer?0:3);
  auto wanted=defer?std::vector<std::string>{"d0","d1","d3","complete_batch"}:std::vector<std::string>{"d0","c0","d1","c1","d3","c3"};if(use)wanted.insert(wanted.begin(),"route_batch");TINYIMX_EXPECT_TRUE(f.events==wanted);
  for(auto&q:f.completions){TINYIMX_EXPECT_EQ(q.message_id,9007199254740993ULL);TINYIMX_EXPECT_EQ(q.lease_token,f.token);TINYIMX_EXPECT_EQ(q.gateway_id,std::string("route-target"));}
  TINYIMX_EXPECT_EQ(f.completions[0].retry_after_ms,std::uint32_t(3000));TINYIMX_EXPECT_EQ(f.completions[1].outcome,rpc::GroupDeliveryAttemptRpcOutcome::kOffline);TINYIMX_EXPECT_EQ(f.completions[2].retry_after_ms,std::uint32_t(1000));
 });
 t.Add("RouteBatch.MissingCallbackKeepsOriginal",[&]{Fixture f;f.absent=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));TINYIMX_EXPECT_EQ(f.route_batches,0);TINYIMX_EXPECT_EQ(f.scalar,3);TINYIMX_EXPECT_EQ(f.completions.size(),std::size_t(3));});
 t.Add("RouteBatch.OversizedClaimPreservesInterleaving",[&]{Fixture f;f.count=5;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(5));TINYIMX_EXPECT_EQ(f.route_batches,0);TINYIMX_EXPECT_EQ(f.completion_batches,0);TINYIMX_EXPECT_EQ(f.single_completions,4);TINYIMX_EXPECT_TRUE(f.events==std::vector<std::string>({"d0","c0","d1","c1","d3","c3","d4","c4"}));});
 t.Add("RouteBatch.EmptyNoDispatch",[&]{Fixture f;f.empty=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(0));TINYIMX_EXPECT_TRUE(f.events.empty());});
 t.Add("RouteBatch.FailedClaimNoDispatch",[&]{Fixture f;f.failed=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(0));TINYIMX_EXPECT_TRUE(f.events.empty());});
 t.Add("RouteBatch.AllInvalidNoBatch",[&]{Fixture f;f.count=1;f.invalid=0;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(1));TINYIMX_EXPECT_TRUE(f.events.empty());});
 t.Add("RouteBatch.Max256AboveInt64",[&]{Fixture f;f.count=256;f.invalid=-1;TINYIMX_EXPECT_EQ(f.Run(600),std::size_t(256));TINYIMX_EXPECT_EQ(f.route_batches,use?1:0);TINYIMX_EXPECT_EQ(f.scalar,256);TINYIMX_EXPECT_EQ(f.completions.size(),std::size_t(256));TINYIMX_EXPECT_EQ(f.completions.back().recipient_user_id,18446744073709550255ULL);});
 t.Add("RouteBatch.MalformedCallbackNeverReplaysSubmission",[&]{Fixture f;f.bad_shape=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));TINYIMX_EXPECT_EQ(f.scalar,3);TINYIMX_EXPECT_EQ(f.route_batches,use?1:0);TINYIMX_EXPECT_EQ(f.completions.size(),std::size_t(3));if(use)for(auto&q:f.completions){TINYIMX_EXPECT_EQ(q.outcome,rpc::GroupDeliveryAttemptRpcOutcome::kRetryableFailure);TINYIMX_EXPECT_EQ(q.error_code,std::string("batch_dispatch_result_mismatch"));}});
 t.Add("RouteBatch.ACKWinningZeroCompletion",[&]{Fixture f;f.zero_affected=true;TINYIMX_EXPECT_EQ(f.Run(),std::size_t(4));TINYIMX_EXPECT_EQ(f.route_batches,use?1:0);TINYIMX_EXPECT_EQ(f.completions.size(),std::size_t(3));});
 int fail=t.RunAll("Group route batch coordinator native");std::cout<<"{\"status\":\""<<(fail?"FAIL":"GROUP_ROUTE_COORDINATOR_PASS")<<"\",\"checks\":9,\"failures\":"<<fail<<"}\n";return fail?1:0;
}
