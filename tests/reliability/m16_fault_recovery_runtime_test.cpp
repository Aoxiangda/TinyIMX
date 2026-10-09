#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"

#include "services/cache/UnreadCountCache.h"
#include "services/eventing/DomainEvent.h"
#include "services/eventing/EventCodec.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/outbox/OutboxRelay.h"
#include "services/outbox/OutboxRepository.h"
#include "services/projection/unread/UnreadProjectionReader.h"
#include "services/projection/unread/UnreadProjector.h"

#include "tests/reliability/M16FaultDecorators.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {

using tinyimx::Config;

bool LoadConfig(
    const std::string& path,
    Config* config
) {
    if (config == nullptr) {
        return false;
    }

    if (!config->LoadFromFile(path)) {
        std::cerr
            << "[FAIL] config load: "
            << config->LastError()
            << '\n';
        return false;
    }

    if (!tinyimx::Logger::Instance().Init(
            config->Logger())) {
        std::cerr << "[FAIL] logger init\n";
        return false;
    }

    return true;
}

tinyimx::eventing::rocketmq_transport::
RocketMQProducerOptions
ProducerOptions(const Config& config) {
    tinyimx::eventing::rocketmq_transport::
        RocketMQProducerOptions options;

    options.endpoint =
        config.RocketMQ().endpoint;
    options.topic =
        config.RocketMQ().message_topic;
    options.request_timeout_ms =
        config.RocketMQ().request_timeout_ms;
    options.tls =
        config.RocketMQ().tls;
    options.access_key =
        config.RocketMQ().access_key;
    options.access_secret =
        config.RocketMQ().access_secret;

    return options;
}

tinyimx::eventing::rocketmq_transport::
RocketMQSimpleConsumerOptions
ConsumerOptions(const Config& config) {
    tinyimx::eventing::rocketmq_transport::
        RocketMQSimpleConsumerOptions options;

    options.endpoint =
        config.RocketMQ().endpoint;
    options.topic =
        config.RocketMQ().message_topic;
    options.consumer_group =
        config.UnreadProjection().consumer_group;
    options.filter_expression =
        tinyimx::projection::unread::kUnreadProjectionTagFilter;
    options.request_timeout_ms =
        config.RocketMQ().request_timeout_ms;
    options.await_duration_ms =
        config.UnreadProjection().await_duration_ms;
    options.tls =
        config.RocketMQ().tls;
    options.access_key =
        config.RocketMQ().access_key;
    options.access_secret =
        config.RocketMQ().access_secret;

    return options;
}

tinyimx::outbox::OutboxRelayOptions
RelayOptions(
    const Config& config,
    const std::string& instance_id
) {
    const auto& src = config.OutboxRelay();

    tinyimx::outbox::OutboxRelayOptions options;

    options.instance_id = instance_id;
    options.allowed_topic =
        config.RocketMQ().message_topic;

    options.batch_size = src.batch_size;
    options.worker_threads = src.worker_threads;
    options.max_inflight = src.max_inflight;
    options.poll_interval_ms = src.poll_interval_ms;

    options.lease_ms = src.lease_ms;
    options.lease_renew_interval_ms =
        src.lease_renew_interval_ms;

    options.retry_base_ms = src.retry_base_ms;
    options.retry_max_ms = src.retry_max_ms;

    options.published_retention_hours =
        src.published_retention_hours;
    options.cleanup_interval_ms =
        src.cleanup_interval_ms;
    options.cleanup_batch_size =
        src.cleanup_batch_size;

    return options;
}

tinyimx::projection::unread::
UnreadProjectorOptions
ProjectorOptions(const Config& config) {
    tinyimx::projection::unread::
        UnreadProjectorOptions options;

    options.mode =
        tinyimx::projection::unread::
            UnreadProjectorMode::kWriter;

    options.topic =
        config.RocketMQ().message_topic;

    options.batch_size =
        config.UnreadProjection().batch_size;

    options.invisible_duration_ms =
        config.UnreadProjection().
            invisible_duration_ms;

    options.receive_error_backoff_ms =
        config.UnreadProjection().
            receive_error_backoff_ms;

    return options;
}

