#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

using tinyimx::ServiceDiscoveryConfig;
using tinyimx::ZooKeeperConfig;
using tinyimx::registry::zookeeper::ZooKeeperClient;
using tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery;

void Usage(const char* argv0) {
    std::cerr
        << "usage: " << argv0
        << " <connect> <service_root> <service> <mode> [value] [timeout_ms]\n"
        << "modes:\n"
        << "  contains <target>\n"
        << "  absent <target>\n"
        << "  count <n>\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 5) {
        Usage(argv[0]);
        return 2;
    }

    const std::string connect = argv[1];
    const std::string root = argv[2];
    const std::string service = argv[3];
    const std::string mode = argv[4];
    const std::string value = argc >= 6 ? argv[5] : "";
    const int timeout_ms = argc >= 7 ? std::atoi(argv[6]) : 5000;

    ZooKeeperConfig zk;
    zk.enable = true;
    zk.connect_string = connect;
    zk.session_timeout_ms = 6000;
    zk.connect_timeout_ms = 5000;
    zk.service_root = root;
    zk.advertise_host = "127.0.0.1";
    zk.service_version = "v1";

    ServiceDiscoveryConfig discovery_config;
    discovery_config.provider = "zookeeper";
    discovery_config.initial_sync_timeout_ms = 5000;
    discovery_config.snapshot_stale_after_ms = 30000;
    discovery_config.retain_last_known_good = true;

    auto client = std::make_shared<ZooKeeperClient>();
    if (!client->Start(zk)) {
        std::cerr << "discovery_probe_error=" << client->LastError() << '\n';
        return 1;
    }
    auto discovery = std::make_shared<ZooKeeperServiceDiscovery>(
        client,
        root,
        discovery_config
    );
    if (!discovery->Start(std::chrono::milliseconds(5000))) {
        std::cerr << "discovery_probe_error=" << discovery->LastError() << '\n';
        discovery->Stop();
        client->Stop();
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    bool matched = false;
    do {
        const auto snapshot = discovery->Snapshot(service);
        if (snapshot.initialized && snapshot.last_refresh_ok) {
            bool contains = false;
            for (const auto& instance : snapshot.instances) {
                if (instance.target == value) {
                    contains = true;
                    break;
                }
            }
            if (mode == "contains") {
                matched = contains;
            } else if (mode == "absent") {
                matched = !contains;
            } else if (mode == "count") {
                matched = static_cast<int>(snapshot.instances.size()) ==
                    std::atoi(value.c_str());
            } else {
                Usage(argv[0]);
                discovery->Stop();
                client->Stop();
                return 2;
            }
            if (matched) {
                std::cout << "snapshot_generation=" << snapshot.generation
                          << " count=" << snapshot.instances.size()
                          << " service=" << service << '\n';
                for (const auto& instance : snapshot.instances) {
                    std::cout << "target=" << instance.target
                              << " instance_id=" << instance.instance_id
                              << '\n';
                }
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);

    discovery->Stop();
    client->Stop();
    if (!matched) {
        std::cerr << "discovery_probe_timeout mode=" << mode
                  << " value=" << value
                  << " service=" << service << '\n';
        return 1;
    }
    return 0;
}
