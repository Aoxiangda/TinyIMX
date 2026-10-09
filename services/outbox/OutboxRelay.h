#pragma once

#include "services/eventing/EventPublisher.h"
#include "services/outbox/OutboxStore.h"
#include "services/outbox/OutboxTypes.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tinyimx::outbox {

struct OutboxRelayOptions {
    std::string instance_id{"outbox-relay-1"};
    std::string allowed_topic{"tinyimx-message-events"};

    std::size_t batch_size{32};
    std::size_t worker_threads{4};
    std::size_t max_inflight{128};

    int poll_interval_ms{100};
    int lease_ms{30000};
    int lease_renew_interval_ms{5000};

    int retry_base_ms{200};
    int retry_max_ms{30000};

    int published_retention_hours{168};
    int cleanup_interval_ms{60000};
    std::size_t cleanup_batch_size{1000};
};

struct OutboxRelayStats {
    std::uint64_t claimed_total{0};
    std::uint64_t published_total{0};
    std::uint64_t retry_total{0};
    std::uint64_t quarantined_total{0};
    std::uint64_t ownership_lost_total{0};
    std::uint64_t publish_failure_total{0};
    std::uint64_t claim_failure_total{0};
    std::size_t inflight{0};
};

class OutboxRelay final {
public:
    OutboxRelay(
        OutboxStore* store,
        eventing::EventPublisher* publisher,
        OutboxRelayOptions options
    );
    ~OutboxRelay();

    OutboxRelay(const OutboxRelay&) = delete;
    OutboxRelay& operator=(const OutboxRelay&) = delete;

    [[nodiscard]] bool Start();
    [[nodiscard]] bool ShutdownGraceful(int timeout_ms);

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;
    [[nodiscard]] OutboxRelayStats Stats() const;

    [[nodiscard]] static int ComputeRetryDelayForTest(
        const std::string& event_id,
        std::uint32_t next_attempt_count,
        int base_ms,
        int max_ms
    );

private:
    struct QueueItem {
        OutboxRecord record;
    };

    [[nodiscard]] bool ValidateOptions(std::string* error) const;
    [[nodiscard]] std::string NextClaimToken();
    [[nodiscard]] bool ValidateRecord(
        const OutboxRecord& record,
        std::string* error
    ) const;

    void PollLoop();
    void RenewLoop();
    void WorkerLoop(std::size_t worker_index);
    void ProcessRecord(const OutboxRecord& record);
    void FinishRecord(std::uint64_t outbox_id);

private:
    OutboxStore* store_{nullptr};
    eventing::EventPublisher* publisher_{nullptr};
    OutboxRelayOptions options_;

    std::atomic<bool> running_{false};
    std::atomic<bool> worker_stop_{false};
    std::atomic<bool> renew_stop_{false};
    std::atomic<std::uint64_t> claim_generation_{0};

    std::thread poll_thread_;
    std::thread renew_thread_;
    std::vector<std::thread> workers_;

    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<QueueItem> queue_;

    mutable std::mutex inflight_mutex_;
    std::condition_variable drain_cv_;
    std::unordered_map<std::uint64_t, std::string> inflight_;

    mutable std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_cv_;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    std::atomic<std::uint64_t> claimed_total_{0};
    std::atomic<std::uint64_t> published_total_{0};
    std::atomic<std::uint64_t> retry_total_{0};
    std::atomic<std::uint64_t> quarantined_total_{0};
    std::atomic<std::uint64_t> ownership_lost_total_{0};
    std::atomic<std::uint64_t> publish_failure_total_{0};
    std::atomic<std::uint64_t> claim_failure_total_{0};
};

}  // namespace tinyimx::outbox
