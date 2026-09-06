#include "services/rpc/ZooKeeperServiceEndpointProvider.h"

#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"

#include <chrono>
#include <cstddef>
#include <utility>

namespace tinyimx::rpc {

ZooKeeperServiceEndpointProvider::ZooKeeperServiceEndpointProvider(
    std::shared_ptr<
        registry::zookeeper::ZooKeeperServiceDiscovery
    > discovery,
    ServiceDiscoveryConfig config
)
    : discovery_(std::move(discovery)),
      config_(std::move(config)) {
    for (auto& counter : counters_) {
        counter.store(0, std::memory_order_relaxed);
    }
}

std::optional<ServiceEndpoint>
ZooKeeperServiceEndpointProvider::Resolve(
    ServiceKind service
) const {
    if (!discovery_ || !discovery_->Running()) {
        return std::nullopt;
    }

    const auto index = ServiceIndex(service);
    const char* service_name = ServiceName(service);
    if (!index.has_value() || service_name == nullptr) {
        return std::nullopt;
    }

    const auto snapshot = discovery_->Snapshot(service_name);
    if (!snapshot.initialized || snapshot.instances.empty()) {
        return std::nullopt;
    }

    if (!snapshot.last_refresh_ok) {
        if (!config_.retain_last_known_good ||
            snapshot.control_plane_unhealthy_since ==
                std::chrono::steady_clock::time_point{}) {
            return std::nullopt;
        }

        const auto age = std::chrono::duration_cast<
            std::chrono::milliseconds
        >(
            std::chrono::steady_clock::now() -
            snapshot.control_plane_unhealthy_since
        );
        if (age > std::chrono::milliseconds(
                      config_.snapshot_stale_after_ms
                  )) {
            return std::nullopt;
        }
    }

    const std::uint64_t cursor =
        counters_[*index].fetch_add(
            1,
            std::memory_order_relaxed
        );
    const std::size_t selected =
        static_cast<std::size_t>(
            cursor % snapshot.instances.size()
        );

    const std::string& target =
        snapshot.instances[selected].target;
    if (target.empty()) {
        return std::nullopt;
    }
    return ServiceEndpoint{target};
}

std::optional<std::size_t>
ZooKeeperServiceEndpointProvider::ServiceIndex(
    ServiceKind service
) {
    switch (service) {
        case ServiceKind::kSocial:
            return 0;
        case ServiceKind::kUser:
            return 1;
        case ServiceKind::kMessage:
            return 2;
        default:
            return std::nullopt;
    }
}

const char* ZooKeeperServiceEndpointProvider::ServiceName(
    ServiceKind service
) {
    switch (service) {
        case ServiceKind::kSocial:
            return "social";
        case ServiceKind::kUser:
            return "user";
        case ServiceKind::kMessage:
            return "message";
        default:
            return nullptr;
    }
}

}  // namespace tinyimx::rpc
