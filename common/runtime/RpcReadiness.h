#pragma once
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/server.h>
#include <mutex>
namespace tinyimx::runtime {
// Named service begins unknown; the default empty-service status is not used.
inline constexpr char kRpcReadinessService[] = "tinyimx.ready";
inline void EnableRpcReadiness() {
    static std::once_flag once;
    std::call_once(once, [] { grpc::EnableDefaultHealthCheckService(true); });
}
inline bool SetRpcReadiness(grpc::Server* server, bool ready) {
    if (server == nullptr || server->GetHealthCheckService() == nullptr)
        return false;
    server->GetHealthCheckService()->SetServingStatus(kRpcReadinessService, ready);
    return true;
}
}  // namespace tinyimx::runtime
