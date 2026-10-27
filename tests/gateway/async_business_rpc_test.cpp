#include "gateway/business/BusinessExecutor.h"
#include "services/rpc/RpcAwaitable.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include <grpcpp/grpcpp.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <map>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace pb=tinyimx::message::v1;
namespace rpc=tinyimx::rpc;
using tinyimx::BusinessExecutor;
int failures=0;
void Check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;}
class Service final:public pb::MessageService::Service{
public:
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    std::map<std::string,pb::MessageRecord> persisted;
    grpc::Status GetPrivateMessage(grpc::ServerContext*,const pb::GetPrivateMessageRequest* q,
                                   pb::GetPrivateMessageResponse* r)override{
        if(q->message_id()==1){std::unique_lock lock(mutex);entered=true;cv.notify_all();
            if(!cv.wait_for(lock,4s,[&]{return released;}))return {grpc::StatusCode::INTERNAL,"owned gate watchdog"};}
        auto* record=r->mutable_record();record->set_message_id(q->message_id()==999?998:q->message_id());
        record->set_from_user_id(500001);record->set_to_user_id(500002);record->set_message_type(1);
        record->set_content("verified");record->set_delivery_state(pb::MESSAGE_DELIVERY_STATE_PENDING);
        return grpc::Status::OK;
    }
    grpc::Status PersistPrivateMessage(grpc::ServerContext*,const pb::PersistPrivateMessageRequest* q,
                                       pb::PersistPrivateMessageResponse* r)override{
        std::lock_guard lock(mutex);
        auto [it,created]=persisted.try_emplace(q->client_message_id());auto& row=it->second;
        if(created){row.set_message_id(100+persisted.size());row.set_client_message_id(q->client_message_id());
            row.set_from_user_id(q->from_user_id());row.set_to_user_id(q->to_user_id());
            row.set_message_type(q->message_type());row.set_content(q->content());row.set_delivery_state(pb::MESSAGE_DELIVERY_STATE_PENDING);}
        r->set_result(created?pb::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED:
            row.content()==q->content()?pb::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED:pb::PERSIST_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT);
        r->set_message_id(row.message_id());*r->mutable_record()=row;
        if(created&&q->client_message_id()=="postcommit")return {grpc::StatusCode::DEADLINE_EXCEEDED,"injected after durable acceptance"};
        return grpc::Status::OK;
    }
};
struct Server{
    Service service;std::unique_ptr<grpc::Server> server;std::unique_ptr<rpc::MessageRpcClient> client;
    Server(){grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);b.RegisterService(&service);
        server=b.BuildAndStart();if(!server||!port)throw std::runtime_error("owned server start failed");
        client=std::make_unique<rpc::MessageRpcClient>(std::make_shared<rpc::StaticServiceEndpointProvider>("","","127.0.0.1:"+std::to_string(port)));}
    ~Server(){ {std::lock_guard lock(service.mutex);service.released=true;service.cv.notify_all();}server->Shutdown();server->Wait();}
};
rpc::RpcCallOptions Options(){rpc::RpcCallOptions o;o.remaining_timeout=3s;return o;}
template<class T,class Start>T AwaitCallback(Start start){
    std::mutex mutex;std::condition_variable cv;std::optional<T> result;
    start([&](T value){std::lock_guard lock(mutex);result.emplace(std::move(value));cv.notify_all();});
    std::unique_lock lock(mutex);if(!cv.wait_for(lock,4s,[&]{return result.has_value();}))throw std::runtime_error("callback watchdog");
    return std::move(*result);
}
void ClientContracts(Server& s){
    auto bad=AwaitCallback<rpc::RpcResult<rpc::GetPrivateMessageRpcResponse>>([&](auto done){s.client->GetPrivateMessageAsync({0},Options(),done);});
    Check(!bad.ok()&&bad.status.code==rpc::RpcErrorCode::kInvalidArgument,"async invalid request fails before transport");
    auto malformed=AwaitCallback<rpc::RpcResult<rpc::GetPrivateMessageRpcResponse>>([&](auto done){s.client->GetPrivateMessageAsync({999},Options(),done);});
    Check(!malformed.ok()&&malformed.status.code==rpc::RpcErrorCode::kDataLoss,"async lookup rejects mismatched durable MID");
    rpc::PersistPrivateMessageRpcRequest q;q.from_user_id=500001;q.to_user_id=500002;q.client_message_id="cid";q.message_type=1;q.content="original";
    auto persist=[&]{return AwaitCallback<rpc::PersistPrivateMessageRpcCallResult>([&](auto done){s.client->PersistPrivateMessageAsync(q,Options(),done);});};
    const auto first=persist();const auto again=persist();q.content="changed";const auto conflict=persist();
    Check(first.ok()&&first.value->Created()&&again.ok()&&again.value->Reused()&&first.value->message_id==again.value->message_id,"CID retry retains exactly one durable identity");
    Check(conflict.ok()&&conflict.value->Conflict(),"async persistence retains immutable CID conflict classification");
    q.client_message_id="postcommit";q.content="durable";const auto uncertain=persist();const auto recovered=persist();
    Check(!uncertain.ok()&&uncertain.attempted&&recovered.ok()&&recovered.value->Reused(),"postcommit response loss remains attempted/uncertain and recovers by CID");
}
thread_local int session_scope=0;
struct Results{std::mutex mutex;std::condition_variable cv;std::vector<int> order;std::atomic<int> replies{0},scopes{0},destroyed{0};};
tinyimx::BusinessCoroutine Get(rpc::MessageRpcClient* client,int id,Results* results){
    struct Lifetime{Results* r;~Lifetime(){++r->destroyed;}} lifetime{results};
    if(session_scope==71)++results->scopes;
    auto value=co_await rpc::RpcAwaitable<rpc::RpcResult<rpc::GetPrivateMessageRpcResponse>>(true,
        []{throw std::logic_error("sync fallback unexpectedly used");return rpc::RpcResult<rpc::GetPrivateMessageRpcResponse>{};},
        [client,id](auto done){client->GetPrivateMessageAsync({static_cast<std::uint64_t>(id)},Options(),std::move(done));});
    if(session_scope==71)++results->scopes;
    if(!value.ok())throw std::runtime_error("owned get failed");
    {std::lock_guard lock(results->mutex);results->order.push_back(id);results->cv.notify_all();}
    co_return;
}
void OrderedReleaseAndDrain(Server& s){
    Results result;std::atomic<bool> current{true};
    std::atomic<bool> other_finished{false};
    tinyimx::BusinessExecutorOptions options;options.worker_threads=1;options.max_pending_tasks=8;
    options.stripe_count=4;options.per_stripe_queue_capacity=8;options.shutdown_timeout=10ms;
    BusinessExecutor executor(options);Check(executor.Start(),"one-worker executor starts");
    auto submit=[&](int id,std::uint64_t key){
        BusinessExecutor::TaskSpec task;task.request.ordering_key=key;task.cancellation_policy=tinyimx::BusinessCancellationPolicy::kMustRun;
        task.completion_still_valid=[&,id]{return id!=1||current.load();};
        task.dispatcher=[](auto fn){fn();};
        task.async_work=[&,id](const auto&,auto post,auto done){
            auto coroutine=Get(s.client.get(),id,&result);
            std::move(coroutine).Start(std::move(post),[](auto fn){const auto old=session_scope;session_scope=71;
                try{fn();}catch(...){session_scope=old;throw;}session_scope=old;},
                [&,id,done=std::move(done)](auto error)mutable{
                    done([&]{++result.replies;},error);
                    if(id==2){std::lock_guard lock(result.mutex);other_finished=true;result.cv.notify_all();}
                });
        };
        return executor.Submit(std::move(task));
    };
    Check(submit(1,1)==tinyimx::BusinessSubmitStatus::kAccepted,"first delayed RPC accepted");
    {std::unique_lock lock(s.service.mutex);Check(s.service.cv.wait_for(lock,2s,[&]{return s.service.entered;}),"real RPC is outstanding at server");}
    Check(submit(2,2)==tinyimx::BusinessSubmitStatus::kAccepted,"different stripe accepted while RPC waits");
    {std::unique_lock lock(result.mutex);Check(result.cv.wait_for(lock,2s,[&]{return result.order==std::vector<int>{2};}),"only worker serves other key before delayed RPC returns");}
    // Observing the business body is earlier than logical task completion.
    // Wait for its admission slot to be released before filling the other seven.
    {std::unique_lock lock(result.mutex);Check(result.cv.wait_for(lock,2s,[&]{return other_finished.load();}),"other key releases its logical admission slot before capacity boundary check");}
    for(int id=3;id<=9;++id)Check(submit(id,1)==tinyimx::BusinessSubmitStatus::kAccepted,"same-key tail accepted within bounded pending");
    Check(submit(10,3)==tinyimx::BusinessSubmitStatus::kOverloaded,"suspended logical tasks still consume admission budget");
    {std::lock_guard lock(result.mutex);Check(result.order==std::vector<int>{2},"same ordering stripe never overtakes outstanding RPC");}
    current=false;executor.BeginDrain();
    Check(submit(11,3)==tinyimx::BusinessSubmitStatus::kShuttingDown,"draining rejects new work");
    std::atomic<bool> stopped=false;bool within=true;
    std::thread shutdown([&]{within=executor.ShutdownGraceful();stopped=true;});
    std::this_thread::sleep_for(30ms);Check(!stopped,"shutdown budget cannot abandon accepted asynchronous work");
    {std::lock_guard lock(s.service.mutex);s.service.released=true;s.service.cv.notify_all();}
    shutdown.join();
    Check(!within&&stopped,"graceful drain reports exceeded budget after safe completion");
    Check(result.order==std::vector<int>({2,1,3,4,5,6,7,8,9}),"all accepted RPCs finish in same-key order");
    const auto stats=executor.GetStats();
    Check(result.replies==8&&stats.completion_dropped_total==1,"stale-session completion suppressed while durable work completes");
    Check(result.scopes==18&&result.destroyed==9,"session scope restored on every resume and frames destroyed once");
    Check(stats.current_pending_tasks==0&&stats.pending_completions==0,"logical pending and completions fully drained");
    // Fast loopback RPCs may complete inline before their starter returns.
    // The deliberately gated first RPC must require at least one queued resume.
    Check(stats.async_started_total==9&&stats.async_finished_total==9&&stats.current_async_tasks==0&&stats.continuation_run_total>=1,"async runtime proves started/finished/resume accounting without parked worker ownership");
}
tinyimx::BusinessCoroutine Inline(int* out){
    *out=co_await rpc::RpcAwaitable<int>(true,[]{return -1;},[](auto done){done(7);done(8);});co_return;
}
void InlineRace(){int value=0,completed=0;tinyimx::BusinessCoroutine::Post post=[](auto fn){fn();};
    auto task=Inline(&value);std::move(task).Start(post,{},[&](auto error){Check(!error,"inline RPC has no lifetime error");++completed;});
    Check(value==7&&completed==1,"inline and duplicate callbacks resume one frame exactly once");}
}
int main(){try{InlineRace();Server server;ClientContracts(server);OrderedReleaseAndDrain(server);}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}return failures?1:0;}
