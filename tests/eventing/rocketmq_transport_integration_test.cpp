#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/eventing/EventCodec.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

int main(int argc, char** argv) {
    std::string config_path = "config/gateway-a.local.json";
    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "[FAIL] config load: " << config.LastError() << '\n';
        return 1;
    }
    if (!config.RocketMQ().enable) {
        std::cerr << "[FAIL] rocketmq.enable must be true\n";
        return 1;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "[FAIL] logger init\n";
        return 1;
    }

    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const std::string event_id =
        "m16b.transport.smoke:" + std::to_string(static_cast<long long>(now));

    tinyimx::eventing::DomainEvent event;
    event.schema_version = 1;
    event.event_id = event_id;
    event.event_type = "m16b.transport.smoke.v1";
    event.aggregate_type = "test";
    event.aggregate_id = event_id;
    event.producer_service = "m16b-transport-test";
    event.occurred_at = "2026-09-09T00:00:00.000Z";
    event.payload = {{"probe", true}};
    const auto encoded = tinyimx::eventing::EventCodec::Encode(event);
    if (!encoded.success) {
        std::cerr << "[FAIL] event encode: " << encoded.message << '\n';
        return 1;
    }

    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions consumer_options;
    consumer_options.endpoint = config.RocketMQ().endpoint;
    consumer_options.topic = config.RocketMQ().message_topic;
    consumer_options.consumer_group =
        "tinyimx-m16b-transport-smoke-v1";
    consumer_options.request_timeout_ms = config.RocketMQ().request_timeout_ms;
    consumer_options.await_duration_ms = 10000;
    consumer_options.tls = config.RocketMQ().tls;
    consumer_options.access_key = config.RocketMQ().access_key;
    consumer_options.access_secret = config.RocketMQ().access_secret;

    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumer consumer(
        std::move(consumer_options)
    );
    if (!consumer.Start()) {
        std::cerr << "[FAIL] consumer start: " << consumer.LastError() << '\n';
        return 1;
    }

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
        std::cerr << "[FAIL] producer start: " << producer.LastError() << '\n';
        return 1;
    }

    tinyimx::eventing::PublishMessage publish;
    publish.event_id = event_id;
    publish.topic = config.RocketMQ().message_topic;
    publish.tag = event.event_type;
    publish.message_key = event_id;
    publish.body = encoded.encoded;
    const auto sent = producer.Publish(publish);
    if (!sent.Published()) {
        std::cerr << "[FAIL] RocketMQ publish: " << sent.message << '\n';
        return 1;
    }
    std::cout << "[PASS] RocketMQ producer publish, message_id="
              << sent.broker_message_id << '\n';

    bool observed = false;
    for (int attempt = 0; attempt < 30 && !observed; ++attempt) {
        const auto received = consumer.Receive(32, 20000);
        if (!received.success) {
            std::cerr << "[WARN] receive attempt failed: " << received.message << '\n';
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        for (const auto& item : received.events) {
            const bool is_probe =
                std::find(item.keys.begin(), item.keys.end(), event_id) != item.keys.end();
            const auto ack = consumer.Ack(item);
            if (!ack.success) {
                std::cerr << "[FAIL] ACK: " << ack.message << '\n';
                return 1;
            }
            if (is_probe) {
                observed = item.topic == publish.topic &&
                           item.tag == publish.tag &&
                           item.body == publish.body;
            }
        }
    }

    producer.Stop();
    consumer.Stop();
    tinyimx::Logger::Instance().Shutdown();

    if (!observed) {
        std::cerr << "[FAIL] RocketMQ SimpleConsumer did not observe published probe\n";
        return 1;
    }
    std::cout << "[PASS] RocketMQ SimpleConsumer receive + ACK preserves topic/tag/key/body\n";
    return 0;
}
