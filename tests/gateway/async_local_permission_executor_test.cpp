#include "gateway/business/BusinessExecutor.h"
#include "services/rpc/LocalPrivateChatPermissionClient.h"
#include "services/rpc/RpcAwaitable.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
using namespace std::chrono_literals;
namespace {
namespace rpc=tinyimx::rpc;namespace social=tinyimx::social;
unsigned checks=0;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);++checks;}
class Repository final:public social::FriendRepositoryPort {
public:
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;std::atomic<unsigned> calls{0};
    social::ChatPermissionApplicationResult CheckPrivateChatPermission(std::uint64_t from,std::uint64_t)override{
        ++calls;
        if(from==500001){std::unique_lock lock(mutex);entered=true;cv.notify_all();if(!cv.wait_for(lock,2s,[&]{return released;}))throw std::runtime_error("owned permission gate timeout");}
        return{social::ChatPermissionApplicationStatus::kAllowed,"authoritative"};
    }
    social::FriendRepositoryListResult ListFriends(std::uint64_t,std::size_t)override{throw std::logic_error("unexpected list");}
};
struct Results{std::mutex mutex;std::condition_variable cv;std::vector<int> durable_markers;std::atomic<int> replies{0},destroyed{0};bool other_done=false;};
tinyimx::BusinessCoroutine AuthorizeAndCommit(rpc::PrivateChatPermissionPort* client,int id,Results* results,bool async=true){
    struct Lifetime{Results* r;~Lifetime(){++r->destroyed;}} lifetime{results};
    rpc::RpcCallOptions options;options.remaining_timeout=2s;
    options.request_id="owned-permission-metadata-longer-than-small-string-storage";
    options.trace_id="owned-authoritative-local-permission-continuation";
    // Build closures in an ordinary factory: GCC11 mishandles nontrivial
    // lambda temporaries written directly in the co_await expression.
    auto make_pending=[client,id,options,async]{
        return rpc::RpcAwaitable<rpc::RpcResult<rpc::CheckPrivateChatPermissionRpcResponse>>(async,
            [client,id,options]{return client->CheckPrivateChatPermission({id==1?500001ULL:500003ULL,id==1?500002ULL:500004ULL},options);},
            [client,id,options](auto done){client->CheckPrivateChatPermissionAsync(
                {id==1?500001ULL:500003ULL,id==1?500002ULL:500004ULL},options,std::move(done));});
    };
    auto permission=co_await make_pending();
    if(!permission.ok()||!permission.value->Allowed())throw std::runtime_error("owned authorization failed");
    {std::lock_guard lock(results->mutex);results->durable_markers.push_back(id);results->cv.notify_all();}
    co_return;
}
void OneWorkerComparison(){
    for(bool async:{false,true}){
        Repository repo;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient permission(&app,2,8);Results result;
        tinyimx::BusinessExecutorOptions options;options.worker_threads=1;options.max_pending_tasks=8;options.stripe_count=4;options.per_stripe_queue_capacity=8;options.shutdown_timeout=2s;
        tinyimx::BusinessExecutor executor(options);Check(executor.Start(),"single worker comparison starts");
        auto submit=[&](int id){tinyimx::BusinessExecutor::TaskSpec task;task.request.ordering_key=id;task.cancellation_policy=tinyimx::BusinessCancellationPolicy::kMustRun;task.dispatcher=[](auto fn){fn();};
            task.async_work=[&,id](const auto&,auto post,auto done){auto coroutine=AuthorizeAndCommit(&permission,id,&result,async);
                std::move(coroutine).Start(std::move(post),{},[&,id,done=std::move(done)](auto error)mutable{done([&]{++result.replies;},error);
                    if(id==2){std::lock_guard lock(result.mutex);result.other_done=true;result.cv.notify_all();}});};
            return executor.Submit(std::move(task));};
        Check(submit(1)==tinyimx::BusinessSubmitStatus::kAccepted,"comparison delayed request accepted");
        {std::unique_lock lock(repo.mutex);Check(repo.cv.wait_for(lock,2s,[&]{return repo.entered;}),"comparison same authoritative read is blocked");}
        Check(submit(2)==tinyimx::BusinessSubmitStatus::kAccepted,"comparison second key accepted");
        bool other_before_release=false;
        {std::unique_lock lock(result.mutex);other_before_release=result.cv.wait_for(lock,async?2s:100ms,[&]{return result.other_done;});}
        Check(other_before_release==async,"only async variant serves second key while same SQL read remains held");
        {std::lock_guard lock(repo.mutex);repo.released=true;}repo.cv.notify_all();
        Check(executor.ShutdownGraceful(),"both variants safely drain after same read release");permission.Drain();
        Check(result.durable_markers==(async?std::vector<int>{2,1}:std::vector<int>{1,2})&&result.replies==2&&repo.calls==2,"both variants preserve two authority reads two results and accepted continuations");
        std::cout<<"ONE_WORKER_LOCAL_PERMISSION_CAUSAL async="<<async<<" other_completed_before_sql_release="<<other_before_release<<" authority_reads="<<repo.calls.load()<<" final_success=2/2\n";
    }
}
}
int main()try{
    OneWorkerComparison();
    Repository repo;social::FriendApplicationService app(&repo);rpc::LocalPrivateChatPermissionClient permission(&app,2,8);Results result;std::atomic<bool> current{true};
    tinyimx::BusinessExecutorOptions options;options.worker_threads=1;options.max_pending_tasks=8;options.stripe_count=4;options.per_stripe_queue_capacity=8;options.shutdown_timeout=10ms;
    tinyimx::BusinessExecutor executor(options);Check(executor.Start(),"one business worker starts");
    auto submit=[&](int id,std::uint64_t key){
        tinyimx::BusinessExecutor::TaskSpec task;task.request.ordering_key=key;task.cancellation_policy=tinyimx::BusinessCancellationPolicy::kMustRun;
        task.completion_still_valid=[&,id]{return id!=1||current.load();};task.dispatcher=[](auto fn){fn();};
        task.async_work=[&,id](const auto&,auto post,auto done){auto coroutine=AuthorizeAndCommit(&permission,id,&result);
            std::move(coroutine).Start(std::move(post),{},[&,id,done=std::move(done)](auto error)mutable{
                done([&]{++result.replies;},error);
                if(id==2){std::lock_guard lock(result.mutex);result.other_done=true;result.cv.notify_all();}
            });};
        return executor.Submit(std::move(task));
    };
    Check(submit(1,1)==tinyimx::BusinessSubmitStatus::kAccepted,"first local SQL authorization task accepted");
    {std::unique_lock lock(repo.mutex);Check(repo.cv.wait_for(lock,2s,[&]{return repo.entered;}),"actual local permission worker is blocked in repository");}
    Check(submit(2,2)==tinyimx::BusinessSubmitStatus::kAccepted,"different ordering key accepted");
    {std::unique_lock lock(result.mutex);Check(result.cv.wait_for(lock,2s,[&]{return result.other_done;}),"single business worker completes another permission before blocked SQL returns");Check(result.durable_markers==std::vector<int>{2},"blocked key has not committed or overtaken");}
    for(int id=3;id<=9;++id)Check(submit(id,1)==tinyimx::BusinessSubmitStatus::kAccepted,"same-key tail accepted within original admission budget");
    Check(submit(10,3)==tinyimx::BusinessSubmitStatus::kOverloaded,"suspended authorization still consumes admission budget");
    current=false;executor.BeginDrain();Check(submit(11,3)==tinyimx::BusinessSubmitStatus::kShuttingDown,"drain fences new work");
    auto stopping=std::async(std::launch::async,[&]{return executor.ShutdownGraceful();});
    Check(stopping.wait_for(30ms)==std::future_status::timeout,"shutdown does not abandon accepted MustRun authorization continuations");
    {std::lock_guard lock(repo.mutex);repo.released=true;}repo.cv.notify_all();
    Check(!stopping.get(),"budget overrun is reported after safe completion");permission.Drain();
    Check(result.durable_markers==std::vector<int>({2,1,3,4,5,6,7,8,9}),"all accepted business continuations finish in original same-key order");
    const auto stats=executor.GetStats();Check(result.replies==8&&stats.completion_dropped_total==1,"stale session response suppressed while accepted work finishes");
    Check(result.destroyed==9&&stats.async_started_total==9&&stats.async_finished_total==9,"all coroutine frames and async tasks finish once");
    Check(stats.current_pending_tasks==0&&stats.pending_completions==0&&stats.current_async_tasks==0,"all logical and completion budgets drain");
    const auto p=permission.AsyncStats();Check(p.started==9&&p.finished==9&&p.pending==0,"local SQL callbacks drain before provider destruction");
    std::cout<<"ASYNC_LOCAL_PERMISSION_EXECUTOR_CONTRACTS_PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"ASYNC_LOCAL_PERMISSION_EXECUTOR_CONTRACTS_FAIL "<<e.what()<<'\n';return 1;}
