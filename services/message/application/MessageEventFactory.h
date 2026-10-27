#pragma once

#include "services/eventing/DomainEvent.h"
#include "services/message/application/MessageApplicationTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace tinyimx::message {

struct OutboxEventSpec {
    eventing::DomainEvent event;
    std::string topic;
    std::string tag;
    std::string message_key;
};

class MessageEventFactory final {
public:
    static constexpr std::string_view kDefaultTopic{"tinyimx-message-events"};
    [[nodiscard]] static bool IsValidTopic(std::string_view topic) noexcept;
    [[nodiscard]] static OutboxEventSpec MessageCreated(
        const MessageView& message,
        std::string_view topic = kDefaultTopic
    );

    [[nodiscard]] static OutboxEventSpec GroupMessageCreated(
        const GroupMessageView& message,
        std::string_view topic = kDefaultTopic
    );

    [[nodiscard]] static OutboxEventSpec DialogReadAdvanced(
        std::uint64_t reader_user_id,
        std::uint64_t peer_user_id,
        std::uint64_t affected_rows,
        std::string_view topic = kDefaultTopic
    );
};

}  // namespace tinyimx::message
