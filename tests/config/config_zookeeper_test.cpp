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
           config->LoadFromString(body, "<config_zookeeper_test>");
}

bool ExpectFailure(
    const std::string& name,
    const std::string& body,
    const std::string& expected
) {
    tinyimx::Config config;
    if (Load(&config, body)) {
        std::cerr << "[FAIL] " << name
                  << ": expected load failure\n";
        return false;
    }
    if (config.LastError() != expected) {
        std::cerr << "[FAIL] " << name
                  << ": expected=\"" << expected
                  << "\", actual=\"" << config.LastError()
                  << "\"\n";
        return false;
    }
    std::cout << "[PASS] " << name << '\n';
    return true;
}

bool TestDefaults() {
    tinyimx::Config config;
    if (!Load(&config, "{}")) {
        std::cerr << "[FAIL] default config load: "
                  << config.LastError() << '\n';
        return false;
    }
    const auto& zk = config.ZooKeeper();
    return
        Expect(!zk.enable, "zookeeper disabled by default") &&
        Expect(
            zk.connect_string == "127.0.0.1:2181",
            "zookeeper default connect string"
        ) &&
        Expect(
            zk.session_timeout_ms == 10000,
            "zookeeper default session timeout"
        ) &&
        Expect(
            zk.connect_timeout_ms == 5000,
            "zookeeper default connect timeout"
        ) &&
        Expect(
            zk.service_root == "/tinyimx/services",
            "zookeeper default service root"
        ) &&
        Expect(
            zk.advertise_host == "127.0.0.1",
            "zookeeper default advertise host"
        ) &&
        Expect(
            zk.service_version == "v1",
            "zookeeper default service version"
        );
}

bool TestExplicitConfig() {
    tinyimx::Config config;
    if (!Load(
            &config,
            R"json({
  "zookeeper": {
    "enable": true,
    "connect_string": "10.0.0.1:2181,10.0.0.2:2181",
    "session_timeout_ms": 12000,
    "connect_timeout_ms": 4000,
    "service_root": "/tinyimx/prod/services",
    "advertise_host": "10.10.0.8",
    "service_version": "v2"
  }
})json"
        )) {
        std::cerr << "[FAIL] explicit zookeeper config: "
                  << config.LastError() << '\n';
        return false;
    }
    const auto& zk = config.ZooKeeper();
    return
        Expect(zk.enable, "zookeeper explicit enable") &&
        Expect(
            zk.connect_string ==
                "10.0.0.1:2181,10.0.0.2:2181",
            "zookeeper explicit connect string"
        ) &&
        Expect(
            zk.session_timeout_ms == 12000,
            "zookeeper explicit session timeout"
        ) &&
        Expect(
            zk.connect_timeout_ms == 4000,
            "zookeeper explicit connect timeout"
        ) &&
        Expect(
            zk.service_root == "/tinyimx/prod/services",
            "zookeeper explicit service root"
        ) &&
        Expect(
            zk.advertise_host == "10.10.0.8",
            "zookeeper explicit advertise host"
        ) &&
        Expect(
            zk.service_version == "v2",
            "zookeeper explicit service version"
        );
}

bool TestValidation() {
    return
        ExpectFailure(
            "reject empty connect string",
            R"json({"zookeeper":{"enable":true,"connect_string":""}})json",
            "zookeeper.connect_string cannot be empty when zookeeper.enable=true"
        ) &&
        ExpectFailure(
            "reject zero session timeout",
            R"json({"zookeeper":{"enable":true,"session_timeout_ms":0}})json",
            "zookeeper.session_timeout_ms must be greater than 0"
        ) &&
        ExpectFailure(
            "reject zero connect timeout",
            R"json({"zookeeper":{"enable":true,"connect_timeout_ms":0}})json",
            "zookeeper.connect_timeout_ms must be greater than 0"
        ) &&
        ExpectFailure(
            "reject relative service root",
            R"json({"zookeeper":{"enable":true,"service_root":"tinyimx/services"}})json",
            "zookeeper.service_root must be an absolute, non-root path without a trailing or duplicate slash"
        ) &&
        ExpectFailure(
            "reject wildcard advertise host",
            R"json({"zookeeper":{"enable":true,"advertise_host":"0.0.0.0"}})json",
            "zookeeper.advertise_host cannot be a wildcard listen address"
        ) &&
        ExpectFailure(
            "reject empty service version",
            R"json({"zookeeper":{"enable":true,"service_version":""}})json",
            "zookeeper.service_version cannot be empty when zookeeper.enable=true"
        );
}

}  // namespace

int main() {
    std::cout << "========== TinyIMX M15-A ZooKeeper Config Tests ==========\n";
    const bool ok =
        TestDefaults() &&
        TestExplicitConfig() &&
        TestValidation();
    std::cout << "==========================================================\n";
    if (!ok) {
        return 1;
    }
    std::cout << "[PASS] M15-A ZooKeeper config tests\n";
    return 0;
}
