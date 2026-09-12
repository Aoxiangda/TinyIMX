#include "tests/reliability/M16FaultDecorators.h"

#include "services/eventing/EventCodec.h"
#include "services/outbox/OutboxRelay.h"
#include "services/outbox/OutboxStore.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using tinyimx::tests::m16c::CrashEvidence;
using tinyimx::tests::m16c::CrashStage;

int g_failed = 0;

void Expect(
    bool condition,
    const std::string& name
) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++g_failed;
    }
}

void EmitAction(
    int fd,
    char action
) {
    if (fd < 0) {
        return;
    }

    while (true) {
        const ssize_t rc =
            ::write(fd, &action, sizeof(action));

        if (rc == sizeof(action)) {
            return;
        }

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        return;
    }
}

class ReportingStore final
    : public tinyimx::outbox::OutboxStore {
public:
    ReportingStore(
        tinyimx::outbox::OutboxRecord record,
        int action_fd
    )
        : record_(std::move(record)),
          action_fd_(action_fd) {}

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

        EmitAction(action_fd_, 'C');

        result.records.push_back(record_);
        return result;
    }

    tinyimx::outbox::OutboxMutationResult RenewClaim(
        std::uint64_t id,
        const std::string& token,
        int
    ) override {
        return Owned(
            id,
            token,
            "renew"
        );
    }

    tinyimx::outbox::OutboxMutationResult MarkPublished(
        std::uint64_t id,
        const std::string& token
    ) override {
        auto result =
            Owned(id, token, "published");

        if (result.success) {
            EmitAction(action_fd_, 'M');
        }

        return result;
    }

    tinyimx::outbox::OutboxMutationResult MarkRetry(
        std::uint64_t id,
        const std::string& token,
        int,
        const std::string&
    ) override {
        auto result =
            Owned(id, token, "retry");

        if (result.success) {
            EmitAction(action_fd_, 'R');
        }

        return result;
    }

    tinyimx::outbox::OutboxMutationResult MarkQuarantined(
        std::uint64_t id,
        const std::string& token,
        const std::string&,
        bool
    ) override {
        auto result =
            Owned(id, token, "quarantine");

        if (result.success) {
            EmitAction(action_fd_, 'Q');
        }

        return result;
    }

    tinyimx::outbox::OutboxMutationResult ReleaseClaim(
        std::uint64_t id,
        const std::string& token
    ) override {
        auto result =
            Owned(id, token, "release");

        if (result.success) {
            EmitAction(action_fd_, 'L');
        }

        return result;
    }

    tinyimx::outbox::OutboxCleanupResult CleanupPublished(
        int,
        std::size_t
    ) override {
        tinyimx::outbox::OutboxCleanupResult result;
        result.success = true;
        return result;
    }

private:
    tinyimx::outbox::OutboxMutationResult Owned(
        std::uint64_t id,
        const std::string& token,
        const std::string& action
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        tinyimx::outbox::OutboxMutationResult result;

        if (id != record_.outbox_id ||
            token != record_.locked_by) {
            result.ownership_lost = true;
            result.message =
                action + " ownership lost";
            return result;
        }

        result.success = true;
        result.affected_rows = 1;
        result.message = action;
        return result;
    }

private:
    mutable std::mutex mutex_;
    tinyimx::outbox::OutboxRecord record_;
    int action_fd_{-1};
    bool available_{true};
};

class SuccessfulPublisher final
    : public tinyimx::eventing::EventPublisher {
public:
    tinyimx::eventing::PublishResult Publish(
        const tinyimx::eventing::PublishMessage&
    ) override {
        tinyimx::eventing::PublishResult result;
        result.status =
            tinyimx::eventing::PublishStatus::kPublished;
        result.broker_message_id =
            "broker-before-mark";
        result.message = "published";
        return result;
    }
};

