#include "common/config/Config.h"

#include <iostream>
#include <string>

namespace {

bool Expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        return false;
    }
    std::cout << "[PASS] " << name << '\n';
    return true;
}

bool Load(tinyimx::Config* config, const std::string& body) {
    return config != nullptr &&
           config->LoadFromString(body, "<config_service_discovery_test>");
}

bool ExpectFailure(
    const std::string& name,
    const std::string& body,
    const std::string& expected
) {
    tinyimx::Config config;
    if (Load(&config, body)) {
        std::cerr << "[FAIL] " << name << ": expected load failure\n";
        return false;
    }
    return Expect(config.LastError() == expected, name);
}

bool TestDefaults() {
    tinyimx::Config config;
    if (!Load(&config, "{}")) {
        std::cerr << "[FAIL] default config load: "
                  << config.LastError() << '\n';
        return false;
    }

    const auto& discovery = config.ServiceDiscovery();
    return
        Expect(discovery.provider == "static", "static provider by default") &&
        Expect(
            discovery.initial_sync_timeout_ms == 5000,
            "default initial sync timeout"
        ) &&
        Expect(
            discovery.snapshot_stale_after_ms == 30000,
            "default stale threshold"
        ) &&
        Expect(
            discovery.retain_last_known_good,
            "retain last-known-good by default"
        );
}

bool TestExplicitZooKeeperProvider() {
    tinyimx::Config config;
    if (!Load(
            &config,
            R"json({
  "zookeeper": {
    "enable": true
  },
  "service_discovery": {
    "provider": "zookeeper",
    "initial_sync_timeout_ms": 2500,
    "snapshot_stale_after_ms": 8000,
    "retain_last_known_good": false
  }
})json"
        )) {
        std::cerr << "[FAIL] explicit discovery config: "
                  << config.LastError() << '\n';
        return false;
    }

    const auto& discovery = config.ServiceDiscovery();
    return
        Expect(discovery.provider == "zookeeper", "zookeeper provider parsed") &&
        Expect(
            discovery.initial_sync_timeout_ms == 2500,
            "explicit initial sync timeout"
        ) &&
        Expect(
            discovery.snapshot_stale_after_ms == 8000,
            "explicit stale threshold"
        ) &&
        Expect(
            !discovery.retain_last_known_good,
            "explicit no-LKG mode"
        );
}

bool TestValidation() {
    return
        ExpectFailure(
            "reject unknown provider",
            R"json({"service_discovery":{"provider":"dns"}})json",
            "service_discovery.provider must be one of: static, zookeeper"
        ) &&
        ExpectFailure(
            "reject zero initial sync timeout",
            R"json({"service_discovery":{"initial_sync_timeout_ms":0}})json",
            "service_discovery.initial_sync_timeout_ms must be greater than 0"
        ) &&
        ExpectFailure(
            "reject zero stale threshold",
            R"json({"service_discovery":{"snapshot_stale_after_ms":0}})json",
            "service_discovery.snapshot_stale_after_ms must be greater than 0"
        ) &&
        ExpectFailure(
            "require ZooKeeper when discovery uses ZooKeeper",
            R"json({"service_discovery":{"provider":"zookeeper"}})json",
            "zookeeper.enable must be true when service_discovery.provider=zookeeper"
        );
}

}  // namespace

int main() {
    std::cout
        << "========== TinyIMX M15-B Service Discovery Config Tests =========="
        << '\n';
    const bool ok =
        TestDefaults() &&
        TestExplicitZooKeeperProvider() &&
        TestValidation();
    std::cout
        << "==================================================================="
        << '\n';
    if (!ok) {
        return 1;
    }
    std::cout << "[PASS] M15-B service discovery config tests\n";
    return 0;
}
