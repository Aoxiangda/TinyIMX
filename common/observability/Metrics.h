#pragma once

#include "common/observability/ObservabilityTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <opentelemetry/metrics/meter.h>
#include <opentelemetry/metrics/observer_result.h>
#include <opentelemetry/metrics/sync_instruments.h>
#include <opentelemetry/nostd/shared_ptr.h>
#include <opentelemetry/nostd/unique_ptr.h>

namespace tinyimx {

class Metrics {
public:
    using ThreadPoolSnapshotFn = std::function<ThreadPoolMetricsSnapshot()>;
    using TcpServerSnapshotFn = std::function<TcpServerMetricsSnapshot()>;

    static Metrics& Instance();

    void Initialize(
        const opentelemetry::nostd::shared_ptr<opentelemetry::metrics::Meter>& meter
    );
    void Shutdown();
    bool IsInitialized() const;

    void RegisterThreadPool(
        const void* owner,
        std::string pool_name,
        ThreadPoolSnapshotFn snapshot_fn
    );
    void UnregisterThreadPool(const void* owner);

    void RegisterTcpServer(
        const void* owner,
        std::string server_name,
        TcpServerSnapshotFn snapshot_fn
    );
    void UnregisterTcpServer(const void* owner);

    void RecordThreadPoolTaskEvent(
        const std::string& pool_name,
        const char* event
    );
    void RecordThreadPoolTaskDuration(
        const std::string& pool_name,
        bool success,
        double seconds
    );
    void RecordTcpConnectionEvent(
        const std::string& server_name,
        const char* event
    );
    void RecordRpcDuration(
        bool client,
        const std::string& service,
        const std::string& method,
        bool success,
        double seconds,
        int grpc_status_code
    );

private:
    Metrics() = default;
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;

    struct ThreadPoolRegistration {
        std::string name;
        ThreadPoolSnapshotFn snapshot;
    };

    struct TcpServerRegistration {
        std::string name;
        TcpServerSnapshotFn snapshot;
    };

    static void ObserveThreadPoolQueue(
        opentelemetry::metrics::ObserverResult result,
        void* state
    );
    static void ObserveThreadPoolWorkers(
        opentelemetry::metrics::ObserverResult result,
        void* state
    );
    static void ObserveThreadPoolActiveWorkers(
        opentelemetry::metrics::ObserverResult result,
        void* state
    );
    static void ObserveTcpConnections(
        opentelemetry::metrics::ObserverResult result,
        void* state
    );
    static void ObserveTcpConnectionPeak(
        opentelemetry::metrics::ObserverResult result,
        void* state
    );

    void ObserveThreadPools(
        opentelemetry::metrics::ObserverResult result,
        int selector
    );
    void ObserveTcpServers(opentelemetry::metrics::ObserverResult result);

private:
    mutable std::mutex mutex_;
    bool initialized_{false};

    std::unordered_map<const void*, ThreadPoolRegistration> thread_pools_;
    std::unordered_map<const void*, TcpServerRegistration> tcp_servers_;

    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<std::uint64_t>>
        thread_pool_tasks_;
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        thread_pool_task_duration_;
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Counter<std::uint64_t>>
        tcp_connection_events_;
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        rpc_client_duration_;
    opentelemetry::nostd::unique_ptr<opentelemetry::metrics::Histogram<double>>
        rpc_server_duration_;

    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        thread_pool_queue_size_;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        thread_pool_worker_count_;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        thread_pool_active_worker_count_;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        tcp_connection_count_;
    opentelemetry::nostd::shared_ptr<opentelemetry::metrics::ObservableInstrument>
        tcp_connection_peak_;
};

}  // namespace tinyimx
