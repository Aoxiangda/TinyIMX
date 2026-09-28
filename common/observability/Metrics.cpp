#include "common/observability/Metrics.h"

#include <opentelemetry/context/context.h>
#include <opentelemetry/metrics/async_instruments.h>
#include <opentelemetry/nostd/variant.h>

#include <utility>

namespace tinyimx {
namespace {

using IntObserver = opentelemetry::nostd::shared_ptr<
    opentelemetry::metrics::ObserverResultT<std::int64_t>
>;

IntObserver GetIntObserver(opentelemetry::metrics::ObserverResult result) {
    return opentelemetry::nostd::get<IntObserver>(result);
}

}  // namespace

Metrics& Metrics::Instance() {
    // Intentionally process-lifetime: instrumentation can be referenced by
    // component destructors during static teardown.
    static Metrics* metrics = new Metrics();
    return *metrics;
}

void Metrics::Initialize(
    const opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>& meter
) {
    if (!meter) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return;
    }

    thread_pool_tasks_ = meter->CreateUInt64Counter(
        "tinyimx.thread_pool.tasks",
        "Thread-pool task lifecycle events",
        "{task}"
    );

    thread_pool_task_duration_ = meter->CreateDoubleHistogram(
        "tinyimx.thread_pool.task.duration",
        "Thread-pool task execution duration",
        "s"
    );

    tcp_connection_events_ = meter->CreateUInt64Counter(
        "tinyimx.tcp.connections",
        "TCP connection lifecycle events",
        "{connection}"
    );

    rpc_client_duration_ = meter->CreateDoubleHistogram(
        "rpc.client.call.duration",
        "Duration of outbound gRPC calls",
        "s"
    );
    rpc_server_duration_ = meter->CreateDoubleHistogram(
        "rpc.server.call.duration",
        "Duration of inbound gRPC calls",
        "s"
    );

    thread_pool_queue_size_ = meter->CreateInt64ObservableGauge(
        "tinyimx.thread_pool.queue.size",
        "Current thread-pool queue depth",
        "{task}"
    );
    thread_pool_worker_count_ = meter->CreateInt64ObservableGauge(
        "tinyimx.thread_pool.workers",
        "Current thread-pool worker count",
        "{worker}"
    );
    thread_pool_active_worker_count_ = meter->CreateInt64ObservableGauge(
        "tinyimx.thread_pool.active_workers",
        "Current active thread-pool workers",
        "{worker}"
    );
    tcp_connection_count_ = meter->CreateInt64ObservableGauge(
        "tinyimx.tcp.connections.active",
        "Current active TCP connections",
        "{connection}"
    );

    thread_pool_queue_size_->AddCallback(&Metrics::ObserveThreadPoolQueue, this);
    thread_pool_worker_count_->AddCallback(&Metrics::ObserveThreadPoolWorkers, this);
    thread_pool_active_worker_count_->AddCallback(
        &Metrics::ObserveThreadPoolActiveWorkers,
        this
    );
    tcp_connection_count_->AddCallback(&Metrics::ObserveTcpConnections, this);

    initialized_ = true;
}

void Metrics::Shutdown() {
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        queue_size;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        workers;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        active_workers;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        tcp_connections;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return;
        }
        initialized_ = false;
        queue_size = thread_pool_queue_size_;
        workers = thread_pool_worker_count_;
        active_workers = thread_pool_active_worker_count_;
        tcp_connections = tcp_connection_count_;
    }

    // Never hold the registry mutex while asking the SDK to remove callbacks:
    // an exporter thread may already be inside one of those callbacks.
    if (queue_size) {
        queue_size->RemoveCallback(&Metrics::ObserveThreadPoolQueue, this);
    }
    if (workers) {
        workers->RemoveCallback(&Metrics::ObserveThreadPoolWorkers, this);
    }
    if (active_workers) {
        active_workers->RemoveCallback(&Metrics::ObserveThreadPoolActiveWorkers, this);
    }
    if (tcp_connections) {
        tcp_connections->RemoveCallback(&Metrics::ObserveTcpConnections, this);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    thread_pool_tasks_.reset();
    thread_pool_task_duration_.reset();
    tcp_connection_events_.reset();
    rpc_client_duration_.reset();
    rpc_server_duration_.reset();
    thread_pool_queue_size_ = nullptr;
    thread_pool_worker_count_ = nullptr;
    thread_pool_active_worker_count_ = nullptr;
    tcp_connection_count_ = nullptr;
}

bool Metrics::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