class CountingAckConsumer final
    : public tinyimx::eventing::EventConsumer {
public:
    explicit CountingAckConsumer(
        tinyimx::eventing::EventConsumer* delegate
    )
        : delegate_(delegate) {}

    tinyimx::eventing::EventReceiveResult Receive(
        std::size_t limit,
        int invisible_duration_ms
    ) override {
        return delegate_->Receive(
            limit,
            invisible_duration_ms
        );
    }

    tinyimx::eventing::EventAckResult Ack(
        const tinyimx::eventing::ConsumedEvent& event
    ) override {
        const auto result =
            delegate_->Ack(event);

        if (result.success) {
            ack_success_.fetch_add(
                1,
                std::memory_order_relaxed
            );
        } else {
            ack_failure_.fetch_add(
                1,
                std::memory_order_relaxed
            );
        }

        return result;
    }

    std::uint64_t AckSuccess() const noexcept {
        return ack_success_.load(
            std::memory_order_relaxed
        );
    }

    std::uint64_t AckFailure() const noexcept {
        return ack_failure_.load(
            std::memory_order_relaxed
        );
    }

private:
    tinyimx::eventing::EventConsumer*
        delegate_{nullptr};

    std::atomic<std::uint64_t>
        ack_success_{0};

    std::atomic<std::uint64_t>
        ack_failure_{0};
};

int RelayBeforePublish(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] MySQL init\n";
        return 2;
    }

    tinyimx::outbox::OutboxRepository repository(
        &mysql_pool
    );

    tinyimx::tests::m16c::
        CrashBeforePublishPublisher publisher(-1);

    tinyimx::outbox::OutboxRelay relay(
        &repository,
        &publisher,
        RelayOptions(
            config,
            "m16c-real-before-publish"
        )
    );

    if (!relay.Start()) {
        std::cerr
            << "[FAIL] relay start: "
            << relay.LastError()
            << '\n';
        return 2;
    }

    // Crash decorator should terminate process first.
    std::this_thread::sleep_for(
        std::chrono::seconds(60)
    );

    return 91;
}

int RelayAfterPublish(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] MySQL init\n";
        return 2;
    }

    tinyimx::outbox::OutboxRepository repository(
        &mysql_pool
    );

    tinyimx::eventing::rocketmq_transport::
        RocketMQProducer producer(
            ProducerOptions(config)
        );

    if (!producer.Start()) {
        std::cerr
            << "[FAIL] producer start: "
            << producer.LastError()
            << '\n';
        return 2;
    }

    tinyimx::tests::m16c::
        CrashAfterPublishPublisher publisher(
            &producer,
            -1
        );

    tinyimx::outbox::OutboxRelay relay(
        &repository,
        &publisher,
        RelayOptions(
            config,
            "m16c-real-after-publish"
        )
    );

    if (!relay.Start()) {
        std::cerr
            << "[FAIL] relay start: "
            << relay.LastError()
            << '\n';
        return 2;
    }

    std::this_thread::sleep_for(
        std::chrono::seconds(60)
    );

    return 91;
}