class ReportingConsumer final
    : public tinyimx::eventing::EventConsumer {
public:
    explicit ReportingConsumer(int ack_fd)
        : ack_fd_(ack_fd) {}

    tinyimx::eventing::EventReceiveResult Receive(
        std::size_t,
        int
    ) override {
        tinyimx::eventing::ConsumedEvent event;
        event.broker_message_id =
            "broker-before-ack";
        event.topic =
            "tinyimx-message-events";
        event.tag =
            "message.created.v1";
        event.keys.push_back(
            "message.created.v1:99001"
        );
        event.body = "{}";

        tinyimx::eventing::EventReceiveResult result;
        result.success = true;
        result.events.push_back(std::move(event));
        result.message = "ok";
        return result;
    }

    tinyimx::eventing::EventAckResult Ack(
        const tinyimx::eventing::ConsumedEvent&
    ) override {
        EmitAction(ack_fd_, 'A');

        tinyimx::eventing::EventAckResult result;
        result.success = true;
        result.message = "acked";
        return result;
    }

private:
    int ack_fd_{-1};
};

tinyimx::outbox::OutboxRecord MakeRecord() {
    tinyimx::eventing::DomainEvent event;

    event.schema_version = 1;
    event.event_id =
        "message.created.v1:99001";
    event.event_type =
        "message.created.v1";
    event.aggregate_type =
        "private_message";
    event.aggregate_id =
        "99001";
    event.producer_service =
        "message-service";
    event.occurred_at =
        "2026-09-12T00:00:00.000Z";
    event.payload = {
        {"message_id", 99001},
        {"from_user_id", 10001},
        {"to_user_id", 10002},
    };

    const auto encoded =
        tinyimx::eventing::EventCodec::Encode(event);

    tinyimx::outbox::OutboxRecord record;
    record.outbox_id = 1;
    record.event_id = event.event_id;
    record.event_type = event.event_type;
    record.schema_version = event.schema_version;
    record.aggregate_type = event.aggregate_type;
    record.aggregate_id = event.aggregate_id;
    record.producer_service = event.producer_service;
    record.topic =
        "tinyimx-message-events";
    record.tag = event.event_type;
    record.message_key = event.event_id;
    record.payload = encoded.encoded;

    return record;
}

tinyimx::outbox::OutboxRelayOptions RelayOptions(
    const std::string& instance_id
) {
    tinyimx::outbox::OutboxRelayOptions options;

    options.instance_id = instance_id;
    options.allowed_topic =
        "tinyimx-message-events";

    options.batch_size = 1;
    options.worker_threads = 1;
    options.max_inflight = 1;

    options.poll_interval_ms = 5;

    // Same deterministic test timing already used by current relay tests.
    options.lease_ms = 3000;
    options.lease_renew_interval_ms = 500;

    options.retry_base_ms = 20;
    options.retry_max_ms = 100;

    options.cleanup_interval_ms = 10000;
    options.cleanup_batch_size = 100;

    return options;
}

bool WaitChild(
    pid_t pid,
    int* status,
    std::chrono::milliseconds timeout
) {
    const auto deadline =
        std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t rc =
            ::waitpid(pid, status, WNOHANG);

        if (rc == pid) {
            return true;
        }

        if (rc < 0 && errno != EINTR) {
            return false;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10)
        );
    }

    ::kill(pid, SIGKILL);

    while (::waitpid(pid, status, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }

    return false;
}

bool ReadEvidence(
    int fd,
    CrashEvidence* evidence
) {
    if (fd < 0 || evidence == nullptr) {
        return false;
    }

    char* ptr =
        reinterpret_cast<char*>(evidence);

    std::size_t remaining =
        sizeof(*evidence);

    while (remaining > 0) {
        const ssize_t rc =
            ::read(fd, ptr, remaining);

        if (rc > 0) {
            ptr += rc;
            remaining -=
                static_cast<std::size_t>(rc);
            continue;
        }

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        break;
    }

    return remaining == 0;
}

std::string ReadActions(int fd) {
    std::string actions;

    char buffer[64];

    while (true) {
        const ssize_t rc =
            ::read(fd, buffer, sizeof(buffer));

        if (rc > 0) {
            actions.append(
                buffer,
                static_cast<std::size_t>(rc)
            );
            continue;
        }

        if (rc < 0 && errno == EINTR) {
            continue;
        }

        break;
    }

    return actions;
}

