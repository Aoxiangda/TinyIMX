#pragma once

#include "common/concurrency/ThreadPool.h"
#include "common/observability/RpcExecutionTrace.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/support/server_callback.h>
#include <functional>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace tinyimx::message {

// Experimental callback admission. Blocking SQL stays off gRPC reaction threads.
// No detached thread per call; queue and workers are both bounded.
class BoundedUnaryRpcDispatcher final {
public:
    BoundedUnaryRpcDispatcher(std::size_t workers, std::size_t queued)
        : pool_(Options(workers, queued)) {
        if (!pool_.Start()) throw std::runtime_error("callback RPC workers failed to start");
    }
    ~BoundedUnaryRpcDispatcher() { Drain(); }

    grpc::ServerUnaryReactor* Dispatch(grpc::CallbackServerContext* context,
                                       std::function<grpc::Status()> work,
                                       int method=0, std::uint64_t mid=0,
                                       std::uint64_t from=0, std::uint64_t to=0,
                                       std::function<std::uint64_t()> final_mid={}) {
        // TrySubmit performs telemetry after enqueuing. If telemetry throws,
        // claim ownership atomically instead of finishing an already-running call.
        auto admission = std::make_shared<std::atomic<int>>(0);
        auto* reactor = new Reactor(context, std::move(work), method,mid,from,to,std::move(final_mid));
        const auto reject = [&] {
            int pending = 0;
            if (admission->compare_exchange_strong(pending, 2))
                reactor->Reject(grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                            "bounded callback RPC queue unavailable"));
        };
        try {
            if (pool_.TrySubmit([reactor,admission] {
                    int pending = 0;
                    if (admission->compare_exchange_strong(pending, 1)) reactor->Run();
                }) == TaskPushResult::kOk)
                return reactor;
        } catch (...) {
            reject();
            return reactor;
        }
        reject();
        return reactor;
    }

    // Caller first stops/waits for the server, then drains before DB teardown.
    void Drain() {
        std::call_once(drain_, [this] { pool_.Shutdown(ShutdownMode::kGraceful); });
    }

private:
    static ThreadPoolOptions Options(std::size_t workers, std::size_t queued) {
        if (workers < 1 || workers > 64 || queued < 1 || queued > 512)
            throw std::invalid_argument("invalid bounded callback RPC capacity");
        ThreadPoolOptions o;
        o.name = "message-callback-rpc";
        o.worker_threads = workers;
        o.queue_capacity = queued;
        o.queue_full_policy = QueueFullPolicy::kDiscard;
        o.enable_dynamic_resize = false;
        return o;
    }
    class Reactor final : public grpc::ServerUnaryReactor {
    public:
        Reactor(grpc::CallbackServerContext* context, std::function<grpc::Status()> work,
                int method, std::uint64_t mid, std::uint64_t from, std::uint64_t to,
                std::function<std::uint64_t()> final_mid)
            : context_(context), work_(std::move(work)),final_mid_(std::move(final_mid)),
              trace_(method,true,mid,from,to,method>0 && diagnostics::RpcExecutionTrace::Enabled()) {}
        void Reject(const grpc::Status& result) {
            trace_.Finish(static_cast<int>(result.error_code()),trace_.Current().mid);
            Finish(result);
        }
        void Run() noexcept {
            trace_.Start();
            grpc::Status result;
            try {
                // Cancellation before starting must not enter a transaction.
                // Once the existing core starts, its durable outcome semantics apply.
                // Client deadline completion can precede delivery of the server's
                // cancellation notification. Inspect the immutable deadline too,
                // before a queued call is allowed to enter the existing core.
                if (context_->deadline() <= std::chrono::system_clock::now())
                    result = grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED,
                                          "callback RPC expired before execution");
                else if (context_->IsCancelled())
                    result = grpc::Status(grpc::StatusCode::CANCELLED,
                                          "callback RPC cancelled before execution");
                else result = work_();
            } catch (...) {
                result = grpc::Status(grpc::StatusCode::INTERNAL,
                                      "callback RPC execution failed");
            }
            auto mid=trace_.Current().mid;
            try { if(final_mid_)mid=final_mid_(); }catch(...){ /* Trace only. */ }
            trace_.Finish(static_cast<int>(result.error_code()),mid);
            final_mid_={};
            work_ = {}; // release request captures before Finish can enable OnDone
            Finish(result); // no access to this/context/request after Finish
        }
    private:
        void OnDone() override { delete this; }
        grpc::CallbackServerContext* context_;
        std::function<grpc::Status()> work_;
        std::function<std::uint64_t()> final_mid_;
        diagnostics::RpcExecutionTrace trace_;
    };
    ThreadPool pool_;
    std::once_flag drain_;
};
} // namespace tinyimx::message
