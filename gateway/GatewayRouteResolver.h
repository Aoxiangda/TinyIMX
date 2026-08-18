#pragma once

#include "services/cache/OnlineStatusCache.h"
#include "services/registry/GatewayDiscovery.h"

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx {

enum class GatewayRouteStatus {
    kLocal = 0,
    kRemote,
    kOffline,
    kGatewayUnavailable,
    kInvalidArgument,
    kDependencyUnavailable,
    kOnlineStatusError
};

struct GatewayRouteResult {
    GatewayRouteStatus status{
        GatewayRouteStatus::
            kDependencyUnavailable
    };

    std::optional<OnlineStatusRecord>
        online_status;

    std::optional<GatewayInstanceRecord>
        remote_gateway;

    std::string error_message;

    bool IsLocal() const noexcept {
        return status ==
            GatewayRouteStatus::kLocal;
    }

    bool IsRemote() const noexcept {
        return status ==
            GatewayRouteStatus::kRemote;
    }

    bool IsOffline() const noexcept {
        return status ==
            GatewayRouteStatus::kOffline;
    }

    bool Resolved() const noexcept {
        return IsLocal() ||
               IsRemote() ||
               IsOffline();
    }
};


class GatewayRouteResolver {
public:
    GatewayRouteResolver(
        std::string local_gateway_id,
        OnlineStatusCache*
            online_status_cache,
        GatewayDiscovery*
            gateway_discovery
    );

    GatewayRouteResolver(
        const GatewayRouteResolver&
    ) = delete;

    GatewayRouteResolver& operator=(
        const GatewayRouteResolver&
    ) = delete;

    GatewayRouteResult Resolve(
        std::uint64_t user_id
    ) const;

    const std::string&
    LocalGatewayId() const noexcept;

private:
    std::string local_gateway_id_;

    OnlineStatusCache*
        online_status_cache_{nullptr};

    GatewayDiscovery*
        gateway_discovery_{nullptr};
};


std::string
GatewayRouteStatusToString(
    GatewayRouteStatus status
);

}  // namespace tinyimx