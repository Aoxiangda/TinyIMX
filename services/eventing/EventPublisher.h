#pragma once

#include <string>

namespace tinyimx::eventing {

enum class PublishStatus {
    kPublished = 0,
    kRetryableFailure,
    kPermanentFailure,
};

struct PublishMessage {
    std::string event_id;
    std::string topic;
    std::string tag;
    std::string message_key;
    std::string body;
};

struct PublishResult {
    PublishStatus status{PublishStatus::kRetryableFailure};
    std::string broker_message_id;
    std::string message;

    [[nodiscard]] bool Published() const noexcept {
        return status == PublishStatus::kPublished;
    }
};

class EventPublisher {
public:
    virtual ~EventPublisher() = default;

    EventPublisher(const EventPublisher&) = delete;
    EventPublisher& operator=(const EventPublisher&) = delete;

    [[nodiscard]] virtual PublishResult Publish(
        const PublishMessage& message
    ) = 0;

protected:
    EventPublisher() = default;
};

const char* PublishStatusToString(PublishStatus status) noexcept;

}  // namespace tinyimx::eventing
