#pragma once

#include "common/config/ConfigTypes.h"
#include "services/rpc/ServiceEndpointProvider.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace tinyimx::registry::zookeeper {
class ZooKeeperServiceDiscovery;
}

namespace tinyimx::rpc {

class ZooKeeperServiceEndpointProvider final
    : public ServiceEndpointProvider {
public:
    ZooKeeperServiceEndpointProvider(
        std::shared_ptr<
            registry::zookeeper::ZooKeeperServiceDiscovery
        > discovery,
        ServiceDiscoveryConfig config
    );

    [[nodiscard]] std::optional<ServiceEndpoint> Resolve(
        ServiceKind service
    ) const override;

private:
    [[nodiscard]] static std::optional<std::size_t> ServiceIndex(
        ServiceKind service
    );
    [[nodiscard]] static const char* ServiceName(ServiceKind service);

private:
    std::shared_ptr<
        registry::zookeeper::ZooKeeperServiceDiscovery
    > discovery_;
    ServiceDiscoveryConfig config_;
    mutable std::array<std::atomic<std::uint64_t>, 5> counters_{};
};

}  // namespace tinyimx::rpc
