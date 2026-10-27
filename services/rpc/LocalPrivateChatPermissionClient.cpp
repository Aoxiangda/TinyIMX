#include "services/rpc/LocalPrivateChatPermissionClient.h"

#include <chrono>
#include <algorithm>
#include <future>
#include <memory>
#include <stdexcept>
#include <utility>
#include <condition_variable>
#include <map>
#include <optional>
#include <thread>

namespace tinyimx::rpc {
namespace {
using Result = RpcResult<CheckPrivateChatPermissionRpcResponse>;
using Clock = std::chrono::steady_clock;

ThreadPoolOptions Options(std::size_t workers, std::size_t queued) {
    if (!workers || workers > 32 || !queued || queued > 256)
        throw std::invalid_argument("local permission workers must be1..32 and queue1..256");
    ThreadPoolOptions out;
    out.name = "local-private-permission";
    out.worker_threads = workers;
    out.queue_capacity = queued;
    out.enable_dynamic_resize = false;
    out.queue_full_policy = QueueFullPolicy::kDiscard;
    return out;
}

Result Expired() {
    return Result::Failure(RpcErrorCode::kDeadlineExceeded,
                           "CheckPrivateChatPermission remaining RPC budget is exhausted");
}

Result Map(social::ChatPermissionApplicationResult result) {
    using A = social::ChatPermissionApplicationStatus;
    ChatPermissionRpcOutcome outcome;
    switch (result.status) {
        case A::kAllowed: outcome=ChatPermissionRpcOutcome::kAllowed; break;
        case A::kInvalidArgument: outcome=ChatPermissionRpcOutcome::kInvalidArgument; break;
        case A::kNotFriend: outcome=ChatPermissionRpcOutcome::kNotFriend; break;
        case A::kBlockedBySelf: outcome=ChatPermissionRpcOutcome::kBlockedBySelf; break;
        case A::kBlockedByPeer: outcome=ChatPermissionRpcOutcome::kBlockedByPeer; break;
        case A::kStorageError: outcome=ChatPermissionRpcOutcome::kStorageError; break;
        default: return Result::Failure(RpcErrorCode::kDataLoss,
                                        "local permission returned unknown application status");
    }
    return Result::Success({outcome,std::move(result.message)});
}

struct Call {
    std::promise<Result> result;
    std::atomic<bool> abandoned{false};
};
}  // namespace

class LocalPrivateChatPermissionClient::AsyncState {
public:
    struct Call {
        Completion completion;
        Clock::time_point deadline;
        std::uint64_t serial{0};
        std::atomic<bool> abandoned{false};
        bool resolved{false},accepted{false},done{false},registered{false};
        std::optional<Result> early_result;
    };
    struct Delivery {
        Completion completion;
        Result result;
        explicit operator bool()const noexcept{return bool(completion);}
    };
    explicit AsyncState(std::size_t limit):capacity(limit),timer([this]{RunTimer();}){}
    ~AsyncState(){Stop();}
    bool Reserve(const std::shared_ptr<Call>& call){
        std::lock_guard lock(mutex);
        if(stopping||reserved>=capacity)return false;
        call->serial=++serial;++reserved;
        peak=std::max(peak,reserved);return true;
    }
    // Called under the outer admission fence. Return a continuation to invoke
    // only after that fence is released. A worker may finish before TrySubmit
    // returns, but cannot publish success before admission itself is resolved.
    Delivery Resolve(const std::shared_ptr<Call>& call,bool accepted,Result rejection){
        std::lock_guard lock(mutex);call->resolved=true;call->accepted=accepted;
        if(!accepted){call->abandoned.store(true);return Finish(call,std::move(rejection));}
        ++started;
        if(Clock::now()>=call->deadline){++deadlines;call->abandoned.store(true);return Finish(call,Expired());}
        if(call->early_result)return Finish(call,std::move(*call->early_result));
        try{waiting.emplace(std::make_pair(call->deadline,call->serial),call);call->registered=true;}
        catch(...){call->abandoned.store(true);return Finish(call,Result::Failure(RpcErrorCode::kInternal,"local permission timer registration failed"));}
        changed.notify_all();return {};
    }
    void Complete(const std::shared_ptr<Call>& call,Result result){
        Delivery delivery;
        {
            std::lock_guard lock(mutex);
            if(call->done)return;
            if(!call->resolved){call->early_result.emplace(std::move(result));return;}
            if(Clock::now()>=call->deadline){++deadlines;call->abandoned.store(true);result=Expired();}
            delivery=Finish(call,std::move(result));
        }
        Dispatch(std::move(delivery));
    }
    void Dispatch(Delivery delivery)noexcept{
        if(!delivery)return;
        try{delivery.completion(std::move(delivery.result));}catch(...){/* A continuation cannot strand the timer or another accepted read. */}
        {std::lock_guard lock(mutex);--callbacks_active;}changed.notify_all();
    }
    LocalPrivatePermissionAsyncStats Stats(){
        std::lock_guard lock(mutex);return {started,finished,reserved,peak,deadlines};
    }
    void Stop(){
        {
            std::unique_lock lock(mutex);
            // The outer owner first joins the SQL pool. Also include callbacks
            // dispatched by API callers during an admission/shutdown race.
            changed.wait(lock,[&]{return reserved==0&&callbacks_active==0;});
            stopping=true;
        }
        changed.notify_all();if(timer.joinable())timer.join();
    }
private:
    Delivery Finish(const std::shared_ptr<Call>& call,Result result){
        if(call->done)return {};
        call->done=true;
        if(call->registered){waiting.erase({call->deadline,call->serial});call->registered=false;}
        --reserved;if(call->accepted)++finished;++callbacks_active;
        Delivery out{std::move(call->completion),std::move(result)};
        changed.notify_all();return out;
    }
    void RunTimer()noexcept{
        std::unique_lock lock(mutex);
        for(;;){
            if(stopping&&waiting.empty())return;
            if(waiting.empty()){changed.wait(lock);continue;}
            const auto deadline=waiting.begin()->first.first;
            if(Clock::now()<deadline){changed.wait_until(lock,deadline);continue;}
            auto call=waiting.begin()->second;
            ++deadlines;call->abandoned.store(true);
            auto delivery=Finish(call,Expired());
            lock.unlock();Dispatch(std::move(delivery));lock.lock();
        }
    }
    const std::size_t capacity;
    std::mutex mutex;std::condition_variable changed;
    std::map<std::pair<Clock::time_point,std::uint64_t>,std::shared_ptr<Call>> waiting;
    std::uint64_t serial{0},reserved{0},peak{0},started{0},finished{0},deadlines{0},callbacks_active{0};
    bool stopping{false};
    std::thread timer;
};

LocalPrivateChatPermissionClient::LocalPrivateChatPermissionClient(
    social::FriendApplicationService* application, std::size_t workers, std::size_t queued)
    : application_(application), pool_(Options(workers,queued)),async_capacity_(workers+queued) {
    if (!application_) throw std::invalid_argument("local permission application is unavailable");
    if (!pool_.Start()) throw std::runtime_error("local permission workers failed to start");
}

void LocalPrivateChatPermissionClient::CheckPrivateChatPermissionAsync(
    const CheckPrivateChatPermissionRpcRequest& request,const RpcCallOptions& options,Completion done){
    if(!done)return;
    const auto invoke=[](Completion cb,Result result)noexcept{try{cb(std::move(result));}catch(...){}};
    if(!request.from_user_id||!request.to_user_id||request.from_user_id==request.to_user_id){
        invoke(std::move(done),Result::Failure(RpcErrorCode::kInvalidArgument,"invalid CheckPrivateChatPermission request"));return;
    }
    if(options.remaining_timeout<=std::chrono::milliseconds::zero()){invoke(std::move(done),Expired());return;}
    const auto start=Clock::now();
    const auto maximum=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max()-start);
    const auto deadline=options.remaining_timeout>=maximum?Clock::time_point::max():start+options.remaining_timeout;
    auto call=std::make_shared<AsyncState::Call>();call->completion=std::move(done);call->deadline=deadline;
    AsyncState* state=nullptr;AsyncState::Delivery delivery;std::optional<Result> immediate;
    {
        std::lock_guard lock(admission_);
        if(!accepting_)immediate=Result::Failure(RpcErrorCode::kUnavailable,"local permission is draining");
        else{
            try{if(!async_)async_=std::make_unique<AsyncState>(async_capacity_);state=async_.get();}
            catch(...){immediate=Result::Failure(RpcErrorCode::kInternal,"local permission timer startup failed");}
            if(!immediate){
                if(!state->Reserve(call)){
                    ++rejected_;immediate=Result::Failure(RpcErrorCode::kResourceExhausted,"local permission pending capacity reached");
                }else{
                    TaskPushResult pushed=TaskPushResult::kStopped;std::optional<Result> failure;
                    try{pushed=pool_.TrySubmit([this,state,call,request,deadline]{
                        Result result;
                        if(call->abandoned.load()||Clock::now()>=deadline){++expired_before_;result=Expired();}
                        else{
                            ++executed_;
                            try{result=Map(application_->CheckPrivateChatPermission(request.from_user_id,request.to_user_id));}
                            catch(...){result=Result::Failure(RpcErrorCode::kInternal,"local permission execution failed");}
                            if(call->abandoned.load()||Clock::now()>=deadline){++expired_after_;result=Expired();}
                        }
                        state->Complete(call,std::move(result));
                    });}
                    catch(...){failure=Result::Failure(RpcErrorCode::kInternal,"local permission submission failed");}
                    if(pushed==TaskPushResult::kOk&&!failure){
                        ++submitted_;delivery=state->Resolve(call,true,Result{});
                    }else{
                        ++rejected_;call->abandoned.store(true);
                        delivery=state->Resolve(call,false,failure?std::move(*failure):Result::Failure(
                            pushed==TaskPushResult::kStopped?RpcErrorCode::kUnavailable:RpcErrorCode::kResourceExhausted,
                            "local permission queue is full or stopped"));
                    }
                }
            }
        }
    }
    if(immediate)invoke(std::move(call->completion),std::move(*immediate));
    else state->Dispatch(std::move(delivery));
}

