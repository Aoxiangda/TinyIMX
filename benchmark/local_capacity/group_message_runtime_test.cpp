#include "gateway/GroupMessageRuntime.h"
#include "gateway/GroupDeliveryOrdering.h"
#include "gateway/business/BusinessExecutor.h"
#include "gateway/ReceiverDeliveryTracker.h"
#include "tests/concurrency/TestFramework.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <iostream>
#include <string>
#include <vector>
using namespace tinyimx;
using namespace std::chrono_literals;
namespace tinyimx::test { void RegisterReceiverDeliveryTrackerTests(TestRunner&); }
static BusinessExecutorOptions Options() {
    BusinessExecutorOptions o;
    o.worker_threads=1; o.stripe_count=64; o.max_pending_tasks=16;
    o.per_stripe_queue_capacity=2; o.default_deadline=4s; o.shutdown_timeout=3s;
    return o;
}
static BusinessSubmitStatus Submit(BusinessExecutor* executor, BusinessOrderingKey key,
    std::function<void()> work) {
    BusinessExecutor::TaskSpec t; t.request.operation="group-message-runtime-native";
    t.request.ordering_key=key; t.cancellation_policy=BusinessCancellationPolicy::kMustRun;
    t.work=[work=std::move(work)](const BusinessExecutor::ExecutionContext&)
        -> BusinessExecutor::Completion { work(); return {}; };
    t.dispatcher=[](BusinessExecutor::Completion completion){ if(completion)completion(); };
    return executor->Submit(std::move(t));
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    const bool enabled=std::string(argv[1])=="1";
    if(GroupDeliveryMessageRuntimeEnabled()!=enabled)return 3;
    tinyimx::test::TestRunner runner;
    runner.Add("GroupRuntime.SelectionAndAbsentFallback",[&] {
        BusinessExecutor control(Options()),message(Options());
        TINYIMX_EXPECT_EQ(SelectGroupDeliveryExecutor(&control,&message),
                         enabled?&message:&control);
        TINYIMX_EXPECT_EQ(SelectGroupDeliveryExecutor(&control,nullptr),&control);
        TINYIMX_EXPECT_EQ(SelectGroupDeliveryExecutor(nullptr,&message),
                         enabled?&message:nullptr);
        TINYIMX_EXPECT_EQ(SelectGroupDeliveryExecutor(nullptr,nullptr),nullptr);
        TINYIMX_EXPECT_EQ(SelectGroupDeliveryExecutor(&control,&control),&control);
    });
    runner.Add("GroupRuntime.BlockedPeerKeepsAckFifoAndControlIsolation",[&] {
        BusinessExecutor control(Options()),message(Options());
        TINYIMX_EXPECT_TRUE(control.Start()&&message.Start());
        auto* executor=SelectGroupDeliveryExecutor(&control,&message);
        std::mutex mutex;std::condition_variable cv;
        bool entered=false,release=false,ack=false,profile=false,private_work=false;
        ReceiverDeliveryTracker tracker;
        const auto identity=GroupDeliveryIdentity(9007199254740993ULL,701);
        ReceiverDeliveryAckStatus status=ReceiverDeliveryAckStatus::kInvalidArgument;
        const auto key=GroupDeliveryOrderingKey(identity.message_id,identity.recipient_user_id);
        auto one=Submit(executor,key,[&] {
            tracker.RegisterAttempt(identity,41);
            std::unique_lock lock(mutex);entered=true;cv.notify_all();
            cv.wait_for(lock,5s,[&]{return release;});
        });
        bool began=false;
        {std::unique_lock lock(mutex);began=cv.wait_for(lock,2s,[&]{return entered;});}
        auto two=Submit(executor,key,[&] {
            status=tracker.Acknowledge(identity,41);
            std::lock_guard lock(mutex);ack=true;cv.notify_all();
        });
        auto other_control=Submit(&control,key+1,[&] {
            std::lock_guard lock(mutex);profile=true;cv.notify_all();
        });
        auto other_message=Submit(&message,key+1,[&] {
            std::lock_guard lock(mutex);private_work=true;cv.notify_all();
        });
        bool early_profile=false,early_private=false,early_ack=false;
        {std::unique_lock lock(mutex);
         cv.wait_for(lock,800ms,[&]{return enabled?profile:private_work;});
         early_profile=profile;early_private=private_work;early_ack=ack;
         release=true;cv.notify_all();}
        const bool drained_control=control.ShutdownGraceful();
        const bool drained_message=message.ShutdownGraceful();
        TINYIMX_EXPECT_TRUE(one==BusinessSubmitStatus::kAccepted&&two==one&&
                           other_control==one&&other_message==one);
        TINYIMX_EXPECT_TRUE(began&&drained_control&&drained_message);
        TINYIMX_EXPECT_EQ(early_profile,enabled);
        TINYIMX_EXPECT_EQ(early_private,!enabled);
        TINYIMX_EXPECT_TRUE(!early_ack&&ack&&profile&&private_work);
        TINYIMX_EXPECT_EQ(status,ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(control.GetStats().current_pending_tasks,std::size_t{0});
        TINYIMX_EXPECT_EQ(message.GetStats().current_pending_tasks,std::size_t{0});
    });
    runner.Add("GroupRuntime.AdmissionAndDrainNeverSwitchOrderingDomain",[&] {
        BusinessExecutor control(Options()),message(Options());
        TINYIMX_EXPECT_TRUE(control.Start()&&message.Start());
        auto* executor=SelectGroupDeliveryExecutor(&control,&message);
        std::mutex mutex;std::condition_variable cv;
        bool entered=false,release=false;int finished=0;
        auto one=Submit(executor,99,[&] {
            std::unique_lock lock(mutex);entered=true;cv.notify_all();
            cv.wait_for(lock,5s,[&]{return release;});++finished;
        });
        bool began=false;
        {std::unique_lock lock(mutex);began=cv.wait_for(lock,2s,[&]{return entered;});}
        auto two=Submit(executor,99,[&] {std::lock_guard lock(mutex);++finished;});
        auto three=Submit(executor,99,[]{});
        executor->BeginDrain();
        TINYIMX_EXPECT_EQ(SelectGroupDeliveryExecutor(&control,&message),executor);
        auto after=Submit(executor,100,[]{});
        {std::lock_guard lock(mutex);release=true;cv.notify_all();}
        const bool c=control.ShutdownGraceful(),m=message.ShutdownGraceful();
        TINYIMX_EXPECT_TRUE(began&&c&&m);
        TINYIMX_EXPECT_EQ(one,BusinessSubmitStatus::kAccepted);
        TINYIMX_EXPECT_EQ(two,one);
        TINYIMX_EXPECT_EQ(three,BusinessSubmitStatus::kHotKeyOverloaded);
        TINYIMX_EXPECT_EQ(after,BusinessSubmitStatus::kShuttingDown);
        TINYIMX_EXPECT_EQ(finished,2);
        TINYIMX_EXPECT_EQ(executor->GetStats().accepted_total,std::uint64_t{2});
        TINYIMX_EXPECT_EQ(executor->GetStats().completed_total,std::uint64_t{2});
        TINYIMX_EXPECT_EQ(executor->GetStats().current_pending_tasks,std::size_t{0});
    });
    runner.Add("GroupRuntime.InstalledStoppedExecutorDoesNotFallback",[&] {
        BusinessExecutor control(Options()),message(Options());
        TINYIMX_EXPECT_TRUE(control.Start()&&message.Start());
        TINYIMX_EXPECT_TRUE(message.ShutdownGraceful());
        auto* selected=SelectGroupDeliveryExecutor(&control,&message);
        auto result=Submit(selected,19,[]{});
        const bool c=control.ShutdownGraceful();
        TINYIMX_EXPECT_TRUE(c);
        TINYIMX_EXPECT_EQ(result,enabled?BusinessSubmitStatus::kShuttingDown:
                                            BusinessSubmitStatus::kAccepted);
    });
    tinyimx::test::RegisterReceiverDeliveryTrackerTests(runner);
    const int failures=runner.RunAll("Group message executor selection and original ACK tracker");
    std::cout<<"{\"status\":\""<<(failures?"FAIL":"GROUP_MESSAGE_RUNTIME_NATIVE_PASS")
       <<"\",\"enabled\":"<<(enabled?"true":"false")
       <<",\"tests\":31,\"failures\":"<<failures<<"}\n";
    return failures==0?0:1;
}
