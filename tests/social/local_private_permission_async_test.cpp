#include "services/rpc/LocalPrivateChatPermissionClient.h"
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
namespace {
using namespace std::chrono_literals;
namespace rpc=tinyimx::rpc;namespace social=tinyimx::social;
using Result=rpc::RpcResult<rpc::CheckPrivateChatPermissionRpcResponse>;
std::atomic<unsigned> checks{0};
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);++checks;}
template<class F>void Until(F fn){const auto end=std::chrono::steady_clock::now()+2s;while(!fn()){Check(std::chrono::steady_clock::now()<end,"bounded test progress");std::this_thread::sleep_for(1ms);}}
rpc::RpcCallOptions Budget(std::chrono::milliseconds t=2s){rpc::RpcCallOptions o;o.remaining_timeout=t;return o;}
class Repository final:public social::FriendRepositoryPort {
public:
    std::atomic<unsigned> calls{0};std::atomic<int> status{0};std::atomic<bool> fail{false};
    std::mutex mutex;std::condition_variable cv;bool hold{false},released{false};
    social::ChatPermissionApplicationResult CheckPrivateChatPermission(std::uint64_t from,std::uint64_t to)override{
        ++calls;if(!from||!to||from==to)throw std::runtime_error("invalid actor reached repository");
        {std::unique_lock lock(mutex);if(hold&&!cv.wait_for(lock,2s,[&]{return released;}))throw std::runtime_error("read gate timeout");}
        if(fail)throw std::runtime_error("repository failure");
        return{static_cast<social::ChatPermissionApplicationStatus>(status.load()),"authoritative"};
    }
    social::FriendRepositoryListResult ListFriends(std::uint64_t,std::size_t)override{throw std::logic_error("unexpected list");}
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
};
struct Reply {
    std::mutex mutex;std::condition_variable cv;std::optional<Result> value;unsigned calls{0};
    void Set(Result r){std::lock_guard lock(mutex);++calls;if(!value)value.emplace(std::move(r));cv.notify_all();}
    Result Get(){std::unique_lock lock(mutex);Check(cv.wait_for(lock,2s,[&]{return value.has_value();}),"callback completes within watchdog");return std::move(*value);}
    unsigned Count(){std::lock_guard lock(mutex);return calls;}
};
auto Start(rpc::LocalPrivateChatPermissionClient& c,rpc::RpcCallOptions o=Budget(),rpc::CheckPrivateChatPermissionRpcRequest q={500001,500002}){
    auto r=std::make_shared<Reply>();c.CheckPrivateChatPermissionAsync(q,o,[r](Result v){r->Set(std::move(v));});return r;
}
void Outcomes(){
    Repository repo;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient c(&app,2,4);
    Check(c.SupportsAsync()&&c.AsyncStats().started==0,"async supported without starting work on default path");
    for(int i=0;i<6;++i){repo.status=i;auto r=Start(c)->Get();Check(r.ok()&&static_cast<int>(r.value->outcome)==i&&r.value->message=="authoritative","all six original application outcomes retained");}
    repo.status=0;Check(Start(c)->Get().value->Allowed(),"current allowed state used");repo.status=4;Check(!Start(c)->Get().value->Allowed(),"changed peer block is reread without positive cache");
    repo.status=99;Check(Start(c)->Get().status.code==rpc::RpcErrorCode::kDataLoss,"unknown status fails closed");repo.fail=true;Check(Start(c)->Get().status.code==rpc::RpcErrorCode::kInternal,"repository exception fails closed");
    auto before=repo.calls.load();Check(Start(c,Budget(),{0,500002})->Get().status.code==rpc::RpcErrorCode::kInvalidArgument,"invalid request completed before scheduling");
    Check(Start(c,Budget(0ms))->Get().status.code==rpc::RpcErrorCode::kDeadlineExceeded&&repo.calls==before,"exhausted budget never enters repository");
    c.Drain();c.Drain();Check(Start(c)->Get().status.code==rpc::RpcErrorCode::kUnavailable,"idempotent drain fences new asynchronous work");
    auto s=c.AsyncStats();Check(s.started==10&&s.finished==10&&s.pending==0,"outcome callbacks and logical ownership fully accounted");
}
void BoundedQueue(){
    Repository repo;repo.hold=true;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient c(&app,1,1);
    auto active=Start(c);Until([&]{return repo.calls==1;});
    auto queued=Start(c,Budget(25ms));auto excess=Start(c);
    Check(excess->Get().status.code==rpc::RpcErrorCode::kResourceExhausted,"full pending bound rejects without fallback SQL");
    Check(queued->Get().status.code==rpc::RpcErrorCode::kDeadlineExceeded&&repo.calls==1,"queued deadline callback fires while first SQL remains blocked");
    repo.Release();Check(active->Get().ok(),"accepted active read finishes normally");c.Drain();
    auto s=c.AsyncStats();auto old=c.Stats();
    Check(active->Count()==1&&queued->Count()==1&&excess->Count()==1,"queue reject timeout completion each invoke once");
    Check(s.started==2&&s.finished==2&&s.pending==0&&s.peak_pending<=2,"bounded timer ownership drains");
    Check(old.submitted==2&&old.executed==1&&old.expired_before_execution==1,"expired queued read is skipped at execution");
}
void ActiveDeadlineAndDrain(){
    Repository repo;repo.hold=true;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient c(&app,1,2);
    auto active=Start(c,Budget(25ms));Until([&]{return repo.calls==1;});
    Check(active->Get().status.code==rpc::RpcErrorCode::kDeadlineExceeded,"active SQL cannot hold caller beyond timeout until SQL returns");
    auto drain=std::async(std::launch::async,[&]{c.Drain();});
    Check(drain.wait_for(20ms)==std::future_status::timeout,"drain retains repository lifetime after caller deadline");
    repo.Release();drain.get();Check(active->Count()==1&&c.Stats().expired_after_execution==1,"late allowed result discarded with no second callback");
    Check(c.AsyncStats().pending==0,"active timeout leaves no pending timer state");
}
void CallbackLifetimeAndReentry(){
    Repository repo;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient c(&app,1,8);
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;auto reply=std::make_shared<Reply>();
    auto submitting=std::async(std::launch::async,[&]{c.CheckPrivateChatPermissionAsync({500001,500002},Budget(),[&](Result r){
        {std::unique_lock lock(mutex);entered=true;cv.notify_all();if(!cv.wait_for(lock,2s,[&]{return released;}))throw std::runtime_error("callback gate");}
        reply->Set(std::move(r));
    });});
    {std::unique_lock lock(mutex);Check(cv.wait_for(lock,2s,[&]{return entered;}),"actual callback entered");}
    auto drain=std::async(std::launch::async,[&]{c.Drain();});
    Check(drain.wait_for(20ms)==std::future_status::timeout,"drain includes active callback completion");
    {std::lock_guard lock(mutex);released=true;}cv.notify_all();submitting.get();drain.get();Check(reply->Get().ok()&&reply->Count()==1,"callback completed before drained dependencies may close");
    rpc::LocalPrivateChatPermissionClient other(&app,2,8);auto nested=std::make_shared<Reply>();
    other.CheckPrivateChatPermissionAsync({500001,500002},Budget(),[&](Result first){
        if(!first.ok())throw std::runtime_error("first permission failed");
        other.CheckPrivateChatPermissionAsync({500003,500004},Budget(),[nested](Result r){nested->Set(std::move(r));});
        throw std::runtime_error("user continuation exception");
    });
    Check(nested->Get().ok(),"callback reentry works outside internal admission and timer locks");other.Drain();
    Check(nested->Count()==1&&other.AsyncStats().started==2&&other.AsyncStats().finished==2,"throwing continuation does not strand another callback or timer");
}
void AdmissionDrainRace(){
    Repository repo;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient c(&app,4,8);
    std::barrier start(9);std::vector<std::thread> threads;std::atomic<unsigned> finished{0},duplicates{0};
    for(unsigned i=0;i<8;++i)threads.emplace_back([&]{start.arrive_and_wait();for(unsigned n=0;n<50;++n){auto r=Start(c);r->Get();if(r->Count()!=1)++duplicates;++finished;}});
    start.arrive_and_wait();Until([&]{return c.Stats().submitted>0;});c.Drain();
    for(auto& t:threads)t.join();auto a=c.AsyncStats();auto s=c.Stats();
    Check(finished==400&&duplicates==0,"all400 concurrent admission/drain callers receive one result");
    Check(a.started==s.submitted&&a.finished==a.started&&a.pending==0&&a.peak_pending<=12,"all accepted asynchronous calls finish within original pool bound");
    Check(s.submitted==s.executed+s.expired_before_execution,"accepted read accounting retained across shutdown race");
}
}
int main()try{Outcomes();BoundedQueue();ActiveDeadlineAndDrain();CallbackLifetimeAndReentry();AdmissionDrainRace();std::cout<<"LOCAL_PRIVATE_PERMISSION_ASYNC_CONTRACTS_PASS checks="<<checks.load()<<'\n';return 0;}
catch(const std::exception&e){std::cerr<<"LOCAL_PRIVATE_PERMISSION_ASYNC_CONTRACTS_FAIL "<<e.what()<<'\n';return 1;}
