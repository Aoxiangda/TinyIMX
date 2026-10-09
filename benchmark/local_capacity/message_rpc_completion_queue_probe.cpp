// Diagnostic only: controlled wait plus synthetic echo, no domain storage.
// CQ lifecycle follows https://grpc.io/docs/languages/cpp/async/ with separate tags.
// This normal mechanism probe does not certify full cancellation/overload/fault contracts.
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <condition_variable>
#include <deque>
#include <functional>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace {
namespace probe_message_proto = ::tinyimx::message::v1;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
constexpr int kWorkers = 16;
constexpr int kWaitMs = 20;
struct Row {
    std::int64_t caller_ns{}, handler_ns{}, handler_cpu_ns{}, late_ns{}, tid{};
    bool ok{};
};
std::int64_t ThreadCpu() {
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value)) return -1;
    return value.tv_sec * 1000000000LL + value.tv_nsec;
}
std::int64_t Micros(const timeval& value) { return value.tv_sec * 1000000LL + value.tv_usec; }
class Service final : public probe_message_proto::MessageService::Service {
public:
    explicit Service(std::vector<Row>& rows) : rows_(rows) {}
    grpc::Status PersistPrivateMessage(grpc::ServerContext*,
            const probe_message_proto::PersistPrivateMessageRequest* request,
            probe_message_proto::PersistPrivateMessageResponse* response) override {
        const auto started = Clock::now();
        const auto cpu = ThreadCpu();
        const auto index = std::stoull(request->client_message_id());
        if (index >= rows_.size()) return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "probe index");
        std::this_thread::sleep_for(std::chrono::milliseconds(kWaitMs));
        response->set_result(probe_message_proto::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED);
        response->set_message_id(index + 1);
        auto* record = response->mutable_record();
        record->set_message_id(index + 1);
        record->set_client_message_id(request->client_message_id());
        record->set_from_user_id(request->from_user_id());
        record->set_to_user_id(request->to_user_id());
        record->set_message_type(request->message_type());
        record->set_content(request->content());
        record->set_delivery_state(probe_message_proto::MESSAGE_DELIVERY_STATE_PENDING);
        // These are synthetic fields, never a durable acceptance assertion.
        record->set_created_at("synthetic-probe");
        const auto end_cpu = ThreadCpu();
        auto& row = rows_[index];
        row.handler_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
        row.handler_cpu_ns = cpu >= 0 && end_cpu >= cpu ? end_cpu-cpu : -1;
        row.tid = syscall(SYS_gettid);
        return grpc::Status::OK;
    }
private:
    std::vector<Row>& rows_;
};

// True CompletionQueue mechanism only. No domain/storage implementation linked.
// IsCancelled requires a done notification on async contexts; this normal-path
// mechanism does not use it or certify production cancellation contracts.
class FixedWorkers {
public:
    FixedWorkers() {
        try {
            for (int i = 0; i < kWorkers; ++i) workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> work;
                    {
                        std::unique_lock lock(mutex_);
                        ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                        if (queue_.empty()) return;
                        work = std::move(queue_.front()); queue_.pop_front();
                    }
                    work();
                    { std::lock_guard lock(mutex_); ++completed_; --pending_; }
                }
            });
        } catch (...) { Stop(); throw; }
    }
    ~FixedWorkers() { Stop(); }
    bool Submit(std::function<void()> work) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || pending_ >= 512) { ++rejected_; return false; }
            queue_.push_back(std::move(work)); ++submitted_; ++pending_;
            peak_ = std::max(peak_, pending_);
        }
        ready_.notify_one(); return true;
    }
    void Stop() {
        std::lock_guard lifecycle(lifecycle_mutex_);
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }
    Json Stats() {
        std::lock_guard lock(mutex_);
        return Json{{"fixed_workers", kWorkers}, {"pending_capacity", 512},
                    {"submitted", submitted_}, {"completed", completed_},
                    {"rejected", rejected_}, {"peak_pending", peak_},
                    {"pending", pending_}, {"queue", queue_.size()}};
    }