void Metrics::RegisterThreadPool(
    const void* owner,
    std::string pool_name,
    ThreadPoolSnapshotFn snapshot_fn
) {
    if (owner == nullptr || !snapshot_fn || pool_name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    thread_pools_[owner] = ThreadPoolRegistration{
        std::move(pool_name),
        std::move(snapshot_fn)
    };
}

void Metrics::UnregisterThreadPool(const void* owner) {
    if (owner == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    thread_pools_.erase(owner);
}

void Metrics::RegisterTcpServer(
    const void* owner,
    std::string server_name,
    TcpServerSnapshotFn snapshot_fn
) {
    if (owner == nullptr || !snapshot_fn || server_name.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tcp_servers_[owner] = TcpServerRegistration{
        std::move(server_name),
        std::move(snapshot_fn)
    };
}

void Metrics::UnregisterTcpServer(const void* owner) {
    if (owner == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tcp_servers_.erase(owner);
}

void Metrics::RecordThreadPoolTaskEvent(
    const std::string& pool_name,
    const char* event
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !thread_pool_tasks_ || event == nullptr) {
        return;
    }

    thread_pool_tasks_->Add(
        1,
        {{"pool.name", pool_name}, {"event", event}}
    );
}

void Metrics::RecordThreadPoolTaskDuration(
    const std::string& pool_name,
    bool success,
    double seconds
) {
    if (seconds < 0.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !thread_pool_task_duration_) {
        return;
    }

    thread_pool_task_duration_->Record(
        seconds,
        {{"pool.name", pool_name}, {"result", success ? "success" : "error"}},
        opentelemetry::context::Context{}
    );
}

void Metrics::RecordTcpConnectionEvent(
    const std::string& server_name,
    const char* event
) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !tcp_connection_events_ || event == nullptr) {
        return;
    }

    tcp_connection_events_->Add(
        1,
        {{"server.name", server_name}, {"event", event}}
    );
}

void Metrics::RecordRpcDuration(
    bool client,
    const std::string& service,
    const std::string& method,
    bool success,
    double seconds,
    int grpc_status_code
) {
    if (service.empty() || method.empty() || seconds < 0.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto* histogram = client ? rpc_client_duration_.get() : rpc_server_duration_.get();
    if (!initialized_ || histogram == nullptr) {
        return;
    }

    const std::string error_type =
        success ? std::string{} : "grpc.status." + std::to_string(grpc_status_code);

    if (error_type.empty()) {
        histogram->Record(
            seconds,
            {
                {"rpc.system.name", "grpc"},
                {"rpc.service", service},
                {"rpc.method", method}
            },
            opentelemetry::context::Context{}
        );
    } else {
        histogram->Record(
            seconds,
            {
                {"rpc.system.name", "grpc"},
                {"rpc.service", service},
                {"rpc.method", method},
                {"error.type", error_type}
            },
            opentelemetry::context::Context{}
        );
    }
}

void Metrics::ObserveThreadPoolQueue(
    opentelemetry::metrics::ObserverResult result,
    void* state
) {
    static_cast<Metrics*>(state)->ObserveThreadPools(std::move(result), 0);
}

void Metrics::ObserveThreadPoolWorkers(
    opentelemetry::metrics::ObserverResult result,
    void* state
) {
    static_cast<Metrics*>(state)->ObserveThreadPools(std::move(result), 1);
}

void Metrics::ObserveThreadPoolActiveWorkers(
    opentelemetry::metrics::ObserverResult result,
    void* state
) {
    static_cast<Metrics*>(state)->ObserveThreadPools(std::move(result), 2);
}

void Metrics::ObserveTcpConnections(
    opentelemetry::metrics::ObserverResult result,
    void* state
) {
    static_cast<Metrics*>(state)->ObserveTcpServers(std::move(result));
}

void Metrics::ObserveThreadPools(
    opentelemetry::metrics::ObserverResult result,
    int selector
) {
    auto observer = GetIntObserver(std::move(result));
    if (!observer) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [owner, registration] : thread_pools_) {
        (void)owner;
        const ThreadPoolMetricsSnapshot snapshot = registration.snapshot();
        std::int64_t value = 0;
        if (selector == 0) {
            value = snapshot.queue_size;
        } else if (selector == 1) {
            value = snapshot.worker_count;
        } else {
            value = snapshot.active_worker_count;
        }
        observer->Observe(value, {{"pool.name", registration.name}});
    }
}

void Metrics::ObserveTcpServers(opentelemetry::metrics::ObserverResult result) {
    auto observer = GetIntObserver(std::move(result));
    if (!observer) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [owner, registration] : tcp_servers_) {
        (void)owner;
        const TcpServerMetricsSnapshot snapshot = registration.snapshot();
        observer->Observe(
            snapshot.connection_count,
            {{"server.name", registration.name}}
        );
    }
}

}  // namespace tinyimx
