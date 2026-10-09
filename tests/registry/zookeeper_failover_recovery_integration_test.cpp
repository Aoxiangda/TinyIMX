#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/rpc/ZooKeeperServiceEndpointProvider.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

using tinyimx::ServiceDiscoveryConfig;
using tinyimx::ZooKeeperConfig;
using tinyimx::registry::zookeeper::ServiceInstance;
using tinyimx::registry::zookeeper::ZooKeeperClient;
using tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery;
using tinyimx::registry::zookeeper::ZooKeeperServiceRegistrar;
using tinyimx::rpc::ServiceKind;
using tinyimx::rpc::ZooKeeperServiceEndpointProvider;

bool Expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        return false;
    }
    std::cout << "[PASS] " << name << '\n';
    return true;
}

bool WaitFor(
    const std::function<bool()>& predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)
) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return predicate();
}

ZooKeeperConfig MakeZooKeeperConfig(
    const std::string& connect,
    const std::string& root
) {
    ZooKeeperConfig config;
    config.enable = true;
    config.connect_string = connect;
    config.session_timeout_ms = 6000;
    config.connect_timeout_ms = 5000;
    config.service_root = root;
    config.advertise_host = "127.0.0.1";
    config.service_version = "v1";
    return config;
}

ServiceDiscoveryConfig MakeDiscoveryConfig() {
    ServiceDiscoveryConfig config;
    config.provider = "zookeeper";
    config.initial_sync_timeout_ms = 5000;
    config.snapshot_stale_after_ms = 3000;
    config.retain_last_known_good = true;
    return config;
}

ServiceInstance MakeUserInstance(int port) {
    ServiceInstance instance;
    instance.service_name = "user";
    instance.target = "127.0.0.1:" + std::to_string(port);
    instance.instance_id = ServiceInstance::BuildInstanceId(
        instance.service_name,
        instance.target
    );
    instance.version = "v1";
    return instance;
}

std::vector<std::string> Targets(
    const std::shared_ptr<ZooKeeperServiceDiscovery>& discovery
) {
    const auto snapshot = discovery->Snapshot("user");
    std::vector<std::string> targets;
    for (const auto& instance : snapshot.instances) {
        targets.push_back(instance.target);
    }
    return targets;
}

bool SnapshotEquals(
    const std::shared_ptr<ZooKeeperServiceDiscovery>& discovery,
    std::vector<std::string> expected
) {
    auto actual = Targets(discovery);
    std::sort(actual.begin(), actual.end());
    std::sort(expected.begin(), expected.end());
    return actual == expected;
}

bool SnapshotUnique(
    const std::shared_ptr<ZooKeeperServiceDiscovery>& discovery
) {
    const auto values = Targets(discovery);
    return std::set<std::string>(values.begin(), values.end()).size() ==
           values.size();
}

bool WaitForSnapshot(
    const std::shared_ptr<ZooKeeperServiceDiscovery>& discovery,
    const std::vector<std::string>& expected,
    std::uint64_t* previous_generation
) {
    if (!WaitFor([&]() {
            const auto snapshot = discovery->Snapshot("user");
            return snapshot.initialized &&
                   snapshot.last_refresh_ok &&
                   SnapshotEquals(discovery, expected) &&
                   SnapshotUnique(discovery) &&
                   snapshot.generation > *previous_generation;
        })) {
        return false;
    }
    *previous_generation = discovery->Snapshot("user").generation;
    return true;
}