private:
    std::mutex mutex_, lifecycle_mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_{};
    std::size_t submitted_{}, completed_{}, rejected_{}, pending_{}, peak_{};
};
class CompletionQueueServer {
    struct Call;
    enum class Kind { kAccept, kFinish };
    struct Tag { Call* call; Kind kind; };
    struct Call {
        CompletionQueueServer& owner;
        grpc::ServerContext context;
        probe_message_proto::PersistPrivateMessageRequest request;
        probe_message_proto::PersistPrivateMessageResponse response;
        grpc::ServerAsyncResponseWriter<probe_message_proto::PersistPrivateMessageResponse> responder;
        Tag accepted{this,Kind::kAccept}, finished{this,Kind::kFinish};
        explicit Call(CompletionQueueServer& value) : owner(value),responder(&context) {
            ++owner.allocated_; ++owner.live_;
            owner.service_.RequestPersistPrivateMessage(&context,&request,&responder,
                owner.queue_.get(),owner.queue_.get(),&accepted);
        }
        ~Call() { ++owner.deleted_; --owner.live_; }
        void Finish(grpc::Status status) {
            // Finish completion owns reclamation; no access after enqueue.
            responder.Finish(response,status,&finished);
        }
        void Proceed(Kind kind,bool ok) {
            if (kind == Kind::kFinish) {
                ++owner.finished_; if (!ok) ++owner.finish_not_ok_;
                delete this; return;
            }
            if (!ok) { ++owner.cancelled_accepts_; delete this; return; }
            ++owner.accepted_;
            if (owner.stopping_.load()) {
                Finish({grpc::StatusCode::UNAVAILABLE,"synthetic server stopping"}); return;
            }
            try {
                new Call(owner);
                if (owner.workers_.Submit([this] {
                    grpc::Status status;
                    try {
                        if (context.deadline() <= std::chrono::system_clock::now())
                            status = {grpc::StatusCode::DEADLINE_EXCEEDED,"synthetic request expired"};
                        else {
                            status = owner.handler_.PersistPrivateMessage(nullptr,&request,&response);
                            if (context.deadline() <= std::chrono::system_clock::now())
                                status = {grpc::StatusCode::DEADLINE_EXCEEDED,"synthetic request expired after handler"};
                        }
                    } catch (...) { status = {grpc::StatusCode::INTERNAL,"synthetic handler exception"}; }
                    Finish(std::move(status));
                })) return;
                Finish({grpc::StatusCode::RESOURCE_EXHAUSTED,"synthetic pending bound"});
            } catch (...) { Finish({grpc::StatusCode::INTERNAL,"synthetic admission exception"}); }
        }
    };
public:
    explicit CompletionQueueServer(Service& handler) : handler_(handler) {
        grpc::ServerBuilder builder;
        builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port_);
        builder.RegisterService(&service_); queue_=builder.AddCompletionQueue();
        server_=builder.BuildAndStart();
        if (!server_ || port_<=0) throw std::runtime_error("CQ own server setup");
        try {
            new Call(*this);
            for (int i=0;i<2;++i) pollers_.emplace_back([this] {
                void* value=nullptr; bool ok=false;
                while (queue_->Next(&value,&ok)) {
                    auto* tag=static_cast<Tag*>(value);
                    tag->call->Proceed(tag->kind,ok);
                }
            });
        } catch (...) {
            // Construction failure is outside measured load. Ensure a poller
            // exists to reclaim initial accept tags during bounded shutdown.
            if (pollers_.empty()) {
                stopping_.store(true); server_->Shutdown(); workers_.Stop(); queue_->Shutdown();
                void* value=nullptr;bool ok=false;
                while(queue_->Next(&value,&ok)){auto* tag=static_cast<Tag*>(value);tag->call->Proceed(tag->kind,ok);}
            } else Stop();
            throw;
        }
    }
    ~CompletionQueueServer() { Stop(); }
    std::string Target() const { return "127.0.0.1:"+std::to_string(port_); }
    void Stop() {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) return;
        stopped_=true; stopping_.store(true);
        // Pollers keep consuming accept/finish tags while handlers drain.
        server_->Shutdown(std::chrono::system_clock::now()+std::chrono::seconds(3));
        workers_.Stop(); queue_->Shutdown();
        for (auto& poller:pollers_) if(poller.joinable()) poller.join();
        server_->Wait();
    }
    Json Stats() {
        auto stats=workers_.Stats();
        stats["cq_pollers"]=2;stats["accepted_calls"]=accepted_.load();
        stats["finish_completions"]=finished_.load();stats["finish_not_ok"]=finish_not_ok_.load();
        stats["cancelled_unbound_accepts"]=cancelled_accepts_.load();
        stats["allocated_call_objects"]=allocated_.load();stats["deleted_call_objects"]=deleted_.load();
        stats["live_call_objects"]=live_.load(); return stats;
    }
