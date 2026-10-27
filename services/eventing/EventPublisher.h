#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

    // Existing implementations stay single-message publishers. A batch-capable
    // transport returns one result for each input; ambiguous entries retry.
    [[nodiscard]] virtual std::size_t MaxBatchMessages() const noexcept {
        return 1;
    }
    [[nodiscard]] virtual std::vector<PublishResult> PublishBatch(
        const std::vector<PublishMessage>& messages
    );

protected:
    EventPublisher() = default;
};

const char* PublishStatusToString(PublishStatus status) noexcept;

}  // namespace tinyimx::eventing