bool HasAction(
    const std::string& actions,
    char value
) {
    return actions.find(value) !=
           std::string::npos;
}

void TestCrashAfterClaimBeforePublish() {
    int evidence_pipe[2]{-1, -1};
    int action_pipe[2]{-1, -1};

    Expect(
        ::pipe(evidence_pipe) == 0,
        "B evidence pipe created"
    );

    Expect(
        ::pipe(action_pipe) == 0,
        "B store-action pipe created"
    );

    const pid_t pid = ::fork();

    Expect(
        pid >= 0,
        "B fork succeeds"
    );

    if (pid == 0) {
        ::close(evidence_pipe[0]);
        ::close(action_pipe[0]);

        ReportingStore store(
            MakeRecord(),
            action_pipe[1]
        );

        tinyimx::tests::m16c::
            CrashBeforePublishPublisher publisher(
                evidence_pipe[1]
            );

        tinyimx::outbox::OutboxRelay relay(
            &store,
            &publisher,
            RelayOptions("m16c-window-b")
        );

        if (!relay.Start()) {
            ::_exit(90);
        }

        std::this_thread::sleep_for(
            std::chrono::seconds(3)
        );

        // Crash decorator should have terminated us.
        ::_exit(91);
    }

    if (pid < 0) {
        return;
    }

    ::close(evidence_pipe[1]);
    ::close(action_pipe[1]);

    int status = 0;

    const bool exited =
        WaitChild(
            pid,
            &status,
            std::chrono::seconds(5)
        );

    CrashEvidence evidence{};
    const bool has_evidence =
        ReadEvidence(
            evidence_pipe[0],
            &evidence
        );

    const std::string actions =
        ReadActions(action_pipe[0]);

    ::close(evidence_pipe[0]);
    ::close(action_pipe[0]);

    Expect(
        exited,
        "B child exits deterministically"
    );

    Expect(
        WIFEXITED(status) &&
        WEXITSTATUS(status) == 71,
        "B exits exactly at AFTER_CLAIM_BEFORE_PUBLISH"
    );

    Expect(
        has_evidence &&
        evidence.stage ==
            static_cast<std::uint32_t>(
                CrashStage::kAfterClaimBeforePublish
            ),
        "B crash evidence stage is correct"
    );

    Expect(
        std::string(evidence.event_id) ==
            "message.created.v1:99001",
        "B crash evidence preserves event_id"
    );

    Expect(
        HasAction(actions, 'C'),
        "B durable record was claimed before crash"
    );

    Expect(
        !HasAction(actions, 'M'),
        "B MarkPublished was never reached"
    );
}

void TestCrashAfterPublishBeforeMarkPublished() {
    int evidence_pipe[2]{-1, -1};
    int action_pipe[2]{-1, -1};

    Expect(
        ::pipe(evidence_pipe) == 0,
        "C evidence pipe created"
    );

    Expect(
        ::pipe(action_pipe) == 0,
        "C store-action pipe created"
    );

    const pid_t pid = ::fork();

    Expect(
        pid >= 0,
        "C fork succeeds"
    );

    if (pid == 0) {
        ::close(evidence_pipe[0]);
        ::close(action_pipe[0]);

        ReportingStore store(
            MakeRecord(),
            action_pipe[1]
        );

        SuccessfulPublisher real_publish_boundary;

        tinyimx::tests::m16c::
            CrashAfterPublishPublisher publisher(
                &real_publish_boundary,
                evidence_pipe[1]
            );

        tinyimx::outbox::OutboxRelay relay(
            &store,
            &publisher,
            RelayOptions("m16c-window-c")
        );

        if (!relay.Start()) {
            ::_exit(90);
        }

        std::this_thread::sleep_for(
            std::chrono::seconds(3)
        );

        ::_exit(91);
    }

    if (pid < 0) {
        return;
    }

    ::close(evidence_pipe[1]);
    ::close(action_pipe[1]);

    int status = 0;

    const bool exited =
        WaitChild(
            pid,
            &status,
            std::chrono::seconds(5)
        );

    CrashEvidence evidence{};
    const bool has_evidence =
        ReadEvidence(
            evidence_pipe[0],
            &evidence
        );

    const std::string actions =
        ReadActions(action_pipe[0]);

    ::close(evidence_pipe[0]);
    ::close(action_pipe[0]);

    Expect(
        exited,
        "C child exits deterministically"
    );

    Expect(
        WIFEXITED(status) &&
        WEXITSTATUS(status) == 72,
        "C exits exactly at AFTER_PUBLISH_BEFORE_MARK_PUBLISHED"
    );

    Expect(
        has_evidence &&
        evidence.stage ==
            static_cast<std::uint32_t>(
                CrashStage::kAfterPublishBeforeMarkPublished
            ),
        "C crash evidence stage is correct"
    );

    Expect(
        std::string(evidence.broker_message_id) ==
            "broker-before-mark",
        "C publish completed before crash"
    );

    Expect(
        HasAction(actions, 'C'),
        "C durable record was claimed"
    );

    Expect(
        !HasAction(actions, 'M'),
        "C MarkPublished was never reached after publish"
    );
}