int RelayNormalOnce(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        return 2;
    }

    tinyimx::outbox::OutboxRepository repository(
        &mysql_pool
    );

    tinyimx::eventing::rocketmq_transport::
        RocketMQProducer producer(
            ProducerOptions(config)
        );

    if (!producer.Start()) {
        std::cerr << producer.LastError() << '\n';
        return 2;
    }

    tinyimx::outbox::OutboxRelay relay(
        &repository,
        &producer,
        RelayOptions(
            config,
            "m16c-real-recovery"
        )
    );

    if (!relay.Start()) {
        std::cerr << relay.LastError() << '\n';
        return 2;
    }

    bool published = false;

    for (int i = 0; i < 300; ++i) {
        const auto stats = relay.Stats();

        if (stats.published_total >= 1) {
            published = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    const bool drained =
        relay.ShutdownGraceful(5000);

    const auto stats = relay.Stats();

    std::cout
        << "relay_normal_once"
        << ", published=" << stats.published_total
        << ", retry=" << stats.retry_total
        << ", quarantined=" << stats.quarantined_total
        << ", ownership_lost="
        << stats.ownership_lost_total
        << ", drained=" << drained
        << '\n';

    producer.Stop();
    mysql_pool.Shutdown();

    return published && drained ? 0 : 1;
}

int ProjectorBeforeAck(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    tinyimx::RedisConnectionPool redis_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        return 2;
    }

    if (!redis_pool.Initialize(config.Redis())) {
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::
        RocketMQSimpleConsumer consumer(
            ConsumerOptions(config)
        );

    if (!consumer.Start()) {
        std::cerr << consumer.LastError() << '\n';
        return 2;
    }

    tinyimx::tests::m16c::
        CrashBeforeAckConsumer crash_consumer(
            &consumer,
            -1
        );

    tinyimx::UnreadCountCache cache(
        &redis_pool
    );

    tinyimx::projection::unread::
        UnreadProjectionReader reader(
            &mysql_pool
        );

    tinyimx::projection::unread::
        UnreadProjector projector(
            &crash_consumer,
            &reader,
            &cache,
            ProjectorOptions(config)
        );

    if (!projector.Start()) {
        std::cerr
            << projector.LastError()
            << '\n';
        return 2;
    }

    std::this_thread::sleep_for(
        std::chrono::seconds(90)
    );

    return 91;
}

int ProjectorNormalOnce(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    tinyimx::RedisConnectionPool redis_pool;

    if (!mysql_pool.Initialize(config.MySql()) ||
        !redis_pool.Initialize(config.Redis())) {
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::
        RocketMQSimpleConsumer consumer(
            ConsumerOptions(config)
        );

    if (!consumer.Start()) {
        std::cerr << consumer.LastError() << '\n';
        return 2;
    }

    CountingAckConsumer counting_consumer(
        &consumer
    );

    tinyimx::UnreadCountCache cache(
        &redis_pool
    );

    tinyimx::projection::unread::
        UnreadProjectionReader reader(
            &mysql_pool
        );

    tinyimx::projection::unread::
        UnreadProjector projector(
            &counting_consumer,
            &reader,
            &cache,
            ProjectorOptions(config)
        );

    if (!projector.Start()) {
        return 2;
    }

    bool closed = false;

    for (int i = 0; i < 450; ++i) {
        const auto stats = projector.Stats();

        if (stats.received_total >= 1 &&
            stats.reconciled_total >= 1 &&
            stats.process_failure_total == 0 &&
            counting_consumer.AckSuccess() >= 1 &&
            counting_consumer.AckFailure() == 0) {
            closed = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    projector.Shutdown();

    const auto stats = projector.Stats();

    std::cout
        << "projector_normal_once"
        << ", received=" << stats.received_total
        << ", reconciled=" << stats.reconciled_total
        << ", process_failure="
        << stats.process_failure_total
        << ", ack_success="
        << counting_consumer.AckSuccess()
        << ", ack_failure="
        << counting_consumer.AckFailure()
        << '\n';

    consumer.Stop();
    redis_pool.Shutdown();
    mysql_pool.Shutdown();

    return closed ? 0 : 1;
}

int PublishMessageEvent(
    const std::string& config_path,
    std::uint64_t message_id,
    std::uint64_t from_user_id,
    std::uint64_t to_user_id
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::eventing::DomainEvent event;

    event.schema_version = 1;
    event.event_id =
        "message.created.v1:" +
        std::to_string(message_id);

    event.event_type =
        "message.created.v1";

    event.aggregate_type =
        "private_message";

    event.aggregate_id =
        std::to_string(message_id);

    event.producer_service =
        "message-service";

    event.occurred_at =
        "2026-09-12T00:00:00.000Z";

    event.payload = {
        {"message_id", message_id},
        {"from_user_id", from_user_id},
        {"to_user_id", to_user_id},
    };

    const auto encoded =
        tinyimx::eventing::EventCodec::Encode(event);

    if (!encoded.success) {
        std::cerr
            << "[FAIL] encode: "
            << encoded.message
            << '\n';
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::
        RocketMQProducer producer(
            ProducerOptions(config)
        );

    if (!producer.Start()) {
        std::cerr << producer.LastError() << '\n';
        return 2;
    }

    tinyimx::eventing::PublishMessage message;

    message.event_id = event.event_id;
    message.topic =
        config.RocketMQ().message_topic;
    message.tag = event.event_type;
    message.message_key = event.event_id;
    message.body = encoded.encoded;

    const auto result =
        producer.Publish(message);

    if (!result.Published()) {
        std::cerr
            << "[FAIL] publish: "
            << result.message
            << '\n';
        producer.Stop();
        return 1;
    }

    std::cout
        << "[PASS] published"
        << ", event_id=" << event.event_id
        << ", broker_message_id="
        << result.broker_message_id
        << '\n';

    producer.Stop();
    return 0;
}



class RetryableFailurePublisher final
    : public tinyimx::eventing::EventPublisher {
public:
    tinyimx::eventing::PublishResult Publish(
        const tinyimx::eventing::PublishMessage& message
    ) override {
        ++calls_;
        last_event_id_ = message.event_id;

        tinyimx::eventing::PublishResult result;

        /*
         * M16-C3 E deterministic dependency seam.
         *
         * This represents a transient RocketMQ dependency failure AFTER
         * the Relay has claimed the real durable MySQL Outbox row.
         *
         * Production RocketMQProducer classification was audited
         * separately:
         *
         *   transport error / ambiguous send
         *       -> kRetryableFailure
         *
         * Here we inject exactly that contract without depending on the
         * RocketMQ SDK's unbounded endpoint bootstrap/reconnect behavior.
         */
        result.status =
            tinyimx::eventing::PublishStatus::
                kRetryableFailure;

        result.message =
            "M16-C3 injected transient RocketMQ dependency failure";

        return result;
    }

    int Calls() const noexcept {
        return calls_;
    }

    const std::string& LastEventId() const noexcept {
        return last_event_id_;
    }

private:
    int calls_{0};
    std::string last_event_id_;
};

int RelayUntilInjectedRetry(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] MySQL init\n";
        return 2;
    }

    tinyimx::outbox::OutboxRepository repository(
        &mysql_pool
    );

    RetryableFailurePublisher publisher;

    tinyimx::outbox::OutboxRelay relay(
        &repository,
        &publisher,
        RelayOptions(
            config,
            "m16c3-e-injected-retry"
        )
    );

    if (!relay.Start()) {
        std::cerr
            << "[FAIL] relay start: "
            << relay.LastError()
            << '\n';

        mysql_pool.Shutdown();
        return 2;
    }

    bool retry_observed = false;

    for (int i = 0; i < 200; ++i) {
        const auto stats = relay.Stats();

        if (stats.retry_total >= 1) {
            retry_observed = true;

            std::cout
                << "[OBSERVED] retryable publish failure"
                << ", retry_total="
                << stats.retry_total
                << ", publisher_calls="
                << publisher.Calls()
                << ", event_id="
                << publisher.LastEventId()
                << std::endl;

            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(50)
        );
    }

    const bool drained =
        relay.ShutdownGraceful(5000);

    const auto stats = relay.Stats();

    std::cout
        << "relay_until_injected_retry"
        << ", claimed=" << stats.claimed_total
        << ", published=" << stats.published_total
        << ", retry=" << stats.retry_total
        << ", quarantined="
        << stats.quarantined_total
        << ", publish_failure="
        << stats.publish_failure_total
        << ", publisher_calls="
        << publisher.Calls()
        << ", drained="
        << drained
        << '\n';

    mysql_pool.Shutdown();

    return (
        retry_observed &&
        publisher.Calls() >= 1 &&
        stats.claimed_total >= 1 &&
        stats.retry_total >= 1 &&
        stats.published_total == 0 &&
        stats.quarantined_total == 0 &&
        drained
    ) ? 0 : 1;
}

int RelayUntilRetry(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] MySQL init\n";
        return 2;
    }

    tinyimx::outbox::OutboxRepository repository(
        &mysql_pool
    );

    tinyimx::eventing::rocketmq_transport::
        RocketMQProducer producer(
            ProducerOptions(config)
        );

    if (!producer.Start()) {
        std::cerr
            << "[FAIL] producer start: "
            << producer.LastError()
            << '\n';
        mysql_pool.Shutdown();
        return 2;
    }

    tinyimx::outbox::OutboxRelay relay(
        &repository,
        &producer,
        RelayOptions(
            config,
            "m16c3-relay-unavailable"
        )
    );

    if (!relay.Start()) {
        std::cerr
            << "[FAIL] relay start: "
            << relay.LastError()
            << '\n';

        producer.Stop();
        mysql_pool.Shutdown();
        return 2;
    }

    bool saw_retry = false;

    for (int i = 0; i < 400; ++i) {
        const auto stats = relay.Stats();

        if (stats.retry_total >= 1) {
            saw_retry = true;

            std::cout
                << "[OBSERVED] relay_retry_total="
                << stats.retry_total
                << std::endl;

            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    const bool drained =
        relay.ShutdownGraceful(5000);

    const auto stats = relay.Stats();

    std::cout
        << "relay_until_retry"
        << ", retry=" << stats.retry_total
        << ", published=" << stats.published_total
        << ", publish_failure="
        << stats.publish_failure_total
        << ", quarantined="
        << stats.quarantined_total
        << ", drained=" << drained
        << '\n';

    producer.Stop();
    mysql_pool.Shutdown();

    return (
        saw_retry &&
        stats.published_total == 0 &&
        drained
    ) ? 0 : 1;
}

int ProjectorRecoverAfterDependency(
    const std::string& config_path
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    tinyimx::RedisConnectionPool redis_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "[FAIL] MySQL init\n";
        return 2;
    }

    if (!redis_pool.Initialize(config.Redis())) {
        std::cerr << "[FAIL] Redis init\n";
        mysql_pool.Shutdown();
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::
        RocketMQSimpleConsumer consumer(
            ConsumerOptions(config)
        );

    if (!consumer.Start()) {
        std::cerr
            << "[FAIL] consumer start: "
            << consumer.LastError()
            << '\n';

        redis_pool.Shutdown();
        mysql_pool.Shutdown();
        return 2;
    }

    CountingAckConsumer counting_consumer(
        &consumer
    );

    tinyimx::UnreadCountCache cache(
        &redis_pool
    );

    tinyimx::projection::unread::
        UnreadProjectionReader reader(
            &mysql_pool
        );

    tinyimx::projection::unread::
        UnreadProjector projector(
            &counting_consumer,
            &reader,
            &cache,
            ProjectorOptions(config)
        );

    if (!projector.Start()) {
        std::cerr
            << "[FAIL] projector start: "
            << projector.LastError()
            << '\n';

        consumer.Stop();
        redis_pool.Shutdown();
        mysql_pool.Shutdown();
        return 2;
    }

    std::cout
        << "[READY] projector-recover-after-dependency"
        << std::endl;

    bool saw_retryable = false;
    bool recovered = false;

    for (int i = 0; i < 600; ++i) {
        const auto stats = projector.Stats();

        if (!saw_retryable &&
            stats.retryable_failure_total >= 1) {

            saw_retryable = true;

            std::cout
                << "[OBSERVED] retryable_failure_total="
                << stats.retryable_failure_total
                << std::endl;
        }

        if (
            saw_retryable &&
            stats.reconciled_total >= 1 &&
            counting_consumer.AckSuccess() >= 1
        ) {
            recovered = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    projector.Shutdown();

    const auto stats = projector.Stats();

    std::cout
        << "projector_dependency_recovery"
        << ", received=" << stats.received_total
        << ", reconciled=" << stats.reconciled_total
        << ", process_failure="
        << stats.process_failure_total
        << ", receive_failure="
        << stats.receive_failure_total
        << ", retryable_failure="
        << stats.retryable_failure_total
        << ", poison_failure="
        << stats.poison_failure_total
        << ", ack_success="
        << counting_consumer.AckSuccess()
        << ", ack_failure="
        << counting_consumer.AckFailure()
        << '\n';

    consumer.Stop();
    redis_pool.Shutdown();
    mysql_pool.Shutdown();

    return (
        saw_retryable &&
        recovered &&
        counting_consumer.AckFailure() == 0
    ) ? 0 : 1;
}

int ProjectorPoisonObserver(
    const std::string& config_path,
    std::uint64_t expected_poison_failures
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    tinyimx::RedisConnectionPool redis_pool;

    if (!mysql_pool.Initialize(config.MySql()) ||
        !redis_pool.Initialize(config.Redis())) {
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::
        RocketMQSimpleConsumer consumer(
            ConsumerOptions(config)
        );

    if (!consumer.Start()) {
        std::cerr << consumer.LastError() << '\n';

        redis_pool.Shutdown();
        mysql_pool.Shutdown();
        return 2;
    }

    CountingAckConsumer counting_consumer(
        &consumer
    );

    tinyimx::UnreadCountCache cache(
        &redis_pool
    );

    tinyimx::projection::unread::
        UnreadProjectionReader reader(
            &mysql_pool
        );

    tinyimx::projection::unread::
        UnreadProjector projector(
            &counting_consumer,
            &reader,
            &cache,
            ProjectorOptions(config)
        );

    if (!projector.Start()) {
        consumer.Stop();
        redis_pool.Shutdown();
        mysql_pool.Shutdown();
        return 2;
    }

    std::cout
        << "[READY] projector-poison-observer"
        << std::endl;

    std::uint64_t last_poison = 0;
    bool poison_bound_observed = false;
    bool healthy_observed = false;

    for (int i = 0; i < 900; ++i) {
        const auto stats = projector.Stats();

        if (stats.poison_failure_total !=
            last_poison) {

            last_poison =
                stats.poison_failure_total;

            std::cout
                << "[OBSERVED] poison_failure_total="
                << last_poison
                << std::endl;
        }

        if (
            stats.poison_failure_total >=
                expected_poison_failures
        ) {
            poison_bound_observed = true;
        }

        if (
            stats.reconciled_total >= 1 &&
            counting_consumer.AckSuccess() >= 1
        ) {
            healthy_observed = true;
        }

        if (
            poison_bound_observed &&
            healthy_observed
        ) {
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    projector.Shutdown();

    const auto stats = projector.Stats();

    std::cout
        << "projector_poison_observer"
        << ", received=" << stats.received_total
        << ", reconciled=" << stats.reconciled_total
        << ", process_failure="
        << stats.process_failure_total
        << ", receive_failure="
        << stats.receive_failure_total
        << ", retryable_failure="
        << stats.retryable_failure_total
        << ", poison_failure="
        << stats.poison_failure_total
        << ", ack_success="
        << counting_consumer.AckSuccess()
        << ", ack_failure="
        << counting_consumer.AckFailure()
        << '\n';

    consumer.Stop();
    redis_pool.Shutdown();
    mysql_pool.Shutdown();

    return (
        poison_bound_observed &&
        healthy_observed
    ) ? 0 : 1;
}

int PublishPoison(
    const std::string& config_path,
    const std::string& event_id
) {
    Config config;

    if (!LoadConfig(config_path, &config)) {
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::
        RocketMQProducer producer(
            ProducerOptions(config)
        );

    if (!producer.Start()) {
        std::cerr
            << "[FAIL] producer start: "
            << producer.LastError()
            << '\n';

        return 2;
    }

    tinyimx::eventing::PublishMessage message;

    message.event_id = event_id;
    message.topic =
        config.RocketMQ().message_topic;

    /*
     * Keep transport metadata valid while deliberately corrupting
     * the event envelope. This guarantees the failure belongs to the
     * consumer's permanent-message classification rather than producer
     * metadata validation.
     */
    message.tag = "message.created.v1";
    message.message_key = event_id;
    message.body = "{broken-json";

    const auto result =
        producer.Publish(message);

    std::cout
        << "publish_poison"
        << ", status="
        << static_cast<int>(result.status)
        << ", broker_message_id="
        << result.broker_message_id
        << ", message="
        << result.message
        << '\n';

    producer.Stop();

    return result.Published() ? 0 : 1;
}


}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr
            << "usage:\n"
            << "  m16_fault_recovery_runtime_test "
               "<mode> <config> [args]\n";
        return 2;
    }

    const std::string mode = argv[1];
    const std::string config_path = argv[2];

    if (mode == "relay-before-publish") {
        return RelayBeforePublish(config_path);
    }

    if (mode == "relay-after-publish") {
        return RelayAfterPublish(config_path);
    }

    if (mode == "relay-normal-once") {
        return RelayNormalOnce(config_path);
    }

    if (mode == "projector-before-ack") {
        return ProjectorBeforeAck(config_path);
    }

    if (mode == "projector-normal-once") {
        return ProjectorNormalOnce(config_path);
    }

    if (mode == "relay-until-injected-retry") {
        return RelayUntilInjectedRetry(
            config_path
        );
    }

    if (mode == "relay-until-retry") {
        return RelayUntilRetry(config_path);
    }

    if (mode == "projector-recover-after-dependency") {
        return ProjectorRecoverAfterDependency(
            config_path
        );
    }

    if (mode == "projector-poison-observer") {
        if (argc != 4) {
            std::cerr
                << "projector-poison-observer requires "
                   "<expected_poison_failures>\n";
            return 2;
        }

        try {
            return ProjectorPoisonObserver(
                config_path,
                std::stoull(argv[3])
            );
        } catch (...) {
            return 2;
        }
    }

    if (mode == "publish-poison") {
        if (argc != 4) {
            std::cerr
                << "publish-poison requires <event_id>\n";
            return 2;
        }

        return PublishPoison(
            config_path,
            argv[3]
        );
    }

    if (mode == "publish-message-event") {
        if (argc != 6) {
            std::cerr
                << "publish-message-event requires "
                   "<message_id> <from_user_id> <to_user_id>\n";
            return 2;
        }

        try {
            return PublishMessageEvent(
                config_path,
                std::stoull(argv[3]),
                std::stoull(argv[4]),
                std::stoull(argv[5])
            );
        } catch (...) {
            std::cerr << "invalid numeric arguments\n";
            return 2;
        }
    }

    std::cerr << "unknown mode: " << mode << '\n';
    return 2;
}
