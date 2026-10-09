#pragma once

#include "services/registry/GatewayRegistry.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tinyimx {

class GatewayDiscovery {
public:
    GatewayDiscovery(
        GatewayRegistry* registry,
        int refresh_interval_seconds
    );

    ~GatewayDiscovery();

    GatewayDiscovery(
        const GatewayDiscovery&
    ) = delete;

    GatewayDiscovery& operator=(
        const GatewayDiscovery&
    ) = delete;

    bool Start();

    void Stop();

    bool RefreshNow();

    bool IsRunning() const noexcept;

    std::size_t Size() const;

    std::vector<GatewayInstanceRecord>
    Snapshot() const;

    std::optional<GatewayInstanceRecord>
    FindById(
        const std::string& gateway_id
    ) const;

private:
    void RefreshLoop();

private:
    GatewayRegistry*
        registry_{nullptr};

    int refresh_interval_seconds_{3};

    std::atomic<bool>
        running_{false};

    mutable std::shared_mutex
        snapshot_mutex_;

    std::unordered_map<
        std::string,
        GatewayInstanceRecord
    > instances_;

    std::mutex wait_mutex_;

    std::condition_variable
        wait_cv_;

    std::thread refresh_thread_;
};

}  // namespace tinyimx