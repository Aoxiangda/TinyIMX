#include "services/eventing/EventCodec.h"
#include "services/eventing/EventPublisher.h"
#include "services/outbox/OutboxRelay.h"
#include "services/outbox/OutboxStore.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

class FakeStore final : public tinyimx::outbox::OutboxStore {
public:
    explicit FakeStore(tinyimx::outbox::OutboxRecord record)
        : record_(std::move(record)) {
    }

    tinyimx::outbox::OutboxClaimResult ClaimBatch(
        const std::string& claim_token,
        std::size_t,
        int
    ) override {
        std::lock_guard<std::mutex> lock(mutex_);
        tinyimx::outbox::OutboxClaimResult result;
        result.success = true;
        if (!available_) {
            return result;
        }
        available_ = false;
        record_.locked_by = claim_token;
        result.records.push_back(record_);
        return result;
    }

    tinyimx::outbox::OutboxMutationResult RenewClaim(
        std::uint64_t id,
        const std::string& token,
        int
    ) override {
        return Owned(id, token, "renew");
    }

    tinyimx::outbox::OutboxMutationResult MarkPublished(
        std::uint64_t id,
        const std::string& token
    ) override {
        auto result = Owned(id, token, "published");
        if (result.success) {
            published_ = true;
        }
        return result;
    }

    tinyimx::outbox::OutboxMutationResult MarkRetry(
        std::uint64_t id,
        const std::string& token,
        int retry_after_ms,
        const std::string&
    ) override {
        auto result = Owned(id, token, "retry");
        if (result.success) {
            retry_ = true;
            retry_after_ms_ = retry_after_ms;
        }
        return result;
    }

    tinyimx::outbox::OutboxMutationResult MarkQuarantined(
        std::uint64_t id,
        const std::string& token,
        const std::string&,
        bool publish_attempted
    ) override {
        auto result = Owned(id, token, "quarantine");
        if (result.success) {
            quarantined_ = true;
            quarantine_publish_attempted_ = publish_attempted;
        }
        return result;
    }

    tinyimx::outbox::OutboxMutationResult ReleaseClaim(
        std::uint64_t id,
        const std::string& token
    ) override {
        return Owned(id, token, "release");
    }

    tinyimx::outbox::OutboxCleanupResult CleanupPublished(
        int,
        std::size_t
    ) override {
        tinyimx::outbox::OutboxCleanupResult result;
        result.success = true;
        return result;
    }

    bool published() const { std::lock_guard<std::mutex> lock(mutex_); return published_; }
    bool retry() const { std::lock_guard<std::mutex> lock(mutex_); return retry_; }
    bool quarantined() const { std::lock_guard<std::mutex> lock(mutex_); return quarantined_; }
    bool quarantine_publish_attempted() const { std::lock_guard<std::mutex> lock(mutex_); return quarantine_publish_attempted_; }
    int retry_after_ms() const { std::lock_guard<std::mutex> lock(mutex_); return retry_after_ms_; }

private:
    tinyimx::outbox::OutboxMutationResult Owned(
        std::uint64_t id,
        const std::string& token,
        const std::string& action
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        tinyimx::outbox::OutboxMutationResult result;
        if (id != record_.outbox_id || token != record_.locked_by) {
            result.ownership_lost = true;
            result.message = action + " ownership lost";
            return result;
        }
        result.success = true;
        result.affected_rows = 1;
        return result;
    }

private:
    mutable std::mutex mutex_;
    tinyimx::outbox::OutboxRecord record_;
    bool available_{true};
    bool published_{false};
    bool retry_{false};
    bool quarantined_{false};
    bool quarantine_publish_attempted_{false};
    int retry_after_ms_{0};
};

class FakePublisher final : public tinyimx::eventing::EventPublisher {
public:
    explicit FakePublisher(tinyimx::eventing::PublishStatus status)
        : status_(status) {
    }

    tinyimx::eventing::PublishResult Publish(
        const tinyimx::eventing::PublishMessage& message
    ) override {
        ++calls_;
        last_event_id_ = message.event_id;
        tinyimx::eventing::PublishResult result;
        result.status = status_;
        result.broker_message_id = status_ == tinyimx::eventing::PublishStatus::kPublished
            ? "broker-1" : "";
        result.message = status_ == tinyimx::eventing::PublishStatus::kPublished
            ? "ok" : "injected publish failure";
        return result;
    }

    int calls() const { return calls_; }
    const std::string& last_event_id() const { return last_event_id_; }

private:
    tinyimx::eventing::PublishStatus status_;
    int calls_{0};
    std::string last_event_id_;
};

int g_failed = 0;

void Expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++g_failed;
    }
}

