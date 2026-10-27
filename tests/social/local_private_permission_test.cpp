#include "services/rpc/LocalPrivateChatPermissionClient.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
namespace rpc=tinyimx::rpc;
namespace social=tinyimx::social;
void Require(bool ok,const char* name) {
    if (!ok) throw std::runtime_error(name);
    std::cout << "PASS " << name << std::endl;
}
template<class F> void Until(F f) {
    const auto deadline=std::chrono::steady_clock::now()+2s;
    while(!f()) {
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("test state watchdog");
        std::this_thread::sleep_for(1ms);
    }
}
rpc::RpcCallOptions Budget(std::chrono::milliseconds value=2s) {
    rpc::RpcCallOptions out;out.remaining_timeout=value;return out;
}
class Repository final:public social::FriendRepositoryPort {
public:
    std::atomic<int> calls{0},status{0};
    std::atomic<bool> fail{false};
    std::mutex mutex;std::condition_variable cv;bool hold{false},released{false};
    social::ChatPermissionApplicationResult CheckPrivateChatPermission(std::uint64_t from,std::uint64_t to)override {
        ++calls;
        if(from!=500001 || to!=500002)throw std::runtime_error("unexpected identity");
        {std::unique_lock lock(mutex);if(hold&&!cv.wait_for(lock,1s,[&]{return released;}))throw std::runtime_error("read gate watchdog");}
        if(fail)throw std::runtime_error("injected repository exception");
        return {static_cast<social::ChatPermissionApplicationStatus>(status.load()),"authoritative"};
    }
    social::FriendRepositoryListResult ListFriends(std::uint64_t,std::size_t)override {throw std::logic_error("unexpected list");}
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
};
void OutcomesAndFreshReads() {
    Repository repo;social::FriendApplicationService app(&repo);
    rpc::LocalPrivateChatPermissionClient client(&app,2,4);
    for(int i=0;i<6;++i) {
        repo.status=i;auto result=client.CheckPrivateChatPermission({500001,500002},Budget());
        Require(result.ok()&&result.value->outcome==static_cast<rpc::ChatPermissionRpcOutcome>(i)&&result.value->message=="authoritative","all Social application outcomes preserved");
    }
    repo.status=0;Require(client.CheckPrivateChatPermission({500001,500002},Budget()).value->Allowed(),"allowed from current state");
    repo.status=4;Require(!client.CheckPrivateChatPermission({500001,500002},Budget()).value->Allowed(),"peer block immediately re-reads authoritative state");
    Require(repo.calls==8,"no positive permission cache or skipped reads");
    repo.status=99;Require(client.CheckPrivateChatPermission({500001,500002},Budget()).status.code==rpc::RpcErrorCode::kDataLoss,"unknown status fails closed");
    repo.fail=true;Require(client.CheckPrivateChatPermission({500001,500002},Budget()).status.code==rpc::RpcErrorCode::kInternal,"repository exception fails closed");
    const auto before=repo.calls.load();
    Require(client.CheckPrivateChatPermission({0,500002},Budget()).status.code==rpc::RpcErrorCode::kInvalidArgument&&client.CheckPrivateChatPermission({500001,500001},Budget()).status.code==rpc::RpcErrorCode::kInvalidArgument,"invalid identity before scheduling");
    Require(client.CheckPrivateChatPermission({500001,500002},Budget(0ms)).status.code==rpc::RpcErrorCode::kDeadlineExceeded&&repo.calls==before,"exhausted budget does not enter repository");
    client.Drain();client.Drain();
    Require(client.CheckPrivateChatPermission({500001,500002},Budget()).status.code==rpc::RpcErrorCode::kUnavailable,"idempotent drain rejects new work");
}
void BoundedQueueAndExpiredQueuedRead() {
    Repository repo;repo.hold=true;social::FriendApplicationService app(&repo);
    rpc::LocalPrivateChatPermissionClient client(&app,1,1);
    auto active=std::async(std::launch::async,[&]{return client.CheckPrivateChatPermission({500001,500002},Budget());});
    Until([&]{return repo.calls==1;});
    auto queued=std::async(std::launch::async,[&]{return client.CheckPrivateChatPermission({500001,500002},Budget(30ms));});
    Until([&]{return client.Stats().submitted==2;});
    const auto overflow=client.CheckPrivateChatPermission({500001,500002},Budget());
    Require(overflow.status.code==rpc::RpcErrorCode::kResourceExhausted,"full local queue rejects without waiting or fallback");
    Require(queued.wait_for(300ms)==std::future_status::ready&&queued.get().status.code==rpc::RpcErrorCode::kDeadlineExceeded,"queued deadline returns while first DB read remains blocked");
    Require(repo.calls==1,"expired queued call has not entered repository");
    repo.Release();Require(active.get().ok(),"active accepted call completed");client.Drain();
    const auto stats=client.Stats();
    Require(repo.calls==1&&stats.expired_before_execution==1&&stats.submitted==2&&stats.executed==1,"queued expiry is skipped and lifecycle fully accounted");
}
void ExpiredActiveReadAndDrain() {
    Repository repo;repo.hold=true;social::FriendApplicationService app(&repo);
    rpc::LocalPrivateChatPermissionClient client(&app,1,2);
    auto caller=std::async(std::launch::async,[&]{return client.CheckPrivateChatPermission({500001,500002},Budget(30ms));});
    Until([&]{return repo.calls==1;});
    Require(caller.wait_for(300ms)==std::future_status::ready&&caller.get().status.code==rpc::RpcErrorCode::kDeadlineExceeded,"active DB read never returns late permission success");
    auto drain=std::async(std::launch::async,[&]{client.Drain();});
    Require(drain.wait_for(15ms)==std::future_status::timeout,"drain retains application lifetime until active read finishes");
    repo.Release();Require(drain.wait_for(1s)==std::future_status::ready,"drain completes after read release");drain.get();
    Require(client.Stats().expired_after_execution==1,"late result safely discarded after caller has returned");
}
void ConcurrentCallsAndDrain() {
    Repository repo;social::FriendApplicationService app(&repo);
    rpc::LocalPrivateChatPermissionClient client(&app,4,32);
    std::atomic<int> allowed{0},denied{0};std::vector<std::thread> callers;
    for(int i=0;i<8;++i)callers.emplace_back([&]{for(int n=0;n<50;++n){auto r=client.CheckPrivateChatPermission({500001,500002},Budget());if(r.ok()&&r.value->Allowed())++allowed;else ++denied;}});
    Until([&]{return client.Stats().submitted>0;});client.Drain();
    for(auto& t:callers)t.join();
    Require(allowed+denied==400&&client.Stats().submitted==client.Stats().executed,"concurrent admission/drain accounts every caller without dangling state");
}
}
int main() {
    try {OutcomesAndFreshReads();BoundedQueueAndExpiredQueuedRead();ExpiredActiveReadAndDrain();ConcurrentCallsAndDrain();}
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
    return 0;
}
