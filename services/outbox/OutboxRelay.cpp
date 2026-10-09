#include "services/outbox/OutboxRelay.h"

#include "common/logging/LogMacros.h"
#include "services/eventing/EventCodec.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <sstream>
#include <utility>

#include <unistd.h>

namespace tinyimx::outbox {
namespace {

constexpr std::size_t kMaxWorkers = 64;
constexpr std::size_t kMaxBatchSize = 1024;
constexpr std::size_t kMaxInflight = 8192;
constexpr int kMaxLeaseMs = 10 * 60 * 1000;

}  // namespace

OutboxRelay::OutboxRelay(
    OutboxStore* store,
    eventing::EventPublisher* publisher,
    OutboxRelayOptions options
)
    : store_(store),
      publisher_(publisher),
      options_(std::move(options)) {
}

OutboxRelay::~OutboxRelay() {
    (void)ShutdownGraceful(10000);
}

bool OutboxRelay::ValidateOptions(std::string* error) const {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (store_ == nullptr) {
        return fail("outbox relay requires a store");
    }
    if (publisher_ == nullptr) {
        return fail("outbox relay requires a publisher");
    }
    if (options_.instance_id.empty() || options_.instance_id.size() > 64) {
        return fail("outbox relay instance_id must be 1-64 characters");
    }
    if (options_.allowed_topic.empty() || options_.allowed_topic.size() > 128) {
        return fail("outbox relay allowed_topic is invalid");
    }
    if (options_.batch_size == 0 || options_.batch_size > kMaxBatchSize) {
        return fail("outbox relay batch_size is invalid");
    }
    if (options_.worker_threads == 0 || options_.worker_threads > kMaxWorkers) {
        return fail("outbox relay worker_threads is invalid");
    }
    if (options_.max_inflight < options_.worker_threads ||
        options_.max_inflight < options_.batch_size ||
        options_.max_inflight > kMaxInflight) {
        return fail("outbox relay max_inflight must cover workers and batch_size");
    }
    if (options_.poll_interval_ms <= 0) {
        return fail("outbox relay poll_interval_ms must be positive");
    }
    if (options_.lease_ms <= 0 || options_.lease_ms > kMaxLeaseMs) {
        return fail("outbox relay lease_ms is invalid");
    }
    if (options_.lease_renew_interval_ms <= 0 ||
        options_.lease_renew_interval_ms * 3 >= options_.lease_ms) {
        return fail("outbox relay lease must be more than 3x renew interval");
    }
    if (options_.retry_base_ms <= 0 ||
        options_.retry_max_ms < options_.retry_base_ms) {
        return fail("outbox relay retry window is invalid");
    }
    if (options_.published_retention_hours <= 0 ||
        options_.cleanup_interval_ms <= 0 ||
        options_.cleanup_batch_size == 0) {
        return fail("outbox relay cleanup configuration is invalid");
    }
    return true;
}

bool OutboxRelay::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return true;
    }

    std::string error;
    if (!ValidateOptions(&error)) {
        running_.store(false);
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = std::move(error);
        return false;
    }

    worker_stop_.store(false);
    renew_stop_.store(false);

    try {
        workers_.reserve(options_.worker_threads);
        for (std::size_t i = 0; i < options_.worker_threads; ++i) {
            workers_.emplace_back(&OutboxRelay::WorkerLoop, this, i);
        }
        renew_thread_ = std::thread(&OutboxRelay::RenewLoop, this);
        poll_thread_ = std::thread(&OutboxRelay::PollLoop, this);
    } catch (const std::exception& e) {
        running_.store(false);
        worker_stop_.store(true);
        renew_stop_.store(true);
        queue_cv_.notify_all();
        lifecycle_cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        if (renew_thread_.joinable()) {
            renew_thread_.join();
        }
        if (poll_thread_.joinable()) {
            poll_thread_.join();
        }
        std::lock_guard<std::mutex> lock(error_mutex_);
        last_error_ = std::string("outbox relay thread start failed: ") + e.what();
        return false;
    }

    LOG_INFO(
        "outbox relay started"
        << ", instance_id=" << options_.instance_id
        << ", workers=" << options_.worker_threads
        << ", batch_size=" << options_.batch_size
        << ", max_inflight=" << options_.max_inflight
        << ", lease_ms=" << options_.lease_ms
    );
    return true;
}

