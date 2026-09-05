#pragma once

#include "services/rpc/ServiceEndpointProvider.h"

#include <string>

namespace tinyimx::rpc {

class StaticServiceEndpointProvider final
    : public ServiceEndpointProvider {
public:
    // Backward-compatible M14-A constructor: configures SocialService only.
    explicit StaticServiceEndpointProvider(
        std::string social_target
    );

    // M14-B constructor: configures SocialService + UserService.
    StaticServiceEndpointProvider(
        std::string social_target,
        std::string user_target
    );

    // M14-C constructor: adds the durable MessageService endpoint while
    // preserving the older constructors for existing tests/callers.
    StaticServiceEndpointProvider(
        std::string social_target,
        std::string user_target,
        std::string message_target
    );

    [[nodiscard]] std::optional<ServiceEndpoint> Resolve(
        ServiceKind service
    ) const override;

private:
    const std::string social_target_;
    const std::string user_target_;
    const std::string message_target_;
};

}  // namespace tinyimx::rpc
