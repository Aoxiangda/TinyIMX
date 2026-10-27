#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/rpc/RpcAwaitable.h"
#include "gateway/business/BusinessExecutor.h"
#include "common/logging/Logger.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace rpc=tinyimx::rpc;namespace pb=tinyimx::social::v1;
using Result=rpc::RpcResult<rpc::CheckPrivateChatPermissionRpcResponse>;
using tinyimx::BusinessExecutor;
int failures=0;
void Check(bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;}
rpc::RpcCallOptions Options(){rpc::RpcCallOptions o;o.request_id="owned-async-permission";o.trace_id="owned-permission-trace";
    o.caller_service="gateway-test";o.caller_instance="owned-gateway";o.remaining_timeout=2s;return o;}
class Service final:public pb::SocialService::Service {
public:
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    std::atomic<int> calls{0},meta_errors{0};
    grpc::Status CheckPrivateChatPermission(grpc::ServerContext* context,const pb::CheckPrivateChatPermissionRequest* q,
        pb::CheckPrivateChatPermissionResponse* r)override {
        ++calls;if(q->meta().request_id()!=Options().request_id||q->meta().caller_service()!=Options().caller_service||
            q->meta().caller_instance()!=Options().caller_instance||q->meta().trace_id()!=Options().trace_id)++meta_errors;
        if(q->from_user_id()==1){std::unique_lock lock(mutex);entered=true;cv.notify_all();
            if(!cv.wait_for(lock,4s,[&]{return released;}))return {grpc::StatusCode::INTERNAL,"owned server gate watchdog"};}
        if(q->from_user_id()==7)return grpc::Status::OK; // unspecified is never an allow
        if(q->from_user_id()==8)return {grpc::StatusCode::UNAVAILABLE,"owned transport failure"};
        if(q->from_user_id()==9){const auto end=std::chrono::steady_clock::now()+300ms;
            while(!context->IsCancelled()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(1ms);
            return {grpc::StatusCode::CANCELLED,"owned deadline path"};}
        switch(q->from_user_id()){
            case 3:r->set_result(pb::CHAT_PERMISSION_RESULT_NOT_FRIEND);break;
            case 4:r->set_result(pb::CHAT_PERMISSION_RESULT_BLOCKED_BY_SELF);break;
            case 5:r->set_result(pb::CHAT_PERMISSION_RESULT_BLOCKED_BY_PEER);break;
            case 6:r->set_result(pb::CHAT_PERMISSION_RESULT_STORAGE_ERROR);break;
            default:r->set_result(pb::CHAT_PERMISSION_RESULT_ALLOWED);break;
        }
        r->set_message("owned permission response");return grpc::Status::OK;
    }
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
};
struct Server{
    Service service;std::unique_ptr<grpc::Server> server;std::shared_ptr<rpc::StaticServiceEndpointProvider> endpoint;
    Server(){grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);b.RegisterService(&service);
        server=b.BuildAndStart();if(!server||!port)throw std::runtime_error("owned RPC server failed");
        endpoint=std::make_shared<rpc::StaticServiceEndpointProvider>("127.0.0.1:"+std::to_string(port));}
    ~Server(){service.Release();server->Shutdown();server->Wait();}
};
template<class Start>Result Await(Start start){
    struct State{std::promise<Result> promise;std::atomic<int> count{0};};auto state=std::make_shared<State>();auto future=state->promise.get_future();
    start([state](Result result){if(state->count.fetch_add(1)==0)state->promise.set_value(std::move(result));});
    if(future.wait_for(5s)!=std::future_status::ready)throw std::runtime_error("async permission callback watchdog");
    auto value=future.get();Check(state->count==1,"permission callback completes once");return value;
}
Result Query(rpc::SocialRpcClient& client,std::uint64_t from,rpc::RpcCallOptions options=Options()){
    return Await([&](auto done){client.CheckPrivateChatPermissionAsync({from,500002},options,std::move(done));});
}
void Contracts(){
    Server server;rpc::SocialRpcClient client(server.endpoint);
    const auto before=server.service.calls.load();Check(Query(client,0).status.code==rpc::RpcErrorCode::kInvalidArgument,"invalid identity rejected before transport");
    auto expired=Options();expired.remaining_timeout=0ms;
    Check(Query(client,2,expired).status.code==rpc::RpcErrorCode::kDeadlineExceeded&&server.service.calls==before,"exhausted deadline consumes no RPC");
    rpc::SocialRpcClient absent(nullptr);Check(Query(absent,2).status.code==rpc::RpcErrorCode::kUnavailable,"missing provider fails closed");
    rpc::SocialRpcClient missing(std::make_shared<rpc::StaticServiceEndpointProvider>(""));
    Check(Query(missing,2).status.code==rpc::RpcErrorCode::kUnavailable,"missing endpoint fails closed");
    for(std::uint64_t from=2;from<=6;++from){const auto sync=client.CheckPrivateChatPermission({from,500002},Options());const auto async=Query(client,from);
        Check(sync.ok()&&async.ok()&&sync.value->outcome==async.value->outcome&&sync.value->message==async.value->message&&
            async.value->Allowed()==(from==2),"async allow and every rejection match authoritative sync result");}
    Check(Query(client,7).status.code==rpc::RpcErrorCode::kDataLoss,"unspecified response cannot authorize persistence");
    Check(Query(client,8).status.code==rpc::RpcErrorCode::kUnavailable,"transport failure never becomes permission allowed");
    auto deadline=Options();deadline.remaining_timeout=10ms;
    Check(Query(client,9,deadline).status.code==rpc::RpcErrorCode::kDeadlineExceeded,"real server delay respects client RPC deadline");
    Check(server.service.meta_errors==0&&client.CachedTargetCountForTest()==1&&client.StubCreationCountForTest()==1,
        "request identity and trace metadata retained with one cached target");
}
thread_local int session_scope=0;
struct FlowState{std::mutex mutex;std::condition_variable cv;std::vector<int> order;std::atomic<int> scoped{0};};
tinyimx::BusinessCoroutine Flow(rpc::SocialRpcClient* client,int id,FlowState* state){
    if(session_scope==71)++state->scoped;
    auto result=co_await rpc::RpcAwaitable<Result>(true,[]{throw std::logic_error("sync fallback used");return Result{};},
        [client,id](auto done){client->CheckPrivateChatPermissionAsync({static_cast<std::uint64_t>(id),500002},Options(),std::move(done));});
    if(session_scope==71)++state->scoped;
    if(!result.ok()||!result.value->Allowed())throw std::runtime_error("owned asynchronous permission denied");
    std::lock_guard lock(state->mutex);state->order.push_back(id);state->cv.notify_all();co_return;
}
void WorkerAndLifetime(){
    Server server;rpc::SocialRpcClient client(server.endpoint);FlowState state;
    tinyimx::BusinessExecutorOptions o;o.worker_threads=1;o.max_pending_tasks=8;o.stripe_count=4;o.per_stripe_queue_capacity=8;o.shutdown_timeout=2s;
    BusinessExecutor executor(o);Check(executor.Start(),"single business worker starts");
    auto submit=[&](int id,std::uint64_t key){BusinessExecutor::TaskSpec t;t.request.ordering_key=key;
        t.cancellation_policy=tinyimx::BusinessCancellationPolicy::kMustRun;
        t.async_work=[&,id](const auto&,auto post,auto done){auto task=Flow(&client,id,&state);
            std::move(task).Start(std::move(post),[](auto fn){const auto prior=session_scope;session_scope=71;
                try{fn();}catch(...){session_scope=prior;throw;}session_scope=prior;},[done=std::move(done)](auto error)mutable{done({},error);});};
        return executor.Submit(std::move(t));};
    Check(submit(1,1)==tinyimx::BusinessSubmitStatus::kAccepted,"delayed permission request accepted");
    {std::unique_lock lock(server.service.mutex);Check(server.service.cv.wait_for(lock,2s,[&]{return server.service.entered;}),"real permission RPC waits on owned server gate");}
    Check(submit(10,1)==tinyimx::BusinessSubmitStatus::kAccepted&&submit(11,2)==tinyimx::BusinessSubmitStatus::kAccepted,"same and independent keys accepted");
    {std::unique_lock lock(state.mutex);Check(state.cv.wait_for(lock,2s,[&]{return state.order==std::vector<int>{11};}),
        "only business worker completes another key while permission RPC remains outstanding");}
    server.service.Release();Check(executor.ShutdownGraceful(),"shutdown drains permission RPC and ordered continuation");
    const auto stats=executor.GetStats();
    Check(state.order==std::vector<int>({11,1,10})&&state.scoped==6&&stats.accepted_total==stats.completed_total&&
        stats.current_pending_tasks==0&&stats.worker_exception_total==0,"same-key FIFO and session scope survive asynchronous permission resume");
    Server owned;auto lifetime=std::make_unique<rpc::SocialRpcClient>(owned.endpoint);
    auto promise=std::make_shared<std::promise<Result>>();auto result=promise->get_future();
    lifetime->CheckPrivateChatPermissionAsync({1,500002},Options(),[promise](auto value){promise->set_value(std::move(value));});
    {std::unique_lock lock(owned.service.mutex);Check(owned.service.cv.wait_for(lock,2s,[&]{return owned.service.entered;}),"client destruction fixture has a live RPC");}
    lifetime.reset();owned.service.Release();Check(result.wait_for(3s)==std::future_status::ready&&result.get().ok(),
        "outstanding request owns stub/context and survives client destruction");
}
}
int main(){try{tinyimx::LoggerConfig logger;logger.level="warn";logger.file="async-permission-unit.log";logger.console=true;
    if(!tinyimx::Logger::Instance().Init(logger))return 2;
    Contracts();WorkerAndLifetime();if(!failures)std::cout<<"ASYNC_PERMISSION_RPC_CONTRACTS_PASS\n";
    return failures?1:0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