bool OutboxRelay::ShutdownGraceful(int timeout_ms) {
    if (timeout_ms <= 0) {
        timeout_ms = 1;
    }

    const bool was_running = running_.exchange(false);
    lifecycle_cv_.notify_all();

    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    bool drained = true;
    {
        std::unique_lock<std::mutex> lock(inflight_mutex_);
        drained = drain_cv_.wait_until(
            lock,
            deadline,
            [&] { return inflight_.empty(); }
        );
    }

    if (!drained) {
        std::vector<OutboxRecord> abandoned;
        {
            std::lock_guard<std::mutex> queue_lock(queue_mutex_);
            while (!queue_.empty()) {
                abandoned.push_back(std::move(queue_.front().record));
                queue_.pop_front();
            }
        }

        for (const auto& record : abandoned) {
            const auto release = store_->ReleaseClaim(
                record.outbox_id,
                record.locked_by
            );
            if (!release.success && release.ownership_lost) {
                ownership_lost_total_.fetch_add(1, std::memory_order_relaxed);
            }
            FinishRecord(record.outbox_id);
        }
    }

    worker_stop_.store(true);
    queue_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    renew_stop_.store(true);
    lifecycle_cv_.notify_all();
    if (renew_thread_.joinable()) {
        renew_thread_.join();
    }

    if (was_running) {
        LOG_INFO(
            "outbox relay stopped"
            << ", instance_id=" << options_.instance_id
            << ", drained=" << drained
            << ", inflight=" << Stats().inflight
        );
    }
    return drained;
}

bool OutboxRelay::IsRunning() const noexcept {
    return running_.load(std::memory_order_acquire);
}

const std::string& OutboxRelay::LastError() const noexcept {
    return last_error_;
}

OutboxRelayStats OutboxRelay::Stats() const {
    OutboxRelayStats stats;
    stats.claimed_total = claimed_total_.load(std::memory_order_relaxed);
    stats.published_total = published_total_.load(std::memory_order_relaxed);
    stats.retry_total = retry_total_.load(std::memory_order_relaxed);
    stats.quarantined_total = quarantined_total_.load(std::memory_order_relaxed);
    stats.ownership_lost_total = ownership_lost_total_.load(std::memory_order_relaxed);
    stats.publish_failure_total = publish_failure_total_.load(std::memory_order_relaxed);
    stats.claim_failure_total = claim_failure_total_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        stats.inflight = inflight_.size();
    }
    return stats;
}

int OutboxRelay::ComputeRetryDelayForTest(
    const std::string& event_id,
    std::uint32_t next_attempt_count,
    int base_ms,
    int max_ms
) {
    if (base_ms <= 0 || max_ms < base_ms) {
        return 0;
    }

    std::uint64_t delay = static_cast<std::uint64_t>(base_ms);
    const std::uint32_t exponent =
        next_attempt_count > 0 ? next_attempt_count - 1 : 0;
    for (std::uint32_t i = 0; i < std::min<std::uint32_t>(exponent, 30); ++i) {
        if (delay >= static_cast<std::uint64_t>(max_ms)) {
            delay = static_cast<std::uint64_t>(max_ms);
            break;
        }
        delay = std::min<std::uint64_t>(
            static_cast<std::uint64_t>(max_ms),
            delay * 2
        );
    }

    const std::string seed = event_id + ":" + std::to_string(next_attempt_count);
    const std::uint64_t hash = std::hash<std::string>{}(seed);
    // Deterministic jitter in [-20%, +20%].
    const int bucket = static_cast<int>(hash % 401ULL) - 200;
    const std::int64_t jittered =
        static_cast<std::int64_t>(delay) +
        (static_cast<std::int64_t>(delay) * bucket) / 1000;

    return static_cast<int>(std::clamp<std::int64_t>(
        jittered,
        1,
        static_cast<std::int64_t>(max_ms)
    ));
}

std::string OutboxRelay::NextClaimToken() {
    const auto generation = claim_generation_.fetch_add(
        1,
        std::memory_order_relaxed
    ) + 1;
    return options_.instance_id + ":" +
           std::to_string(static_cast<long long>(::getpid())) + ":" +
           std::to_string(generation);
}

