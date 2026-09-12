#include "common/config/Config.h"

#include <iostream>
#include <string>

namespace {
int g_failed = 0;
void Expect(bool condition, const std::string& name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
    } else {
        std::cerr << "[FAIL] " << name << '\n';
        ++g_failed;
    }
}

void TestDefaults() {
    tinyimx::Config config;
    Expect(config.LoadFromString("{}", "<m16-b-defaults>"), "M16-B default config loads");
    Expect(!config.RocketMQ().enable, "RocketMQ disabled by default");
    Expect(
        config.RocketMQ().message_topic == "tinyimx-message-events",
        "RocketMQ default topic is broker-legal"
    );
    Expect(!config.OutboxRelay().enable, "Outbox relay disabled by default");
    Expect(config.UnreadProjection().owner == "gateway", "Gateway remains default unread writer");
    Expect(!config.UnreadProjection().enable, "Unread projector disabled by default");
}

void TestExplicit() {
    tinyimx::Config config;
    const std::string body = R"json({
      "mysql":{"enable":true,"user":"root"},
      "redis":{"enable":true},
      "rocketmq":{"enable":true,"endpoint":"127.0.0.1:8081","message_topic":"tinyimx-message-events","request_timeout_ms":2500},
      "outbox_relay":{"enable":true,"instance_id":"relay-a","batch_size":16,"worker_threads":2,"max_inflight":64,"lease_ms":30000,"lease_renew_interval_ms":5000},
      "unread_projection":{"enable":true,"owner":"projector","consumer_group":"tinyimx-unread-projector-v1","batch_size":8}
    })json";
    Expect(config.LoadFromString(body, "<m16-b-explicit>"), "M16-B explicit config loads");
    if (config.IsLoaded()) {
        Expect(config.RocketMQ().request_timeout_ms == 2500, "RocketMQ timeout parsed");
        Expect(config.OutboxRelay().batch_size == 16, "Relay batch parsed");
        Expect(config.UnreadProjection().owner == "projector", "Projector ownership parsed");
    }
}

void TestInvalid() {
    tinyimx::Config config;
    Expect(
        !config.LoadFromString(R"json({"outbox_relay":{"enable":true}})json", "<relay-no-deps>"),
        "Relay fails closed without MySQL/RocketMQ"
    );
    Expect(
        !config.LoadFromString(R"json({"unread_projection":{"owner":"projector"}})json", "<projector-disabled>"),
        "Projector owner requires projector enable"
    );
    Expect(
        !config.LoadFromString(R"json({"unread_projection":{"enable":true,"owner":"invalid"}})json", "<owner-invalid>"),
        "Unknown unread projection owner rejected"
    );
    Expect(
        !config.LoadFromString(
            R"json({
                "mysql": {
                    "enable": true
                },
                "redis": {
                    "enable": true
                },
                "rocketmq": {
                    "enable": true,
                    "endpoint": "127.0.0.1:8081",
                    "message_topic": "tinyimx-message-events"
                },
                "unread_projection": {
                    "enable": true,
                    "owner": "projector",
                    "consumer_group":
                        "tinyimx-unread-projector-v1",
                    "invisible_duration_ms": 9999
                }
            })json",
            "<projector-illegal-invisibility>"
        ),
        "Projector rejects RocketMQ invisibility below 10 seconds"
    );

    Expect(
        !config.LoadFromString(
            R"json({"rocketmq":{"enable":true,"endpoint":"127.0.0.1:8081","message_topic":"tinyimx.message.events"}})json",
            "<rocketmq-illegal-topic>"
        ),
        "RocketMQ topic with dot is rejected before broker startup"
    );
}
}

int main() {
    std::cout << "========== TinyIMX M16-B Config Tests ==========\n";
    TestDefaults();
    TestExplicit();
    TestInvalid();
    std::cout << "================================================\n";
    return g_failed == 0 ? 0 : 1;
}
