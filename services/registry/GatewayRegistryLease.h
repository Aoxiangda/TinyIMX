#pragma once

#include "services/registry/GatewayRegistry.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace tinyimx {

struct GatewayRegistryLeaseOptions {
    std::string gateway_id;

    std::string advertise_host;

    std::uint16_t advertise_port{0};

    int lease_ttl_seconds{15};

    int heartbeat_interval_seconds{5};
};

class GatewayRegistryLease {
public:
    using LeaseLostCallback =
        std::function<void()>;

    GatewayRegistryLease(
        GatewayRegistry* registry,
        GatewayRegistryLeaseOptions options,
        LeaseLostCallback lease_lost_callback = {}
    );

    ~GatewayRegistryLease();

    GatewayRegistryLease(
        const GatewayRegistryLease&
    ) = delete;

    GatewayRegistryLease& operator=(
        const GatewayRegistryLease&
    ) = delete;

    bool Start();

    void Stop();

    bool IsRunning() const noexcept;

    const std::string&
    LeaseToken() const noexcept;

private:
    void HeartbeatLoop();

    void RefreshOnce();

    void RecoverMissingLease();

    void MarkLeaseLost(
        const std::string& reason
    );

    static std::string
    GenerateLeaseToken();

    static std::int64_t
    UnixNowSeconds();

private:
    GatewayRegistry*
        registry_{nullptr};

    GatewayRegistryLeaseOptions
        options_;

    LeaseLostCallback
        lease_lost_callback_;

    GatewayInstanceRecord
        record_;

    std::atomic<bool>
        running_{false};

    std::atomic<bool>
        owns_lease_{false};

    std::mutex
        wait_mutex_;

    std::condition_variable
        wait_cv_;

    std::thread
        heartbeat_thread_;
};

}  // namespace tinyimx