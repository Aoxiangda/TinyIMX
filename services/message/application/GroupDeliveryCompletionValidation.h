#pragma once
#include "services/message/application/MessageApplicationTypes.h"
#include <set>
#include <utility>

namespace tinyimx::message {
inline bool ValidGroupDeliveryAttemptCompletions(
    const std::vector<GroupDeliveryAttemptCompletion>& attempts
) {
    if (attempts.empty() || attempts.size() > 256) return false;
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
    for (const auto& item : attempts) {
        if (item.message_id == 0 || item.recipient_user_id == 0 ||
            item.lease_token.empty() || item.lease_token.size() > 128 ||
            item.gateway_id.size() > 128 || item.error_code.size() > 128 ||
            item.retry_after_ms > 600000 ||
            !identities.emplace(item.message_id, item.recipient_user_id).second) {
            return false;
        }
        switch (item.outcome) {
            case GroupDeliveryAttemptOutcome::kSubmitted:
            case GroupDeliveryAttemptOutcome::kOffline:
            case GroupDeliveryAttemptOutcome::kRetryableFailure:
                break;
            default:
                return false;
        }
    }
    return true;
}
}  // namespace tinyimx::message