tinyimx::outbox::OutboxRecord MakeRecord(bool valid_payload = true) {
    tinyimx::eventing::DomainEvent event;
    event.schema_version = 1;
    event.event_id = "message.created.v1:99001";
    event.event_type = "message.created.v1";
    event.aggregate_type = "private_message";
    event.aggregate_id = "99001";
    event.producer_service = "message-service";
    event.occurred_at = "2026-09-09T00:00:00.000Z";
    event.payload = {
        {"message_id", 99001},
        {"from_user_id", 10001},
        {"to_user_id", 10002},
    };
    const auto encoded = tinyimx::eventing::EventCodec::Encode(event);

    tinyimx::outbox::OutboxRecord record;
    record.outbox_id = 1;
    record.event_id = event.event_id;
    record.event_type = event.event_type;
    record.schema_version = event.schema_version;
    record.aggregate_type = event.aggregate_type;
    record.aggregate_id = event.aggregate_id;
    record.producer_service = event.producer_service;
    record.topic = "tinyimx-message-events";
    record.tag = event.event_type;
    record.message_key = event.event_id;
    record.payload = valid_payload ? encoded.encoded : "{broken-json";
    return record;
}

tinyimx::outbox::OutboxRelayOptions Options() {
    tinyimx::outbox::OutboxRelayOptions options;
    options.instance_id = "relay-test";
    options.batch_size = 1;
    options.worker_threads = 1;
    options.max_inflight = 1;
    options.poll_interval_ms = 5;
    options.lease_ms = 3000;
    options.lease_renew_interval_ms = 500;
    options.retry_base_ms = 20;
    options.retry_max_ms = 100;
    options.cleanup_interval_ms = 10000;
    return options;
}

bool WaitUntil(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return condition();
}

void TestPublished() {
    FakeStore store(MakeRecord());
    FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);
    tinyimx::outbox::OutboxRelay relay(&store, &publisher, Options());
    Expect(relay.Start(), "relay starts for publish case");
    Expect(WaitUntil([&] { return store.published(); }), "published result marks durable outbox Published");
    Expect(publisher.calls() == 1, "published case invokes publisher once");
    Expect(relay.ShutdownGraceful(1000), "published case drains gracefully");
}

void TestRetry() {
    FakeStore store(MakeRecord());
    FakePublisher publisher(tinyimx::eventing::PublishStatus::kRetryableFailure);
    tinyimx::outbox::OutboxRelay relay(&store, &publisher, Options());
    Expect(relay.Start(), "relay starts for retry case");
    Expect(WaitUntil([&] { return store.retry(); }), "retryable send schedules durable retry");
    Expect(store.retry_after_ms() >= 1 && store.retry_after_ms() <= 100, "retry delay stays bounded");
    Expect(relay.ShutdownGraceful(1000), "retry case drains gracefully");
}

void TestInvalidQuarantine() {
    FakeStore store(MakeRecord(false));
    FakePublisher publisher(tinyimx::eventing::PublishStatus::kPublished);
    tinyimx::outbox::OutboxRelay relay(&store, &publisher, Options());
    Expect(relay.Start(), "relay starts for quarantine case");
    Expect(WaitUntil([&] { return store.quarantined(); }), "invalid durable event is quarantined");
    Expect(publisher.calls() == 0, "invalid durable event never reaches RocketMQ publisher");
    Expect(!store.quarantine_publish_attempted(), "validation quarantine does not increment publish attempt");
    Expect(relay.ShutdownGraceful(1000), "quarantine case drains gracefully");
}

void TestDeterministicBackoff() {
    const int a = tinyimx::outbox::OutboxRelay::ComputeRetryDelayForTest(
        "event-x", 4, 200, 30000
    );
    const int b = tinyimx::outbox::OutboxRelay::ComputeRetryDelayForTest(
        "event-x", 4, 200, 30000
    );
    const int capped = tinyimx::outbox::OutboxRelay::ComputeRetryDelayForTest(
        "event-x", 30, 200, 30000
    );
    Expect(a == b, "retry jitter is deterministic for event/attempt");
    Expect(a > 0 && a <= 30000, "retry jitter stays in configured bound");
    Expect(capped > 0 && capped <= 30000, "exponential retry remains capped");
}

}  // namespace

int main() {
    std::cout << "========== TinyIMX M16-B Outbox Relay Unit Tests ==========\n";
    TestPublished();
    TestRetry();
    TestInvalidQuarantine();
    TestDeterministicBackoff();
    std::cout << "===========================================================\n";
    if (g_failed == 0) {
        std::cout << "[PASS] M16-B Outbox Relay unit tests\n";
    }
    return g_failed == 0 ? 0 : 1;
}
