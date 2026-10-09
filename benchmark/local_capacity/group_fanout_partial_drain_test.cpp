#include "gateway/GroupFanoutCoordinator.h"
#include "gateway/GroupFanoutPipeline.h"
#include "gateway/GroupFanoutWakeup.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
using namespace tinyimx;
using namespace std::chrono_literals;
namespace {
struct Probe {
    enum class Mode { Split49And15, FivePartial, Empty, Failed };
    Mode mode; std::mutex mutex;std::condition_variable cv;
    int claims=0,completed=0;bool valid=true;
    std::vector<std::chrono::steady_clock::time_point> times;
    explicit Probe(Mode value):mode(value){}
    GroupFanoutCoordinatorDependencies Dependencies() {
        GroupFanoutCoordinatorDependencies d;
        d.claim=[this](const rpc::ClaimGroupMessageDeliveriesRpcRequest& q,const rpc::RpcCallOptions& options) {
            int n;
            {std::lock_guard lock(mutex);n=++claims;times.push_back(std::chrono::steady_clock::now());
             valid=valid&&q.limit==64&&q.lease_ms==5000&&q.message_id==0&&q.lease_owner=="partial-native"&&
                   !q.lease_token.empty()&&options.remaining_timeout==2500ms;cv.notify_all();}
            if(mode==Mode::Failed)return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Failure(
                rpc::RpcErrorCode::kUnavailable,"OWN_UNCERTAIN_CLAIM");
            rpc::ClaimGroupMessageDeliveriesRpcResponse response;
            int count=mode==Mode::Split49And15?(n==1?49:n==2?15:0):mode==Mode::FivePartial&&n<=5?1:0;
            for(int i=0;i<count;++i) {
                rpc::GroupDeliveryWorkRpcRecord w;w.message.message_id=1304;w.message.group_id=901;w.message.from_user_id=701;
                w.delivery.message_id=1304;w.delivery.group_id=901;w.delivery.recipient_user_id=70000+n*1000+i;
                w.delivery.delivery_state=rpc::GroupDeliveryRpcState::kPending;response.work_items.push_back(w);
            }
            return rpc::RpcResult<rpc::ClaimGroupMessageDeliveriesRpcResponse>::Success(std::move(response));
        };
        d.dispatch=[](const rpc::GroupDeliveryWorkRpcRecord&) {
            GroupFanoutDispatchResult result;result.status=GroupFanoutDispatchStatus::kSubmitted;
            result.gateway_id="partial-native";return result;
        };
        d.complete=[this](const rpc::CompleteGroupMessageDeliveryAttemptRpcRequest& q,const rpc::RpcCallOptions&) {
            {std::lock_guard lock(mutex);++completed;
             valid=valid&&q.message_id==1304&&q.recipient_user_id>=71000&&!q.lease_token.empty()&&
               q.outcome==rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted&&q.retry_after_ms==3000;cv.notify_all();}
            rpc::MessageMutationRpcResponse result;result.affected_rows=1;
            return rpc::MessageMutationRpcCallResult::Success(result);
        };
        return d;
    }
    bool WaitComplete(int count,std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);return cv.wait_for(lock,timeout,[&]{return completed>=count;});
    }
    bool WaitClaim(int count,std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);return cv.wait_for(lock,timeout,[&]{return claims>=count;});
    }
};
GroupFanoutCoordinatorOptions Options() {
    GroupFanoutCoordinatorOptions o;o.gateway_id="partial-native";o.batch_size=64;
    o.lease_ms=5000;o.recovery_interval=1000ms;return o;
}
void Check(bool condition,const char* name) {
    if(!condition)throw std::runtime_error(name);
    std::cout<<"[PASS] "<<name<<'\n';
}
}
int main(int argc,char** argv) {
try {
    if(argc!=3)return 2;
    bool flag=std::string(argv[1])=="1",wake=std::string(argv[2])=="1",effective=flag&&wake;
    Check(GroupFanoutPartialDrainEnabled()==flag&&GroupFanoutCommitWakeEnabled()==wake,"strict_flag_and_wake_mode");
    {
        Probe p(Probe::Mode::Split49And15);GroupFanoutCoordinator c(p.Dependencies(),Options());
        Check(c.Start()&&p.WaitComplete(49,2s),"first_committed_partial49_completed");
        bool done=p.WaitComplete(64,300ms);auto before=std::chrono::steady_clock::now();c.Stop();
        Check(done==effective&&p.valid&&p.completed==(effective?64:49)&&
              std::chrono::steady_clock::now()-before<300ms,"49plus15_without_recovery_sleep_and_prompt_stop");
    }
    {
        Probe p(Probe::Mode::FivePartial);GroupFanoutCoordinator c(p.Dependencies(),Options());
        Check(c.Start()&&p.WaitComplete(1,2s),"first_hot_partial_ready");
        bool done=p.WaitComplete(5,300ms);c.Stop();
        Check(done==effective&&p.valid&&p.completed==(effective?5:1)&&
              (!effective||p.times.at(4)-p.times.at(0)>=20ms),"four_batch_bound_and_original25ms_yield");
    }
    for(auto mode:{Probe::Mode::Empty,Probe::Mode::Failed}) {
        Probe p(mode);GroupFanoutCoordinator c(p.Dependencies(),Options());
        Check(c.Start()&&p.WaitClaim(1,2s),"empty_or_failed_claim_started");
        bool repeated=p.WaitClaim(2,150ms);c.Stop();
        Check(!repeated&&p.valid&&p.completed==0&&p.claims==1,"empty_or_failed_uses_original_recovery_no_spin");
    }
    std::cout<<nlohmann::json({{"status","GROUP_PARTIAL_DRAIN_NATIVE_PASS"},{"enabled",flag},{"wake",wake},
         {"checks",9},{"reproduction","49_then15_with_original_limit64_recovery1000"},
         {"performance_acceptance",false}}).dump()<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
}