bool OutboxRelay::ValidateRecord(
    const OutboxRecord& record,
    std::string* error
) const {
    auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };

    if (record.outbox_id == 0 || record.event_id.empty() ||
        record.event_type.empty() || record.message_key.empty()) {
        return fail("outbox record has missing identity metadata");
    }
    if (record.topic != options_.allowed_topic) {
        return fail("outbox record topic is outside configured allowlist");
    }
    if (record.schema_version != 1) {
        return fail("outbox record schema version is unsupported");
    }

    const auto decoded = eventing::EventCodec::Decode(record.payload);
    if (!decoded.success) {
        return fail(decoded.message);
    }

    const auto& event = decoded.event;
    if (event.schema_version != record.schema_version ||
        event.event_id != record.event_id ||
        event.event_type != record.event_type ||
        event.aggregate_type != record.aggregate_type ||
        event.aggregate_id != record.aggregate_id ||
        event.producer_service != record.producer_service) {
        return fail("outbox row metadata does not match encoded event envelope");
    }
    if (record.tag != event.event_type) {
        return fail("outbox tag does not match event_type");
    }
    if (record.message_key != event.event_id) {
        return fail("outbox message_key does not match event_id");
    }
    return true;
}

void OutboxRelay::PollLoop() {
    auto next_cleanup = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(options_.cleanup_interval_ms);

    while (running_.load(std::memory_order_acquire)) {
        std::size_t inflight_count = 0;
        {
            std::lock_guard<std::mutex> lock(inflight_mutex_);
            inflight_count = inflight_.size();
        }

        if (inflight_count < options_.max_inflight) {
            const std::size_t available = options_.max_inflight - inflight_count;
            const std::size_t limit = std::min(options_.batch_size, available);
            const std::string claim_token = NextClaimToken();
            const auto claim = store_->ClaimBatch(
                claim_token,
                limit,
                options_.lease_ms
            );

            if (!claim.success) {
                claim_failure_total_.fetch_add(1, std::memory_order_relaxed);
                LOG_WARN(
                    "outbox relay claim failed"
                    << ", instance_id=" << options_.instance_id
                    << ", error=" << claim.message
                );
            } else if (!claim.records.empty()) {
                {
                    std::lock_guard<std::mutex> inflight_lock(inflight_mutex_);
                    for (const auto& record : claim.records) {
                        inflight_[record.outbox_id] = record.locked_by;
                    }
                }
                {
                    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
                    for (const auto& record : claim.records) {
                        queue_.push_back(QueueItem{record});
                    }
                }
                claimed_total_.fetch_add(
                    claim.records.size(),
                    std::memory_order_relaxed
                );
                queue_cv_.notify_all();
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= next_cleanup) {
            const auto cleanup = store_->CleanupPublished(
                options_.published_retention_hours,
                options_.cleanup_batch_size
            );
            if (!cleanup.success) {
                LOG_WARN(
                    "outbox relay cleanup failed"
                    << ", error=" << cleanup.message
                );
            } else if (cleanup.deleted_rows > 0) {
                LOG_INFO(
                    "outbox relay cleanup completed"
                    << ", deleted_rows=" << cleanup.deleted_rows
                );
            }
            next_cleanup = now +
                std::chrono::milliseconds(options_.cleanup_interval_ms);
        }

        std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
        lifecycle_cv_.wait_for(
            lifecycle_lock,
            std::chrono::milliseconds(options_.poll_interval_ms),
            [&] { return !running_.load(std::memory_order_acquire); }
        );
    }
}

void OutboxRelay::RenewLoop() {
    while (!renew_stop_.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
            lifecycle_cv_.wait_for(
                lifecycle_lock,
                std::chrono::milliseconds(options_.lease_renew_interval_ms),
                [&] { return renew_stop_.load(std::memory_order_acquire); }
            );
        }
        if (renew_stop_.load(std::memory_order_acquire)) {
            break;
        }

        std::vector<std::pair<std::uint64_t, std::string>> owned;
        {
            std::lock_guard<std::mutex> lock(inflight_mutex_);
            owned.reserve(inflight_.size());
            for (const auto& entry : inflight_) {
                owned.push_back(entry);
            }
        }

        for (const auto& [outbox_id, claim_token] : owned) {
            const auto renewed = store_->RenewClaim(
                outbox_id,
                claim_token,
                options_.lease_ms
            );
            if (!renewed.success && renewed.ownership_lost) {
                ownership_lost_total_.fetch_add(1, std::memory_order_relaxed);
                LOG_WARN(
                    "outbox relay lease renewal lost ownership"
                    << ", outbox_id=" << outbox_id
                    << ", claim_token=" << claim_token
                );
            } else if (!renewed.success) {
                LOG_WARN(
                    "outbox relay lease renewal failed"
                    << ", outbox_id=" << outbox_id
                    << ", error=" << renewed.message
                );
            }
        }
    }
}

