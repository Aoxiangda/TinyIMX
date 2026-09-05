#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tinyimx::rpc {

enum class ServiceKind : std::uint8_t {
    kSocial = 0,
    kUser,
    kMessage,
};

struct ServiceEndpoint {
    std::string target;
};

class ServiceEndpointProvider {
public:
    virtual ~ServiceEndpointProvider() = default;

    ServiceEndpointProvider() = default;
    ServiceEndpointProvider(const ServiceEndpointProvider&) = delete;
    ServiceEndpointProvider& operator=(const ServiceEndpointProvider&) = delete;

    [[nodiscard]] virtual std::optional<ServiceEndpoint> Resolve(
        ServiceKind service
    ) const = 0;
};

}  // namespace tinyimx::rpc
