// Diagnostic only: controlled wait plus synthetic echo, no domain storage.
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

// Mechanism prototype only; no production handlers or storage are linked.
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
                    ++completed_;
                }
            });
        } catch (...) { Stop(); throw; }
    }
    ~FixedWorkers() { Stop(); }
    bool Submit(std::function<void()> work) {
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || queue_.size() >= 512) { ++rejected_; return false; }
            queue_.push_back(std::move(work)); ++submitted_;
            peak_ = std::max(peak_, queue_.size());
        }
        ready_.notify_one(); return true;
    }
    Json Stats() {
        std::lock_guard lock(mutex_);
        return Json{{"fixed_workers", kWorkers}, {"pending_capacity", 512},
                    {"submitted", submitted_}, {"completed", completed_.load()},
                    {"rejected", rejected_}, {"peak_pending", peak_}, {"pending", queue_.size()}};
    }
private:
    void Stop() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_all();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
    }
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_{};
    std::size_t submitted_{}, rejected_{}, peak_{};
    std::atomic<std::size_t> completed_{};
};
class CallbackService final : public probe_message_proto::MessageService::CallbackService {
public:
    explicit CallbackService(Service& service) : service_(service) {}
    grpc::ServerUnaryReactor* PersistPrivateMessage(grpc::CallbackServerContext* context,
            const probe_message_proto::PersistPrivateMessageRequest* request,
            probe_message_proto::PersistPrivateMessageResponse* response) override {
        class Reactor final : public grpc::ServerUnaryReactor {
        public:
            Reactor(FixedWorkers& workers, Service& service, grpc::CallbackServerContext* context,
                    const probe_message_proto::PersistPrivateMessageRequest* request,
                    probe_message_proto::PersistPrivateMessageResponse* response) {
                if (!workers.Submit([this, &service, context, request, response] {
                    grpc::Status status;
                    try {
                        if (cancelled_.load() || context->IsCancelled())
                            status = {grpc::StatusCode::CANCELLED, "synthetic request cancelled"};
                        else if (context->deadline() <= std::chrono::system_clock::now())
                            status = {grpc::StatusCode::DEADLINE_EXCEEDED, "synthetic request expired"};
                        else {
                            status = service.PersistPrivateMessage(nullptr, request, response);
                            if (cancelled_.load() || context->IsCancelled())
                                status = {grpc::StatusCode::CANCELLED, "synthetic request cancelled"};
                        }
                    } catch (...) { status = {grpc::StatusCode::INTERNAL, "synthetic handler exception"}; }
                    // Finish is the last access: OnDone may reclaim this reactor.
                    Finish(std::move(status));
                })) Finish({grpc::StatusCode::RESOURCE_EXHAUSTED, "synthetic queue full"});
            }
            void OnCancel() override { cancelled_.store(true); }
            void OnDone() override { delete this; }
        private:
            std::atomic<bool> cancelled_{};
        };
        return new Reactor(workers_, service_, context, request, response);
    }
    Json Stats() { return workers_.Stats(); }
private:
    Service& service_;
    FixedWorkers workers_;
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
    if((mode!="direct"&&mode!="grpc"&&mode!="callback")||rate<1||rate>500||seconds<1||seconds>20) return 2;
    const std::size_t count=static_cast<std::size_t>(rate)*seconds;
    std::vector<Row> rows(count);Service service(rows);std::unique_ptr<CallbackService> callback;std::unique_ptr<grpc::Server> server;
    std::unique_ptr<probe_message_proto::MessageService::Stub> stub;std::shared_ptr<grpc::Channel> channel;
    if(mode!="direct") {
        if(mode=="callback") callback=std::make_unique<CallbackService>(service);
        grpc::ServerBuilder builder;int port=0;
        builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);
        builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::NUM_CQS,1);
        builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MIN_POLLERS,1);
        builder.SetSyncServerOption(grpc::ServerBuilder::SyncServerOption::MAX_POLLERS,2);
        builder.RegisterService(callback ? static_cast<grpc::Service*>(callback.get()) : static_cast<grpc::Service*>(&service));server=builder.BuildAndStart();
        if(!server||port<=0) return 3;
        channel=grpc::CreateChannel("127.0.0.1:"+std::to_string(port),grpc::InsecureChannelCredentials());
        if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(2))) return 4;
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
    output["callback_executor"] = callback ? callback->Stats() : Json(nullptr);
    std::cout<<output.dump()<<'\n';return output["status"]=="SYNTHETIC_COMPONENT_COMPLETE"?0:1;
 } catch(const std::exception& e) {std::cerr<<"Probe setup failed: "<<e.what()<<'\n';return 5;}
}
