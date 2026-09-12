#include "services/message/application/MessageEventFactory.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include <unistd.h>

namespace tinyimx::message {
namespace {

constexpr const char* kMessageEventsTopic = "tinyimx-message-events";
constexpr const char* kProducerService = "message-service";

std::string CurrentUtcIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ) % 1000;

    std::tm utc{};
    gmtime_r(&raw, &utc);

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis.count()
        << 'Z';
    return oss.str();
}

std::string MakeReadEventId(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id
) {
    static std::atomic<std::uint64_t> sequence{0};

    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    return "dialog.read_advanced.v1:" +
           std::to_string(reader_user_id) + ":" +
           std::to_string(peer_user_id) + ":" +
           std::to_string(static_cast<long long>(nanos)) + ":" +
           std::to_string(static_cast<long long>(::getpid())) + ":" +
           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

}  // namespace

OutboxEventSpec MessageEventFactory::MessageCreated(
    const MessageView& message
) {
    OutboxEventSpec spec;

    spec.event.schema_version = 1;
    spec.event.event_id =
        "message.created.v1:" + std::to_string(message.message_id);
    spec.event.event_type = "message.created.v1";
    spec.event.aggregate_type = "private_message";
    spec.event.aggregate_id = std::to_string(message.message_id);
    spec.event.producer_service = kProducerService;
    spec.event.occurred_at = CurrentUtcIso8601();
    spec.event.payload = {
        {"message_id", message.message_id},
        {"from_user_id", message.from_user_id},
        {"to_user_id", message.to_user_id},
    };

    spec.topic = kMessageEventsTopic;
    spec.tag = spec.event.event_type;
    spec.message_key = spec.event.event_id;
    return spec;
}

OutboxEventSpec MessageEventFactory::DialogReadAdvanced(
    std::uint64_t reader_user_id,
    std::uint64_t peer_user_id,
    std::uint64_t affected_rows
) {
    OutboxEventSpec spec;

    spec.event.schema_version = 1;
    spec.event.event_id = MakeReadEventId(reader_user_id, peer_user_id);
    spec.event.event_type = "dialog.read_advanced.v1";
    spec.event.aggregate_type = "dialog";
    spec.event.aggregate_id =
        std::to_string(reader_user_id) + ":" + std::to_string(peer_user_id);
    spec.event.producer_service = kProducerService;
    spec.event.occurred_at = CurrentUtcIso8601();
    spec.event.payload = {
        {"reader_user_id", reader_user_id},
        {"peer_user_id", peer_user_id},
        {"affected_rows", affected_rows},
    };

    spec.topic = kMessageEventsTopic;
    spec.tag = spec.event.event_type;
    spec.message_key = spec.event.event_id;
    return spec;
}

}  // namespace tinyimx::message
