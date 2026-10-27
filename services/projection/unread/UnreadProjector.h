#pragma once

#include "services/eventing/EventConsumer.h"
#include "services/projection/unread/UnreadProjectionReader.h"

#include <atomic>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    // Each worker holds at most one received batch, including unacked events.
    // Values >1 require a consumer supporting concurrent Receive/Ack calls.
    std::size_t worker_threads{1};
    // Opt-in bounded parallel transport ACK, only after projection succeeds.
    bool parallel_batch_ack{false};
    // Opt-in single-read-view SQL snapshots per received batch; writer only.
    bool batch_snapshot_reads{false};
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
    // API invocations and requested dialogs, not inferred SQL query counts.
    std::uint64_t snapshot_batch_calls{0};
    std::uint64_t snapshot_batch_dialogs{0};
};

class UnreadProjector final {
public:
    UnreadProjector(
        eventing::EventConsumer* consumer,
        UnreadProjectionSource* reader,
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
    [[nodiscard]] std::vector<UnreadProcessResult> ProcessBatch(
        const std::vector<eventing::ConsumedEvent>& events
    );
    [[nodiscard]] UnreadProcessResult ApplyDurableSnapshot(
        const AffectedDialog& dialog,
        const UnreadProjectionReadResult<DialogUnreadSnapshot>& durable
    );
    [[nodiscard]] bool ExtractAffectedDialog(
        const eventing::ConsumedEvent& consumed,
        AffectedDialog* dialog,
        std::string* error
    ) const;

    void RunLoop();

private:
    eventing::EventConsumer* consumer_{nullptr};
    UnreadProjectionSource* reader_{nullptr};
    UnreadCountCache* cache_{nullptr};
    UnreadProjectorOptions options_;

    std::atomic<bool> running_{false};
    std::vector<std::thread> threads_;
    // A receiver's total spans all peers. Serialize the durable read AND cache
    // write (also RebuildUser); locking by dialog alone permits stale totals.
    std::array<std::mutex, 1024> receiver_mutexes_;
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
    std::atomic<std::uint64_t> snapshot_batch_calls_{0};
    std::atomic<std::uint64_t> snapshot_batch_dialogs_{0};
};

}  // namespace tinyimx::projection::unread
