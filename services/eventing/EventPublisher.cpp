#include "services/eventing/EventPublisher.h"

namespace tinyimx::eventing {

const char* PublishStatusToString(PublishStatus status) noexcept {
    switch (status) {
        case PublishStatus::kPublished:
            return "published";
        case PublishStatus::kRetryableFailure:
            return "retryable_failure";
        case PublishStatus::kPermanentFailure:
            return "permanent_failure";
    }
    return "unknown";
}

}  // namespace tinyimx::eventing
