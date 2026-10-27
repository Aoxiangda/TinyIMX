#include "services/message/service/MessageCallbackService.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace pb = tinyimx::message::v1;
int failed = 0;
void Check(bool value, const char* what) {
    std::cout << (value ? "PASS " : "FAIL ") << what << '\n';
    if (!value) ++failed;
}
class Probe final : public pb::MessageService::WithCallbackMethod_GetPrivateMessage<pb::MessageService::Service> {
public:
    tinyimx::message::BoundedUnaryRpcDispatcher dispatcher{1,1};
    std::mutex mutex;
    std::condition_variable cv;
    bool released{false};
    int entered{0}, callbacks_returned{0};
    std::atomic<int> mutations{0};
    grpc::ServerUnaryReactor* GetPrivateMessage(grpc::CallbackServerContext* c,
        const pb::GetPrivateMessageRequest* q, pb::GetPrivateMessageResponse* r) override {
        auto* reactor = dispatcher.Dispatch(c, [this,q,r] {
            if (q->message_id() == 1) {
                std::unique_lock lock(mutex); ++entered; cv.notify_all();
                if (!cv.wait_for(lock, 3s, [this] { return released; }))
                    return grpc::Status(grpc::StatusCode::INTERNAL,"probe gate watchdog");
            }
            if (q->message_id() == 2) ++mutations;
            if (q->message_id() == 3) throw std::runtime_error("owned test failure");
            r->mutable_record()->set_message_id(q->message_id());
            return grpc::Status::OK;
        });
        { std::lock_guard lock(mutex); ++callbacks_returned; cv.notify_all(); }
        return reactor;
    }
    bool Wait(int starts, int callbacks) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, 2s, [&] { return entered>=starts && callbacks_returned>=callbacks; });
    }
    void Release() { std::lock_guard lock(mutex); released=true; cv.notify_all(); }
};
struct Server {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<pb::MessageService::Stub> stub;
    explicit Server(grpc::Service* service) {
        grpc::ServerBuilder b; int port=0;
        b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);
        b.RegisterService(service); server=b.BuildAndStart();
        if (!server || port<=0) throw std::runtime_error("owned callback server failed");
        stub=pb::MessageService::NewStub(grpc::CreateChannel("127.0.0.1:"+std::to_string(port),grpc::InsecureChannelCredentials()));
    }
    ~Server() { server->Shutdown(); server->Wait(); }
    grpc::Status Get(std::uint64_t id, std::chrono::milliseconds timeout=4s) {
        grpc::ClientContext c; c.set_deadline(std::chrono::system_clock::now()+timeout);
        pb::GetPrivateMessageRequest q; q.set_message_id(id); pb::GetPrivateMessageResponse r;
        auto status=stub->GetPrivateMessage(&c,q,&r);
        if (status.ok() && r.record().message_id()!=id)
            return {grpc::StatusCode::DATA_LOSS,"probe response correlation"};
        return status;
    }
};
void CancelAndOverload() {
    Probe p; Server s(&p); grpc::Status first,second;
    std::thread a([&] { first=s.Get(1); });
    const bool entered=p.Wait(1,1);
    Check(entered,"bounded worker entered owned blocking call");
    std::thread b([&] { second=s.Get(2,100ms); });
    const bool queued=p.Wait(1,2);
    Check(queued,"second callback accepted to bounded queue without blocking reaction");
    const auto rejected=s.Get(4);
    Check(rejected.error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"full queue returns explicit resource exhausted");
    b.join(); Check(second.error_code()==grpc::StatusCode::DEADLINE_EXCEEDED,"queued call deadline observed by client");
    p.Release(); a.join();
    Check(first.ok(),"running call completes after queued cancellation");
    Check(s.Get(4).ok(),"queue recovers after cancel");
    Check(p.mutations==0,"cancelled queued request never starts mutation");
    Check(s.Get(3).error_code()==grpc::StatusCode::INTERNAL,"worker exception becomes RPC status");
    Check(s.Get(4).ok(),"worker survives exception and correlation remains valid");
    p.dispatcher.Drain();
    Check(s.Get(4).error_code()==grpc::StatusCode::RESOURCE_EXHAUSTED,"stopped dispatcher rejects explicitly");
}
void AcceptedDrain() {
    Probe p; Server s(&p); grpc::Status first,second;
    std::thread a([&] { first=s.Get(1); }); Check(p.Wait(1,1),"drain first admitted");
    std::thread b([&] { second=s.Get(2); }); Check(p.Wait(1,2),"drain queued call admitted");
    std::thread stopping([&] { s.server->Shutdown(); });
    p.Release(); a.join(); b.join(); stopping.join(); s.server->Wait(); p.dispatcher.Drain();
    Check(first.ok() && second.ok() && p.mutations==1,"graceful server drain preserves both accepted calls exactly once");
}
void CoreAndSyncFallback() {
    tinyimx::message::MessageCallbackService svc(nullptr,nullptr,1,2); Server s(&svc);
    Check(s.Get(4).error_code()==grpc::StatusCode::UNAVAILABLE,"real Get core failure mapping preserved");
    grpc::ClientContext a; pb::PersistPrivateMessageRequest q; pb::PersistPrivateMessageResponse r;
    Check(s.stub->PersistPrivateMessage(&a,q,&r).error_code()==grpc::StatusCode::UNAVAILABLE,"real Persist core failure mapping preserved");
    grpc::ClientContext b; pb::ConfirmReceiverRequest cq; pb::MessageMutationResponse cr;
    Check(s.stub->ConfirmReceiver(&b,cq,&cr).error_code()==grpc::StatusCode::UNAVAILABLE,"real Confirm core failure mapping preserved");
    grpc::ClientContext c; pb::ListHistoryRequest hq; pb::ListHistoryResponse hr;
    Check(s.stub->ListHistory(&c,hq,&hr).error_code()==grpc::StatusCode::UNAVAILABLE,"unmodified sync method remains registered");
    s.server->Shutdown(); s.server->Wait(); svc.Drain();
}
}
int main() {
    try {
        CancelAndOverload(); AcceptedDrain(); CoreAndSyncFallback();
        bool invalid=false;
        try { tinyimx::message::BoundedUnaryRpcDispatcher x(65,1); } catch (const std::invalid_argument&) { invalid=true; }
        Check(invalid,"worker bound validated before runtime allocation");
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 2; }
    return failed ? 1 : 0;
}