void TestCrashBeforeAckSeam() {
    int evidence_pipe[2]{-1, -1};
    int delegate_ack_pipe[2]{-1, -1};

    Expect(
        ::pipe(evidence_pipe) == 0,
        "D evidence pipe created"
    );

    Expect(
        ::pipe(delegate_ack_pipe) == 0,
        "D delegate-ACK pipe created"
    );

    const pid_t pid = ::fork();

    Expect(
        pid >= 0,
        "D fork succeeds"
    );

    if (pid == 0) {
        ::close(evidence_pipe[0]);
        ::close(delegate_ack_pipe[0]);

        ReportingConsumer delegate(
            delegate_ack_pipe[1]
        );

        tinyimx::tests::m16c::
            CrashBeforeAckConsumer consumer(
                &delegate,
                evidence_pipe[1]
            );

        const auto received =
            consumer.Receive(1, 5000);

        if (!received.success ||
            received.events.empty()) {
            ::_exit(90);
        }

        /*
         * The decorator must terminate here.
         * delegate.Ack() must never run.
         */
        (void)consumer.Ack(
            received.events.front()
        );

        ::_exit(91);
    }

    if (pid < 0) {
        return;
    }

    ::close(evidence_pipe[1]);
    ::close(delegate_ack_pipe[1]);

    int status = 0;

    const bool exited =
        WaitChild(
            pid,
            &status,
            std::chrono::seconds(5)
        );

    CrashEvidence evidence{};
    const bool has_evidence =
        ReadEvidence(
            evidence_pipe[0],
            &evidence
        );

    const std::string delegate_actions =
        ReadActions(delegate_ack_pipe[0]);

    ::close(evidence_pipe[0]);
    ::close(delegate_ack_pipe[0]);

    Expect(
        exited,
        "D child exits deterministically"
    );

    Expect(
        WIFEXITED(status) &&
        WEXITSTATUS(status) == 73,
        "D exits exactly at AFTER_REDIS_APPLY_BEFORE_ACK seam"
    );

    Expect(
        has_evidence &&
        evidence.stage ==
            static_cast<std::uint32_t>(
                CrashStage::kAfterRedisApplyBeforeAck
            ),
        "D crash evidence stage is correct"
    );

    Expect(
        std::string(evidence.event_id) ==
            "message.created.v1:99001",
        "D preserves business event identity"
    );

    Expect(
        std::string(evidence.broker_message_id) ==
            "broker-before-ack",
        "D preserves broker delivery identity"
    );

    Expect(
        !HasAction(delegate_actions, 'A'),
        "D real delegate ACK was not invoked"
    );
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M16-C Crash Window Contract Tests ==========\n";

    TestCrashAfterClaimBeforePublish();
    TestCrashAfterPublishBeforeMarkPublished();
    TestCrashBeforeAckSeam();

    std::cout
        << "===============================================================\n";

    if (g_failed == 0) {
        std::cout
            << "[PASS] M16-C deterministic crash seams established\n";
    } else {
        std::cerr
            << "[FAIL] M16-C crash seam contract failures="
            << g_failed
            << '\n';
    }

    return g_failed == 0 ? 0 : 1;
}
