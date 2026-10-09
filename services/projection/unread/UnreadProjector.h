#pragma once

#include "services/eventing/EventConsumer.h"
#include "services/projection/unread/UnreadProjectionReader.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace tinyimx {
class UnreadCountCache;
}

namespace tinyimx::projection::unread {

// The unread projection owns only private-message unread events on the shared
// M16 business-event topic. Keep this expression identical for every member
// of the unread-projector consumer group.
inline constexpr const char* kUnreadProjectionTagFilter =
    "message.created.v1||dialog.read_advanced.v1";

enum class UnreadProjectorMode {
    kShadow = 0,
    kWriter,
};

/*
 * M16-C3 processing classification.
 *
 * Transport semantics remain at-least-once:
 *
 *   Succeeded
 *       -> ACK
 *
 *   RetryableDependencyFailure
 *       -> NO ACK
 *       -> RocketMQ redelivery after invisible duration
 *
 *   PermanentMessageFailure
 *       -> NO ACK
 *       -> bounded RocketMQ retry
 *       -> DLQ
 *
 * This classification intentionally stays inside the projection layer.
 * RocketMQ-specific delivery-attempt metadata is not leaked into the
 * generic EventConsumer / ConsumedEvent abstraction.
 */
enum class UnreadProcessDisposition {
    kSucceeded = 0,
    kRetryableDependencyFailure,
    kPermanentMessageFailure,
};

struct UnreadProcessResult {
    UnreadProcessDisposition disposition{
        UnreadProcessDisposition::kRetryableDependencyFailure
    };

    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return disposition == UnreadProcessDisposition::kSucceeded;
    }
};

struct UnreadProjectorOptions {
    UnreadProjectorMode mode{UnreadProjectorMode::kShadow};
    std::string topic{"tinyimx-message-events"};
    std::size_t batch_size{16};
    int invisible_duration_ms{30000};
    int receive_error_backoff_ms{500};
};

struct UnreadProjectorStats {
    std::uint64_t received_total{0};
    std::uint64_t reconciled_total{0};
    std::uint64_t shadow_match_total{0};
    std::uint64_t shadow_mismatch_total{0};
    // Backward-compatible aggregate failure counter.
    std::uint64_t process_failure_total{0};

    // M16-C3 classified failure counters.
    std::uint64_t receive_failure_total{0};
    std::uint64_t retryable_failure_total{0};
    std::uint64_t poison_failure_total{0};

    std::uint64_t ack_failure_total{0};
};

class UnreadProjector final {
public:
    UnreadProjector(
        eventing::EventConsumer* consumer,
        UnreadProjectionReader* reader,
        UnreadCountCache* cache,
        UnreadProjectorOptions options
    );
    ~UnreadProjector();

    UnreadProjector(const UnreadProjector&) = delete;
    UnreadProjector& operator=(const UnreadProjector&) = delete;

    [[nodiscard]] bool Start();
    void Shutdown();

    [[nodiscard]] bool RebuildUser(
        std::uint64_t receiver_user_id,
        std::string* error_message = nullptr
    );

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] UnreadProjectorStats Stats() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

private:
    struct AffectedDialog {
        std::uint64_t receiver_user_id{0};
        std::uint64_t peer_user_id{0};
    };

    [[nodiscard]] bool ValidateOptions(std::string* error) const;
    [[nodiscard]] UnreadProcessResult ProcessEvent(
        const eventing::ConsumedEvent& consumed
    );
    [[nodiscard]] bool ExtractAffectedDialog(
        const eventing::ConsumedEvent& consumed,
        AffectedDialog* dialog,
        std::string* error
    ) const;

    void RunLoop();

private:
    eventing::EventConsumer* consumer_{nullptr};
    UnreadProjectionReader* reader_{nullptr};
    UnreadCountCache* cache_{nullptr};
    UnreadProjectorOptions options_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_cv_;

    std::string last_error_;

    std::atomic<std::uint64_t> received_total_{0};
    std::atomic<std::uint64_t> reconciled_total_{0};
    std::atomic<std::uint64_t> shadow_match_total_{0};
    std::atomic<std::uint64_t> shadow_mismatch_total_{0};
    // Backward-compatible aggregate failure counter.
    std::atomic<std::uint64_t> process_failure_total_{0};

    // M16-C3 classified failure counters.
    std::atomic<std::uint64_t> receive_failure_total_{0};
    std::atomic<std::uint64_t> retryable_failure_total_{0};
    std::atomic<std::uint64_t> poison_failure_total_{0};

    std::atomic<std::uint64_t> ack_failure_total_{0};
};

}  // namespace tinyimx::projection::unread