LocalPrivateChatPermissionClient::~LocalPrivateChatPermissionClient() { Drain(); }

Result LocalPrivateChatPermissionClient::CheckPrivateChatPermission(
    const CheckPrivateChatPermissionRpcRequest& request,const RpcCallOptions& options) {
    if (!request.from_user_id || !request.to_user_id || request.from_user_id==request.to_user_id)
        return Result::Failure(RpcErrorCode::kInvalidArgument,"invalid CheckPrivateChatPermission request");
    if (options.remaining_timeout <= std::chrono::milliseconds::zero()) return Expired();
    const auto start=Clock::now();
    const auto maximum=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max()-start);
    const auto deadline=options.remaining_timeout>=maximum ? Clock::time_point::max()
                                                          : start+options.remaining_timeout;
    auto call=std::make_shared<Call>();
    auto result=call->result.get_future();
    TaskPushResult pushed;
    {
        std::lock_guard lock(admission_);
        if (!accepting_) {
            rejected_.fetch_add(1,std::memory_order_relaxed);
            return Result::Failure(RpcErrorCode::kUnavailable,"local permission admission stopped");
        }
        // The accepted closure owns all request/result state. In particular,
        // no stack promise, request, or callback is captured by reference.
        try {
            pushed=pool_.TrySubmit([this,call,request,deadline] {
                Result value;
                if (call->abandoned.load(std::memory_order_acquire) || Clock::now()>=deadline) {
                    expired_before_.fetch_add(1,std::memory_order_relaxed);
                    value=Expired();
                } else {
                    executed_.fetch_add(1,std::memory_order_relaxed);
                    try {
                        value=Map(application_->CheckPrivateChatPermission(
                            request.from_user_id,request.to_user_id));
                    } catch (...) {
                        value=Result::Failure(RpcErrorCode::kInternal,"local permission execution failed");
                    }
                    if (call->abandoned.load(std::memory_order_acquire) || Clock::now()>=deadline) {
                        expired_after_.fetch_add(1,std::memory_order_relaxed);
                        value=Expired();
                    }
                }
                call->result.set_value(std::move(value));
            });
        } catch (...) {
            // TrySubmit telemetry may throw after enqueue; shared ownership
            // and abandoned=true keep that possible accepted read safe too.
            call->abandoned.store(true,std::memory_order_release);
            rejected_.fetch_add(1,std::memory_order_relaxed);
            return Result::Failure(RpcErrorCode::kInternal,"local permission submission failed");
        }
    }
    if (pushed!=TaskPushResult::kOk) {
        rejected_.fetch_add(1,std::memory_order_relaxed);
        return Result::Failure(pushed==TaskPushResult::kStopped?RpcErrorCode::kUnavailable:
                               RpcErrorCode::kResourceExhausted,"local permission queue is full or stopped");
    }
    submitted_.fetch_add(1,std::memory_order_relaxed);
    if (result.wait_until(deadline)!=std::future_status::ready || Clock::now()>=deadline) {
        call->abandoned.store(true,std::memory_order_release);
        return Expired();
    }
    try {
        return result.get();
    } catch (...) {
        return Result::Failure(RpcErrorCode::kInternal,"local permission result unavailable");
    }
}

void LocalPrivateChatPermissionClient::Drain() {
    std::call_once(drain_,[this] {
        { std::lock_guard lock(admission_); accepting_=false; }
        pool_.Shutdown(ShutdownMode::kGraceful);
        if(async_)async_->Stop();
    });
}

LocalPrivatePermissionAsyncStats LocalPrivateChatPermissionClient::AsyncStats()const{
    std::lock_guard lock(admission_);return async_?async_->Stats():LocalPrivatePermissionAsyncStats{};
}

LocalPrivateChatPermissionStats LocalPrivateChatPermissionClient::Stats() const noexcept {
    return {submitted_.load(std::memory_order_relaxed),executed_.load(std::memory_order_relaxed),
            rejected_.load(std::memory_order_relaxed),expired_before_.load(std::memory_order_relaxed),
            expired_after_.load(std::memory_order_relaxed)};
}
}  // namespace tinyimx::rpc
