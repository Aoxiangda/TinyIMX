#pragma once

#include "services/eventing/DomainEvent.h"
#include "services/message/application/MessageApplicationTypes.h"

#include <cstdint>
#include <string>

namespace tinyimx::message {

struct OutboxEventSpec {
    eventing::DomainEvent event;
    std::string topic;
    std::string tag;
    std::string message_key;
};

class MessageEventFactory final {
public:
    [[nodiscard]] static OutboxEventSpec MessageCreated(
        const MessageView& message
    );

    [[nodiscard]] static OutboxEventSpec GroupMessageCreated(
        const GroupMessageView& message
    );

    [[nodiscard]] static OutboxEventSpec DialogReadAdvanced(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id,
        std::uint64_t affected_rows
    );
};

}  // namespace tinyimx::message
