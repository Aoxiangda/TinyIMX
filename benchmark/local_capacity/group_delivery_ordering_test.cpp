#include "gateway/GroupDeliveryOrdering.h"
#include "gateway/business/BusinessExecutor.h"
#include "tests/concurrency/TestFramework.h"
#include <iostream>
#include <algorithm>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
#include <vector>
using namespace tinyimx;
using namespace std::chrono_literals;
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    const bool enabled=std::string(argv[1])=="1";
    if(GroupDeliveryRecipientOrderingEnabled()!=enabled)return 3;
    tinyimx::test::TestRunner runner;
    runner.Add("RecipientOrdering.StrictIdentityAndUint64",[&] {
        for(auto mid:{1ULL,9007199254740993ULL,18446744073709551615ULL}) {
            const auto a=GroupDeliveryOrderingKey(mid,701);
            TINYIMX_EXPECT_EQ(a,GroupDeliveryOrderingKey(mid,701));
            if(enabled)TINYIMX_EXPECT_TRUE(a!=GroupDeliveryOrderingKey(mid,702));
            else TINYIMX_EXPECT_EQ(a,mid);
        }
    });
    runner.Add("RecipientOrdering.SameIdentityFifoAndIndependentRecipientProgress",[&] {
        BusinessExecutorOptions o;o.worker_threads=2;o.stripe_count=1024;
        o.max_pending_tasks=32;o.per_stripe_queue_capacity=8;
        o.default_deadline=4s;o.shutdown_timeout=3s;
        BusinessExecutor executor(o);TINYIMX_EXPECT_TRUE(executor.Start());
        std::mutex mutex;std::condition_variable cv;
        bool entered=false,release=false,other=false,duplicate=false;
        std::vector<int> order;
        const auto first=GroupDeliveryOrderingKey(9001,701);
        std::uint64_t uid=702;
        if(enabled)while(GroupDeliveryOrderingKey(9001,uid)%1024==first%1024)++uid;
        auto submit=[&](int which,BusinessOrderingKey key) {
            BusinessExecutor::TaskSpec t;t.request.operation="group-delivery-identity-test";
            t.request.ordering_key=key;t.cancellation_policy=BusinessCancellationPolicy::kMustRun;
            t.work=[&,which](const BusinessExecutor::ExecutionContext&)->BusinessExecutor::Completion {
                std::unique_lock lock(mutex);
                if(which==1){entered=true;cv.notify_all();cv.wait_for(lock,3s,[&]{return release;});}
                if(which==2)duplicate=true;
                if(which==3)other=true;
                order.push_back(which);cv.notify_all();return {};
            };
            t.dispatcher=[](BusinessExecutor::Completion c){if(c)c();};
            return executor.Submit(std::move(t));
        };
        const auto one=submit(1,first);
        bool saw_entered=false;
        {std::unique_lock lock(mutex);saw_entered=cv.wait_for(lock,2s,[&]{return entered;});}
        const auto two=submit(2,first);
        const auto three=submit(3,GroupDeliveryOrderingKey(9001,uid));
        bool early_other=false,early_duplicate=false;
        {std::unique_lock lock(mutex);early_other=cv.wait_for(lock,enabled?2s:200ms,[&]{return other;});
         early_duplicate=duplicate;release=true;cv.notify_all();}
        const bool drained=executor.ShutdownGraceful();
        TINYIMX_EXPECT_TRUE(one==BusinessSubmitStatus::kAccepted&&two==one&&three==one);
        TINYIMX_EXPECT_TRUE(saw_entered&&drained);
        TINYIMX_EXPECT_EQ(early_other,enabled);TINYIMX_EXPECT_TRUE(!early_duplicate);
        TINYIMX_EXPECT_TRUE(duplicate&&other);
        auto p1=std::find(order.begin(),order.end(),1),p2=std::find(order.begin(),order.end(),2);
        TINYIMX_EXPECT_TRUE(p1<p2);
        const auto stats=executor.GetStats();
        TINYIMX_EXPECT_EQ(stats.accepted_total,static_cast<std::uint64_t>(3));
        TINYIMX_EXPECT_EQ(stats.completed_total,static_cast<std::uint64_t>(3));
        TINYIMX_EXPECT_EQ(stats.current_pending_tasks,static_cast<std::size_t>(0));
    });
    const int failures=runner.RunAll("Group delivery recipient ordering native");
    std::cout<<"{\"status\":\""<<(failures?"FAIL":"GROUP_DELIVERY_ORDER_NATIVE_PASS")
             <<"\",\"enabled\":"<<(enabled?"true":"false")
             <<",\"tests\":2,\"failures\":"<<failures<<"}\n";
    return failures==0?0:1;
}