void OutboxRelay::WorkerLoop(std::size_t worker_index) {
    while (true) {
        QueueItem item;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(
                lock,
                [&] {
                    return worker_stop_.load(std::memory_order_acquire) ||
                           !queue_.empty();
                }
            );

            if (queue_.empty()) {
                if (worker_stop_.load(std::memory_order_acquire)) {
                    break;
                }
                continue;
            }

            item = std::move(queue_.front());
            queue_.pop_front();
        }

        LOG_DEBUG(
            "outbox relay worker processing"
            << ", worker=" << worker_index
            << ", outbox_id=" << item.record.outbox_id
            << ", event_id=" << item.record.event_id
        );
        ProcessRecord(item.record);
        FinishRecord(item.record.outbox_id);
    }
}

void OutboxRelay::ProcessRecord(const OutboxRecord& record) {
    std::string validation_error;
    if (!ValidateRecord(record, &validation_error)) {
        const auto quarantined = store_->MarkQuarantined(
            record.outbox_id,
            record.locked_by,
            validation_error,
            false
        );
        if (quarantined.success) {
            quarantined_total_.fetch_add(1, std::memory_order_relaxed);
            LOG_ERROR(
                "outbox relay quarantined invalid durable event"
                << ", outbox_id=" << record.outbox_id
                << ", event_id=" << record.event_id
                << ", reason=" << validation_error
            );
        } else if (quarantined.ownership_lost) {
            ownership_lost_total_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    eventing::PublishMessage message;
    message.event_id = record.event_id;
    message.topic = record.topic;
    message.tag = record.tag;
    message.message_key = record.message_key;
    message.body = record.payload;

    const auto published = publisher_->Publish(message);
    if (published.status == eventing::PublishStatus::kPublished) {
        const auto marked = store_->MarkPublished(
            record.outbox_id,
            record.locked_by
        );
        if (marked.success) {
            published_total_.fetch_add(1, std::memory_order_relaxed);
            LOG_INFO(
                "outbox relay published event"
                << ", outbox_id=" << record.outbox_id
                << ", event_id=" << record.event_id
                << ", broker_message_id=" << published.broker_message_id
            );
        } else if (marked.ownership_lost) {
            ownership_lost_total_.fetch_add(1, std::memory_order_relaxed);
            LOG_WARN(
                "outbox relay broker success became ambiguous after ownership loss"
                << ", outbox_id=" << record.outbox_id
                << ", event_id=" << record.event_id
            );
        } else {
            publish_failure_total_.fetch_add(1, std::memory_order_relaxed);
            LOG_ERROR(
                "outbox relay broker success but database mark failed; event may be republished"
                << ", outbox_id=" << record.outbox_id
                << ", event_id=" << record.event_id
                << ", error=" << marked.message
            );
        }
        return;
    }

    publish_failure_total_.fetch_add(1, std::memory_order_relaxed);

    if (published.status == eventing::PublishStatus::kPermanentFailure) {
        const auto quarantined = store_->MarkQuarantined(
            record.outbox_id,
            record.locked_by,
            published.message,
            true
        );
        if (quarantined.success) {
            quarantined_total_.fetch_add(1, std::memory_order_relaxed);
        } else if (quarantined.ownership_lost) {
            ownership_lost_total_.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    const std::uint32_t next_attempt =
        record.attempt_count == std::numeric_limits<std::uint32_t>::max()
            ? record.attempt_count
            : record.attempt_count + 1;
    const int retry_after_ms = ComputeRetryDelayForTest(
        record.event_id,
        next_attempt,
        options_.retry_base_ms,
        options_.retry_max_ms
    );
    const auto retry = store_->MarkRetry(
        record.outbox_id,
        record.locked_by,
        retry_after_ms,
        published.message
    );
    if (retry.success) {
        retry_total_.fetch_add(1, std::memory_order_relaxed);
        LOG_WARN(
            "outbox relay scheduled retry"
            << ", outbox_id=" << record.outbox_id
            << ", event_id=" << record.event_id
            << ", retry_after_ms=" << retry_after_ms
            << ", reason=" << published.message
        );
    } else if (retry.ownership_lost) {
        ownership_lost_total_.fetch_add(1, std::memory_order_relaxed);
    }
}

void OutboxRelay::FinishRecord(std::uint64_t outbox_id) {
    {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        inflight_.erase(outbox_id);
    }
    drain_cv_.notify_all();
}

}  // namespace tinyimx::outbox
