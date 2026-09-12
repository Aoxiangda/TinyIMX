#pragma once

#include "services/eventing/EventConsumer.h"
#include "services/eventing/EventPublisher.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include <unistd.h>

namespace tinyimx::tests::m16c {

enum class CrashStage : std::uint32_t {
    kUnknown = 0,
    kAfterClaimBeforePublish = 71,
    kAfterPublishBeforeMarkPublished = 72,
    kAfterRedisApplyBeforeAck = 73,
};

struct CrashEvidence {
    std::uint32_t stage{0};
    char event_id[192]{};
    char broker_message_id[192]{};
};

inline void CopyEvidenceText(
    char* dst,
    std::size_t capacity,
    const std::string& value
) noexcept {
    if (dst == nullptr || capacity == 0) {
        return;
    }

    std::snprintf(
        dst,
        capacity,
        "%s",
        value.c_str()
    );
}

inline void WriteAll(
    int fd,
    const void* data,
    std::size_t size
) noexcept {
    if (fd < 0 || data == nullptr || size == 0) {
        return;
    }

    const auto* ptr = static_cast<const char*>(data);
    std::size_t remaining = size;

    while (remaining > 0) {
        const ssize_t written = ::write(fd, ptr, remaining);

        if (written > 0) {
            ptr += written;
            remaining -= static_cast<std::size_t>(written);
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        break;
    }
}

[[noreturn]] inline void ExitAtCrashPoint(
    int evidence_fd,
    CrashStage stage,
    const std::string& event_id,
    const std::string& broker_message_id,
    int exit_code
) noexcept {
    CrashEvidence evidence;
    evidence.stage = static_cast<std::uint32_t>(stage);

    CopyEvidenceText(
        evidence.event_id,
        sizeof(evidence.event_id),
        event_id
    );

    CopyEvidenceText(
        evidence.broker_message_id,
        sizeof(evidence.broker_message_id),
        broker_message_id
    );

    WriteAll(
        evidence_fd,
        &evidence,
        sizeof(evidence)
    );

    /*
     * _exit(), not std::exit():
     *
     * - no C++ destructor unwinding
     * - no stdio flushing dependency
     * - models abrupt process death
     * - on Linux terminates the process group of threads via exit_group
     */
    ::_exit(exit_code);
}

/*
 * Window B
 *
 * Relay has already:
 *
 *   ClaimBatch
 *      -> queue
 *      -> worker
 *      -> EventPublisher::Publish
 *
 * Therefore reaching this method proves the record was already claimed.
 */
class CrashBeforePublishPublisher final
    : public eventing::EventPublisher {
public:
    explicit CrashBeforePublishPublisher(int evidence_fd)
        : evidence_fd_(evidence_fd) {}

    eventing::PublishResult Publish(
        const eventing::PublishMessage& message
    ) override {
        ExitAtCrashPoint(
            evidence_fd_,
            CrashStage::kAfterClaimBeforePublish,
            message.event_id,
            "",
            71
        );
    }

private:
    int evidence_fd_{-1};
};

/*
 * Window C
 *
 * delegate_->Publish() performs the real/fake broker publish first.
 *
 * Only after kPublished is returned do we terminate the process.
 * Therefore OutboxRelay never regains control and cannot execute
 * MarkPublished().
 */
class CrashAfterPublishPublisher final
    : public eventing::EventPublisher {
public:
    CrashAfterPublishPublisher(
        eventing::EventPublisher* delegate,
        int evidence_fd
    )
        : delegate_(delegate),
          evidence_fd_(evidence_fd) {}

    eventing::PublishResult Publish(
        const eventing::PublishMessage& message
    ) override {
        if (delegate_ == nullptr) {
            eventing::PublishResult result;
            result.status =
                eventing::PublishStatus::kPermanentFailure;
            result.message =
                "CrashAfterPublishPublisher has null delegate";
            return result;
        }

        auto result = delegate_->Publish(message);

        if (result.Published()) {
            ExitAtCrashPoint(
                evidence_fd_,
                CrashStage::kAfterPublishBeforeMarkPublished,
                message.event_id,
                result.broker_message_id,
                72
            );
        }

        return result;
    }

private:
    eventing::EventPublisher* delegate_{nullptr};
    int evidence_fd_{-1};
};

/*
 * Window D
 *
 * UnreadProjector production order is:
 *
 *   ProcessEvent()
 *       -> MySQL durable snapshot
 *       -> Redis SetUnreadSnapshot
 *   Ack()
 *
 * Therefore reaching this decorator's Ack() means projection processing
 * already succeeded. We crash BEFORE forwarding ACK to the real consumer.
 */
class CrashBeforeAckConsumer final
    : public eventing::EventConsumer {
public:
    CrashBeforeAckConsumer(
        eventing::EventConsumer* delegate,
        int evidence_fd
    )
        : delegate_(delegate),
          evidence_fd_(evidence_fd) {}

    eventing::EventReceiveResult Receive(
        std::size_t limit,
        int invisible_duration_ms
    ) override {
        if (delegate_ == nullptr) {
            eventing::EventReceiveResult result;
            result.success = false;
            result.message =
                "CrashBeforeAckConsumer has null delegate";
            return result;
        }

        return delegate_->Receive(
            limit,
            invisible_duration_ms
        );
    }

    eventing::EventAckResult Ack(
        const eventing::ConsumedEvent& event
    ) override {
        std::string event_id;

        if (!event.keys.empty()) {
            event_id = event.keys.front();
        }

        ExitAtCrashPoint(
            evidence_fd_,
            CrashStage::kAfterRedisApplyBeforeAck,
            event_id,
            event.broker_message_id,
            73
        );
    }

private:
    eventing::EventConsumer* delegate_{nullptr};
    int evidence_fd_{-1};
};

}  // namespace tinyimx::tests::m16c
