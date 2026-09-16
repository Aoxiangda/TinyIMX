#include "services/rpc/StaticServiceEndpointProvider.h"

#include <utility>

namespace tinyimx::rpc {

StaticServiceEndpointProvider::StaticServiceEndpointProvider(
    std::string social_target
)
    : StaticServiceEndpointProvider(
          std::move(social_target),
          std::string{},
          std::string{}
      ) {
}

StaticServiceEndpointProvider::StaticServiceEndpointProvider(
    std::string social_target,
    std::string user_target
)
    : StaticServiceEndpointProvider(
          std::move(social_target),
          std::move(user_target),
          std::string{}
      ) {
}

StaticServiceEndpointProvider::StaticServiceEndpointProvider(
    std::string social_target,
    std::string user_target,
    std::string message_target
)
    : StaticServiceEndpointProvider(
          std::move(social_target),
          std::move(user_target),
          std::move(message_target),
          std::string{}
      ) {
}

StaticServiceEndpointProvider::StaticServiceEndpointProvider(
    std::string social_target,
    std::string user_target,
    std::string message_target,
    std::string group_target
)
    : social_target_(std::move(social_target)),
      user_target_(std::move(user_target)),
      message_target_(std::move(message_target)),
      group_target_(std::move(group_target)) {
}

std::optional<ServiceEndpoint>
StaticServiceEndpointProvider::Resolve(
    ServiceKind service
) const {
    switch (service) {
        case ServiceKind::kSocial:
            if (!social_target_.empty()) {
                return ServiceEndpoint{social_target_};
            }
            break;

        case ServiceKind::kUser:
            if (!user_target_.empty()) {
                return ServiceEndpoint{user_target_};
            }
            break;

        case ServiceKind::kMessage:
            if (!message_target_.empty()) {
                return ServiceEndpoint{message_target_};
            }
            break;

        case ServiceKind::kGroup:
            if (!group_target_.empty()) {
                return ServiceEndpoint{group_target_};
            }
            break;
    }

    return std::nullopt;
}

}  // namespace tinyimx::rpc
