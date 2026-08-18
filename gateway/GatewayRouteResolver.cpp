#include "gateway/GatewayRouteResolver.h"

#include <utility>

namespace tinyimx {

GatewayRouteResolver::
GatewayRouteResolver(
    std::string local_gateway_id,
    OnlineStatusCache*
        online_status_cache,
    GatewayDiscovery*
        gateway_discovery
)
    : local_gateway_id_(
          std::move(local_gateway_id)
      ),
      online_status_cache_(
          online_status_cache
      ),
      gateway_discovery_(
          gateway_discovery
      ) {
}


GatewayRouteResult
GatewayRouteResolver::Resolve(
    std::uint64_t user_id
) const {
    GatewayRouteResult result;

    if (user_id == 0) {
        result.status =
            GatewayRouteStatus::
                kInvalidArgument;

        result.error_message =
            "route resolve failed: "
            "user_id is zero";

        return result;
    }

    if (local_gateway_id_.empty()) {
        result.status =
            GatewayRouteStatus::
                kDependencyUnavailable;

        result.error_message =
            "route resolve failed: "
            "local gateway id is empty";

        return result;
    }

    if (online_status_cache_ == nullptr) {
        result.status =
            GatewayRouteStatus::
                kDependencyUnavailable;

        result.error_message =
            "route resolve failed: "
            "online status cache is null";

        return result;
    }

    if (gateway_discovery_ == nullptr) {
        result.status =
            GatewayRouteStatus::
                kDependencyUnavailable;

        result.error_message =
            "route resolve failed: "
            "gateway discovery is null";

        return result;
    }

    const GetOnlineStatusResult
        online_result =
            online_status_cache_->
                GetOnlineStatus(
                    user_id
                );

    if (online_result.NotFound()) {
        result.status =
            GatewayRouteStatus::
                kOffline;

        return result;
    }

    if (!online_result.Found()) {
        result.status =
            GatewayRouteStatus::
                kOnlineStatusError;

        result.error_message =
            online_result.error_message;

        return result;
    }

    result.online_status =
        online_result.record;

    const OnlineStatusRecord&
        online_record =
            online_result.record.value();

    /*
     * Redis 在线状态指向当前 Gateway。
     *
     * 注意：
     * 这里只判断“路由归属”属于本机，
     * 具体 TCP connection 是否仍然存在，
     * 下一阶段仍由 SessionManager 确认。
     */
    if (
        online_record.gateway_id ==
        local_gateway_id_
    ) {
        result.status =
            GatewayRouteStatus::
                kLocal;

        return result;
    }

    /*
     * 在线状态指向其他 Gateway，
     * 从 Discovery 本地快照查找它。
     */
    const auto remote_gateway =
        gateway_discovery_->FindById(
            online_record.gateway_id
        );

    if (!remote_gateway.has_value()) {
        result.status =
            GatewayRouteStatus::
                kGatewayUnavailable;

        result.error_message =
            "online status points to "
            "an unavailable gateway: " +
            online_record.gateway_id;

        return result;
    }

    result.remote_gateway =
        remote_gateway;

    result.status =
        GatewayRouteStatus::
            kRemote;

    return result;
}


const std::string&
GatewayRouteResolver::LocalGatewayId()
    const noexcept {
    return local_gateway_id_;
}


std::string
GatewayRouteStatusToString(
    GatewayRouteStatus status
) {
    switch (status) {
        case GatewayRouteStatus::kLocal:
            return "local";

        case GatewayRouteStatus::kRemote:
            return "remote";

        case GatewayRouteStatus::kOffline:
            return "offline";

        case GatewayRouteStatus::
            kGatewayUnavailable:
            return "gateway_unavailable";

        case GatewayRouteStatus::
            kInvalidArgument:
            return "invalid_argument";

        case GatewayRouteStatus::
            kDependencyUnavailable:
            return "dependency_unavailable";

        case GatewayRouteStatus::
            kOnlineStatusError:
            return "online_status_error";
    }

    return "unknown";
}

}  // namespace tinyimx