private:
    Service& handler_;
    probe_message_proto::MessageService::AsyncService service_;
    FixedWorkers workers_;
    std::unique_ptr<grpc::ServerCompletionQueue> queue_;
    std::unique_ptr<grpc::Server> server_;
    std::vector<std::thread> pollers_;
    std::mutex lifecycle_mutex_;
    std::atomic<bool> stopping_{false}; bool stopped_{false}; int port_{};
    std::atomic<std::size_t> allocated_{0},deleted_{0},live_{0},accepted_{0},finished_{0},finish_not_ok_{0},cancelled_accepts_{0};
};

Json Distribution(std::vector<std::int64_t> values) {
    if (values.empty()) return nullptr;
    std::sort(values.begin(),values.end());
    long double total=0;for(auto value:values) total+=value;
    const auto quantile=[&](int numerator){return values[(values.size()*numerator+99)/100-1]/1000000.0;};
    return Json{{"count",values.size()},{"mean_ms",static_cast<double>(total/values.size()/1000000)},
                {"p50_ms",quantile(50)},{"p95_ms",quantile(95)},{"p99_ms",quantile(99)},
                {"max_ms",values.back()/1000000.0}};
}
} // namespace
int main(int argc,char** argv) {
 try {
    if(argc!=4) return 2;
    const std::string mode=argv[1];const int rate=std::stoi(argv[2]),seconds=std::stoi(argv[3]);
    if((mode!="direct"&&mode!="grpc"&&mode!="cq")||rate<1||rate>500||seconds<1||seconds>20) return 2;
    const std::size_t count=static_cast<std::size_t>(rate)*seconds;
    std::vector<Row> rows(count);Service service(rows);std::unique_ptr<CompletionQueueServer> cq_server;std::unique_ptr<grpc::Server> server;
    std::unique_ptr<probe_message_proto::MessageService::Stub> stub;std::shared_ptr<grpc::Channel> channel;
    if(mode!="direct") {
        std::string target;
        if(mode=="cq") {
            cq_server=std::make_unique<CompletionQueueServer>(service);target=cq_server->Target();
        } else {
            grpc::ServerBuilder builder;int port=0;
            builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);
            builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS,1);
            builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS,1);
            builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS,2);
            builder.RegisterService(&service);server=builder.BuildAndStart();
            if(!server||port<=0)return 3;target="127.0.0.1:"+std::to_string(port);
        }
        channel=grpc::CreateChannel(target,grpc::InsecureChannelCredentials());
        if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(2)))return 4;
        stub=probe_message_proto::MessageService::NewStub(channel);
    }

    std::atomic<std::size_t> next{0};std::atomic<int> exceptions{0};
    std::barrier ready(kWorkers+1);Clock::time_point origin;std::vector<std::thread> clients;
    for(int worker=0;worker<kWorkers;++worker) clients.emplace_back([&] {
        ready.arrive_and_wait();
        while(true) {
            const auto index=next.fetch_add(1);if(index>=count) break;
            try {
                const auto due=origin+std::chrono::nanoseconds(index*1000000000LL/rate);
                std::this_thread::sleep_until(due);
                probe_message_proto::PersistPrivateMessageRequest request;probe_message_proto::PersistPrivateMessageResponse response;
                request.set_from_user_id(10001);request.set_to_user_id(10002);
                request.set_client_message_id(std::to_string(index));request.set_message_type(1);request.set_content(std::string(128,'x'));
                const auto started=Clock::now();rows[index].late_ns=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::nanoseconds>(started-due).count());
                grpc::Status result;
                if(mode!="direct") {
                    grpc::ClientContext context;context.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
                    result=stub->PersistPrivateMessage(&context,request,&response);
                } else result=service.PersistPrivateMessage(nullptr,&request,&response);
                rows[index].caller_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-started).count();
                rows[index].ok=result.ok()&&response.result()==probe_message_proto::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED&&
                    response.message_id()==index+1&&response.record().message_id()==index+1&&
                    response.record().client_message_id()==request.client_message_id()&&
                    response.record().from_user_id()==10001&&response.record().to_user_id()==10002&&
                    response.record().message_type()==1&&response.record().content()==request.content()&&
                    response.record().delivery_state()==probe_message_proto::MESSAGE_DELIVERY_STATE_PENDING;
            } catch(...) {++exceptions;}
        }
    });
    rusage before{},after{};getrusage(RUSAGE_SELF,&before);origin=Clock::now();ready.arrive_and_wait();
    for(auto& client:clients) client.join();const auto ended=Clock::now();getrusage(RUSAGE_SELF,&after);
    if(server){server->Shutdown();server->Wait();}
    if(cq_server)cq_server->Stop();
    const double elapsed=std::chrono::duration<double>(ended-origin).count();
    std::unordered_set<std::int64_t> tids;std::vector<std::int64_t> caller,handler,cpu,extra,late;Json raw=Json::array();int valid=0,negative_extra=0;
    for(std::size_t i=0;i<count;++i) {
        const auto& row=rows[i];valid+=row.ok;caller.push_back(row.caller_ns);handler.push_back(row.handler_ns);late.push_back(row.late_ns);
        if(row.handler_cpu_ns>=0) cpu.push_back(row.handler_cpu_ns);if(row.tid>0) tids.insert(row.tid);
        if(row.ok&&row.handler_ns>0){extra.push_back(row.caller_ns-row.handler_ns);negative_extra+=extra.back()<0;}
        raw.push_back({i,row.ok,row.caller_ns,row.handler_ns,row.handler_cpu_ns,row.late_ns,row.tid});
    }
    const auto user=Micros(after.ru_utime)-Micros(before.ru_utime),system=Micros(after.ru_stime)-Micros(before.ru_stime);
    Json output{{"status",valid==static_cast<int>(count)&&!exceptions&&negative_extra==0?"SYNTHETIC_COMPONENT_COMPLETE":"FAIL"},
        {"mode",mode},{"planned",count},{"synthetic_result_ok",valid},{"exceptions",exceptions.load()},
        {"rate_per_second",rate},{"nominal_seconds",seconds},{"elapsed_seconds",elapsed},{"workers",kWorkers},{"controlled_handler_wait_ms",kWaitMs},
        {"handler_distinct_tids",tids.size()},{"caller_wall",Distribution(caller)},{"handler_wall",Distribution(handler)},
        {"handler_thread_cpu",Distribution(cpu)},{"matched_caller_minus_handler_wall",Distribution(extra)},
        {"negative_matched_extra",negative_extra},{"scheduled_lateness",Distribution(late)},
        {"process_user_cpu_seconds",user/1000000.0},{"process_system_cpu_seconds",system/1000000.0},
        {"process_mean_cpu_cores",(user+system)/1000000.0/elapsed},
        {"voluntary_context_switches",after.ru_nvcsw-before.ru_nvcsw},{"involuntary_context_switches",after.ru_nivcsw-before.ru_nivcsw},
        {"raw_columns",{"index","synthetic_ok","caller_ns","handler_ns","handler_cpu_ns","scheduled_late_ns","handler_tid"}},{"raw",std::move(raw)},
        {"limits","Own process caller+server CPU, controlled20ms sleep andecho. No MySQL/Redis/authorization/persistence/receiver/wire/10khold. Not production latency or capacity acceptance. Initial client/framework startup excluded from rusage but tracer counts include startup/shutdown; sequentialsharedhost anddiagnosticclocks may perturb."},
        {"performance_acceptance",false}};
    output["cq_executor"] = cq_server ? cq_server->Stats() : Json(nullptr);
    std::cout<<output.dump()<<'\n';return output["status"]=="SYNTHETIC_COMPONENT_COMPLETE"?0:1;
 } catch(const std::exception& e) {std::cerr<<"Probe setup failed: "<<e.what()<<'\n';return 5;}
}
