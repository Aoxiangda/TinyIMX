#include "services/eventing/EventPublisher.h"

#include <stdexcept>

namespace tinyimx::eventing {

std::vector<PublishResult> EventPublisher::PublishBatch(
    const std::vector<PublishMessage>& messages
) {
    if (messages.size() > 32) {
        throw std::invalid_argument("event publication batch exceeds 32 messages");
    }
    std::vector<PublishResult> results;
    results.reserve(messages.size());
    for (const auto& message : messages) {
        results.push_back(Publish(message));
    }
    return results;
}

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
