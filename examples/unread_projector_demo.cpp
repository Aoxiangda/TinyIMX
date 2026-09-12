#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/projection/unread/UnreadProjectionReader.h"
#include "services/projection/unread/UnreadProjector.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {
std::atomic<bool> g_stop{false};
void HandleSignal(int) {
    g_stop.store(true, std::memory_order_release);
}
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::cerr
            << "usage: unread_projector_demo <config.json> "
               "[--rebuild-user <user_id>]\n";
        return 2;
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1])) {
        std::cerr << "config load failed: " << config.LastError() << '\n';
        return 2;
    }
    if (!config.UnreadProjection().enable) {
        std::cerr << "unread_projection.enable must be true\n";
        return 2;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    tinyimx::RedisConnectionPool redis_pool;
    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "MySQL pool init failed\n";
        return 2;
    }
    if (!redis_pool.Initialize(config.Redis())) {
        std::cerr << "Redis pool init failed\n";
        return 2;
    }

    tinyimx::UnreadCountCache cache(&redis_pool);
    tinyimx::projection::unread::UnreadProjectionReader reader(&mysql_pool);

    if (argc == 4) {
        if (std::string(argv[2]) != "--rebuild-user") {
            std::cerr << "unknown option: " << argv[2] << '\n';
            return 2;
        }
        std::uint64_t user_id = 0;
        try {
            user_id = std::stoull(argv[3]);
        } catch (...) {
            std::cerr << "invalid user_id\n";
            return 2;
        }

        // Rebuild does not need to consume MQ; use a null consumer only after
        // the durable snapshot has been applied below through a small local
        // projector composition with no run-loop start.
        class NoopConsumer final : public tinyimx::eventing::EventConsumer {
        public:
            tinyimx::eventing::EventReceiveResult Receive(std::size_t, int) override {
                return {};
            }
            tinyimx::eventing::EventAckResult Ack(
                const tinyimx::eventing::ConsumedEvent&
            ) override {
                return {};
            }
        } noop_consumer;

        tinyimx::projection::unread::UnreadProjectorOptions options;
        options.mode = tinyimx::projection::unread::UnreadProjectorMode::kWriter;
        tinyimx::projection::unread::UnreadProjector projector(
            &noop_consumer,
            &reader,
            &cache,
            options
        );
        std::string error;
        if (!projector.RebuildUser(user_id, &error)) {
            std::cerr << "rebuild failed: " << error << '\n';
            return 1;
        }
        std::cout << "unread projection rebuilt for user=" << user_id << '\n';
        return 0;
    }

    const auto& projection_config = config.UnreadProjection();
    if (projection_config.owner == "gateway" && !projection_config.shadow_mode) {
        std::cerr
            << "projector run rejected: owner=gateway requires shadow_mode=true\n";
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions consumer_options;
    consumer_options.endpoint = config.RocketMQ().endpoint;
    consumer_options.topic = config.RocketMQ().message_topic;
    consumer_options.consumer_group = projection_config.consumer_group;
    consumer_options.request_timeout_ms = config.RocketMQ().request_timeout_ms;
    consumer_options.await_duration_ms = projection_config.await_duration_ms;
    consumer_options.tls = config.RocketMQ().tls;
    consumer_options.access_key = config.RocketMQ().access_key;
    consumer_options.access_secret = config.RocketMQ().access_secret;

    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumer consumer(
        std::move(consumer_options)
    );
    if (!consumer.Start()) {
        std::cerr << consumer.LastError() << '\n';
        return 2;
    }

    tinyimx::projection::unread::UnreadProjectorOptions options;
    options.mode = projection_config.owner == "projector"
        ? tinyimx::projection::unread::UnreadProjectorMode::kWriter
        : tinyimx::projection::unread::UnreadProjectorMode::kShadow;
    options.topic = config.RocketMQ().message_topic;
    options.batch_size = projection_config.batch_size;
    options.invisible_duration_ms = projection_config.invisible_duration_ms;
    options.receive_error_backoff_ms = projection_config.receive_error_backoff_ms;

    tinyimx::projection::unread::UnreadProjector projector(
        &consumer,
        &reader,
        &cache,
        std::move(options)
    );
    if (!projector.Start()) {
        std::cerr << "projector start failed: " << projector.LastError() << '\n';
        return 2;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    projector.Shutdown();
    consumer.Stop();
    const auto stats = projector.Stats();
    std::cout
        << "unread projector stopped"
        << ", received=" << stats.received_total
        << ", reconciled=" << stats.reconciled_total
        << ", shadow_match=" << stats.shadow_match_total
        << ", shadow_mismatch=" << stats.shadow_mismatch_total
        << ", process_failure=" << stats.process_failure_total
        << ", ack_failure=" << stats.ack_failure_total
        << '\n';

    redis_pool.Shutdown();
    mysql_pool.Shutdown();
    return 0;
}
