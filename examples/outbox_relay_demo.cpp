#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/outbox/OutboxRelay.h"
#include "services/outbox/OutboxRepository.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};
void HandleSignal(int) {
    g_stop.store(true, std::memory_order_release);
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: outbox_relay_demo <config.json>\n";
        return 2;
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1])) {
        std::cerr << "config load failed: " << config.LastError() << '\n';
        return 2;
    }
    if (!config.OutboxRelay().enable) {
        std::cerr << "outbox_relay.enable must be true\n";
        return 2;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "MySQL pool init failed\n";
        return 2;
    }

    tinyimx::outbox::OutboxRepository repository(&mysql_pool);

    tinyimx::eventing::rocketmq_transport::RocketMQProducerOptions producer_options;
    producer_options.endpoint = config.RocketMQ().endpoint;
    producer_options.topic = config.RocketMQ().message_topic;
    producer_options.request_timeout_ms = config.RocketMQ().request_timeout_ms;
    producer_options.tls = config.RocketMQ().tls;
    producer_options.access_key = config.RocketMQ().access_key;
    producer_options.access_secret = config.RocketMQ().access_secret;

    tinyimx::eventing::rocketmq_transport::RocketMQProducer producer(
        std::move(producer_options)
    );
    if (!producer.Start()) {
        std::cerr << producer.LastError() << '\n';
        return 2;
    }

    const auto& relay_config = config.OutboxRelay();
    tinyimx::outbox::OutboxRelayOptions relay_options;
    relay_options.instance_id = relay_config.instance_id;
    relay_options.allowed_topic = config.RocketMQ().message_topic;
    relay_options.batch_size = relay_config.batch_size;
    relay_options.worker_threads = relay_config.worker_threads;
    relay_options.max_inflight = relay_config.max_inflight;
    relay_options.poll_interval_ms = relay_config.poll_interval_ms;
    relay_options.lease_ms = relay_config.lease_ms;
    relay_options.lease_renew_interval_ms = relay_config.lease_renew_interval_ms;
    relay_options.retry_base_ms = relay_config.retry_base_ms;
    relay_options.retry_max_ms = relay_config.retry_max_ms;
    relay_options.published_retention_hours = relay_config.published_retention_hours;
    relay_options.cleanup_interval_ms = relay_config.cleanup_interval_ms;
    relay_options.cleanup_batch_size = relay_config.cleanup_batch_size;

    tinyimx::outbox::OutboxRelay relay(
        &repository,
        &producer,
        std::move(relay_options)
    );
    if (!relay.Start()) {
        std::cerr << "relay start failed: " << relay.LastError() << '\n';
        return 2;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    const bool drained = relay.ShutdownGraceful(
        relay_config.shutdown_timeout_ms
    );
    const auto stats = relay.Stats();
    std::cout
        << "outbox relay stopped"
        << ", drained=" << drained
        << ", claimed=" << stats.claimed_total
        << ", published=" << stats.published_total
        << ", retry=" << stats.retry_total
        << ", quarantined=" << stats.quarantined_total
        << ", ownership_lost=" << stats.ownership_lost_total
        << '\n';

    producer.Stop();
    mysql_pool.Shutdown();
    return drained ? 0 : 1;
}