void Cleanup(ZooKeeperClient* client, const std::string& root) {
    if (client == nullptr) {
        return;
    }
    client->DeleteNode(root + "/user", -1);
    client->DeleteNode(root + "/social", -1);
    client->DeleteNode(root + "/message", -1);
    client->DeleteNode(root, -1);
    const auto slash = root.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        client->DeleteNode(root.substr(0, slash), -1);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string connect = argc >= 2
        ? argv[1]
        : "127.0.0.1:22181,127.0.0.1:22182,127.0.0.1:22183";
    const std::string root = argc >= 3
        ? argv[2]
        : "/tinyimx-m15-c-" +
              std::to_string(static_cast<long long>(::getpid())) +
              "/services";

    const auto zk_config = MakeZooKeeperConfig(connect, root);
    const auto discovery_config = MakeDiscoveryConfig();

    std::cout
        << "========== TinyIMX M15-C Failover/Recovery Integration =========="
        << '\n';

    auto discovery_client = std::make_shared<ZooKeeperClient>();
    if (!Expect(discovery_client->Start(zk_config), "discovery client connected")) {
        std::cerr << discovery_client->LastError() << '\n';
        return 1;
    }

    auto discovery = std::make_shared<ZooKeeperServiceDiscovery>(
        discovery_client,
        root,
        discovery_config
    );
    if (!Expect(
            discovery->Start(std::chrono::milliseconds(5000)),
            "authoritative initial discovery sync"
        ) ||
        !Expect(
            SnapshotEquals(discovery, {}),
            "initial user membership empty"
        )) {
        discovery->Stop();
        discovery_client->Stop();
        return 1;
    }

    ZooKeeperClient owner_a;
    ZooKeeperClient owner_b;
    ZooKeeperClient owner_c;
    if (!Expect(owner_a.Start(zk_config), "owner A connected") ||
        !Expect(owner_b.Start(zk_config), "owner B connected") ||
        !Expect(owner_c.Start(zk_config), "owner C connected")) {
        discovery->Stop();
        discovery_client->Stop();
        return 1;
    }

    const auto a = MakeUserInstance(57001);
    const auto b = MakeUserInstance(57002);
    const auto c = MakeUserInstance(57003);
    ZooKeeperServiceRegistrar registrar_a(&owner_a, a, root);
    ZooKeeperServiceRegistrar registrar_b(&owner_b, b, root);
    ZooKeeperServiceRegistrar registrar_c(&owner_c, c, root);

    std::uint64_t generation = discovery->Snapshot("user").generation;

    auto fail_cleanup = [&]() {
        registrar_a.Stop(); registrar_b.Stop(); registrar_c.Stop();
        discovery->Stop();
        Cleanup(&owner_a, root);
        owner_a.Stop(); owner_b.Stop(); owner_c.Stop();
        discovery_client->Stop();
        return 1;
    };

    if (!Expect(
            registrar_a.Start(std::chrono::milliseconds(3000)),
            "churn step 1 register A"
        ) ||
        !Expect(
            WaitForSnapshot(discovery, {a.target}, &generation),
            "churn step 1 snapshot [A]"
        ) ||
        !Expect(
            registrar_b.Start(std::chrono::milliseconds(3000)),
            "churn step 2 register B"
        ) ||
        !Expect(
            WaitForSnapshot(discovery, {a.target, b.target}, &generation),
            "churn step 2 snapshot [A,B]"
        )) {
        return fail_cleanup();
    }

    registrar_a.Stop();
    if (!Expect(
            WaitForSnapshot(discovery, {b.target}, &generation),
            "churn step 3 snapshot [B]"
        ) ||
        !Expect(
            registrar_c.Start(std::chrono::milliseconds(3000)),
            "churn step 4 register C"
        ) ||
        !Expect(
            WaitForSnapshot(discovery, {b.target, c.target}, &generation),
            "churn step 4 snapshot [B,C]"
        )) {
        return fail_cleanup();
    }

    registrar_b.Stop();
    if (!Expect(
            WaitForSnapshot(discovery, {c.target}, &generation),
            "churn step 5 snapshot [C]"
        ) ||
        !Expect(
            registrar_a.Start(std::chrono::milliseconds(3000)),
            "churn step 6 re-register A"
        ) ||
        !Expect(
            WaitForSnapshot(discovery, {a.target, c.target}, &generation),
            "churn step 6 snapshot [A,C]"
        )) {
        return fail_cleanup();
    }

    registrar_c.Stop();
    if (!Expect(
            WaitForSnapshot(discovery, {a.target}, &generation),
            "churn step 7 snapshot [A]"
        )) {
        return fail_cleanup();
    }

    ZooKeeperServiceEndpointProvider provider(discovery, discovery_config);
    const auto route = provider.Resolve(ServiceKind::kUser);
    if (!Expect(
            route && route->target == a.target,
            "routing never returns a converged-away endpoint"
        )) {
        return fail_cleanup();
    }

    // Exercise the Discovery object's lifecycle fence. Membership is changed
    // while the consumer is stopped; a subsequent Start() must not reuse the
    // old initialized snapshot to declare readiness early.
    discovery->Stop();
    registrar_a.Stop();
    if (!Expect(
            registrar_b.Start(std::chrono::milliseconds(3000)),
            "register B while discovery stopped"
        ) ||
        !Expect(
            discovery->Start(std::chrono::milliseconds(5000)),
            "discovery restart performs full authoritative sync"
        ) ||
        !Expect(
            SnapshotEquals(discovery, {b.target}),
            "discovery restart discards prior snapshot and converges to [B]"
        )) {
        return fail_cleanup();
    }

    registrar_b.Stop();
    discovery->Stop();
    Cleanup(&owner_a, root);
    owner_a.Stop(); owner_b.Stop(); owner_c.Stop();
    discovery_client->Stop();

    std::cout
        << "================================================================="
        << '\n';
    std::cout << "[PASS] M15-C failover/recovery integration tests\n";
    return 0;
}
