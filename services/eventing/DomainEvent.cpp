#include "services/eventing/DomainEvent.h"

namespace tinyimx::eventing {

bool DomainEvent::Valid() const noexcept {
    return schema_version > 0 &&
           !event_id.empty() &&
           !event_type.empty() &&
           !aggregate_type.empty() &&
           !aggregate_id.empty() &&
           !producer_service.empty() &&
           !occurred_at.empty() &&
           payload.is_object();
}

}  // namespace tinyimx::eventing
