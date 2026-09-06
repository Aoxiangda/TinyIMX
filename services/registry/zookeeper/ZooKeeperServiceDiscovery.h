#pragma once

#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tinyimx::registry::zookeeper {

struct ServiceDiscoverySnapshot {
    std::vector<ServiceInstance> instances;
    std::uint64_t generation{0};
    bool initialized{false};
    bool last_refresh_ok{false};
    std::chrono::steady_clock::time_point refreshed_at{};
    std::chrono::steady_clock::time_point control_plane_unhealthy_since{};
    std::string last_error;
};

class ZooKeeperServiceDiscovery final {
public:
    ZooKeeperServiceDiscovery(
        std::shared_ptr<ZooKeeperClient> client,
        std::string service_root,
        ServiceDiscoveryConfig config
    );
    ~ZooKeeperServiceDiscovery();

    ZooKeeperServiceDiscovery(
        const ZooKeeperServiceDiscovery&
    ) = delete;
    ZooKeeperServiceDiscovery& operator=(
        const ZooKeeperServiceDiscovery&
    ) = delete;

    bool Start(std::chrono::milliseconds timeout);
    void Stop();

    [[nodiscard]] bool Running() const;
    [[nodiscard]] bool Ready() const;
    [[nodiscard]] ServiceDiscoverySnapshot Snapshot(
        const std::string& service_name
    ) const;
    [[nodiscard]] std::string LastError() const;

private:
    struct ControlState;

    void ControlLoop();
    bool RefreshService(const std::string& service_name);
    void PublishSnapshot(
        const std::string& service_name,
        std::vector<ServiceInstance> instances
    );
    void MarkRefreshFailed(
        const std::string& service_name,
        const std::string& error
    );
    void MarkAllRefreshFailed(const std::string& error);
    void QueueRefresh(const std::string& service_name);
    void QueueRefreshAll();
    [[nodiscard]] bool AllInitialized() const;
    [[nodiscard]] bool AnyRefreshFailed() const;

    [[nodiscard]] std::string ServicePath(
        const std::string& service_name
    ) const;

private:
    std::shared_ptr<ZooKeeperClient> client_;
    std::string service_root_;
    ServiceDiscoveryConfig config_;

    std::shared_ptr<ControlState> control_state_;
    ZooKeeperClient::WatchObserverId observer_id_{0};
    std::thread control_thread_;
    std::uint64_t observed_session_generation_{0};

    mutable std::shared_mutex snapshot_mutex_;
    std::unordered_map<std::string, ServiceDiscoverySnapshot> snapshots_;

    mutable std::mutex state_mutex_;
    mutable std::condition_variable state_cv_;
    bool running_{false};
    bool ready_{false};
    std::string last_error_;
};

}  // namespace tinyimx::registry::zookeeper
