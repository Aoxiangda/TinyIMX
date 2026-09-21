#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/rpc/ZooKeeperServiceEndpointProvider.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
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
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
    config.snapshot_stale_after_ms = 600;
    config.retain_last_known_good = true;
    return config;
}

ServiceInstance MakeInstance(const std::string& service_name, int port) {
    ServiceInstance instance;
    instance.service_name = service_name;
    instance.target = "127.0.0.1:" + std::to_string(port);
    instance.instance_id = ServiceInstance::BuildInstanceId(
        instance.service_name,
        instance.target
    );
    instance.version = "v1";
    return instance;
}

bool HasTargets(
    const std::shared_ptr<ZooKeeperServiceDiscovery>& discovery,
    const std::vector<std::string>& expected,
    const std::string& service_name = "user"
) {
    const auto snapshot = discovery->Snapshot(service_name);
    if (!snapshot.initialized ||
        snapshot.instances.size() != expected.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (snapshot.instances[i].target != expected[i]) {
            return false;
        }
    }
    return true;
}

void Cleanup(
    ZooKeeperClient* client,
    const std::string& root
) {
    if (client == nullptr) {
        return;
    }
    client->DeleteNode(root + "/user", -1);
    client->DeleteNode(root + "/social", -1);
    client->DeleteNode(root + "/message", -1);
    client->DeleteNode(root + "/group", -1);
    client->DeleteNode(root + "/file", -1);
    client->DeleteNode(root, -1);

    const auto slash = root.rfind('/');
    if (slash != std::string::npos && slash > 0) {
        client->DeleteNode(root.substr(0, slash), -1);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string connect =
        argc >= 2 ? argv[1] : "127.0.0.1:2181";
    const std::string root =
        argc >= 3
            ? argv[2]
            : "/tinyimx-m15-b-" +
                  std::to_string(static_cast<long long>(::getpid())) +
                  "/services";

    const auto zk_config = MakeZooKeeperConfig(connect, root);
    const auto discovery_config = MakeDiscoveryConfig();

    std::cout
        << "========== TinyIMX M15-B ZooKeeper Discovery Integration =========="
        << '\n';

    auto discovery_client = std::make_shared<ZooKeeperClient>();
    if (!Expect(
            discovery_client->Start(zk_config),
            "discovery ZooKeeper session connected"
        )) {
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
            "initial discovery synchronization"
        ) ||
        !Expect(
            HasTargets(discovery, {}),
            "initial missing parent publishes authoritative empty user snapshot"
        ) ||
        !Expect(
            discovery->Snapshot("social").initialized &&
                discovery->Snapshot("social").instances.empty(),
            "initial social snapshot empty"
        ) ||
        !Expect(
            discovery->Snapshot("message").initialized &&
                discovery->Snapshot("message").instances.empty(),
            "initial message snapshot empty"
        ) ||
        !Expect(
            discovery->Snapshot("group").initialized &&
                discovery->Snapshot("group").instances.empty(),
            "initial group snapshot empty"
        ) ||
        !Expect(
            discovery->Snapshot("file").initialized &&
                discovery->Snapshot("file").instances.empty(),
            "initial file snapshot empty"
        )) {
        discovery->Stop();
        discovery_client->Stop();
        return 1;
    }

    ZooKeeperClient owner1;
    ZooKeeperClient owner2;
    ZooKeeperClient owner3;
    ZooKeeperClient group_owner;
    ZooKeeperClient file_owner;
    if (!Expect(owner1.Start(zk_config), "owner1 connected") ||
        !Expect(owner2.Start(zk_config), "owner2 connected") ||
        !Expect(owner3.Start(zk_config), "owner3 connected") ||
        !Expect(group_owner.Start(zk_config), "group owner connected") ||
        !Expect(file_owner.Start(zk_config), "file owner connected")) {
        discovery->Stop();
        discovery_client->Stop();
        return 1;
    }

    const auto u1 = MakeInstance("user", 56001);
    const auto u2 = MakeInstance("user", 56002);
    const auto u3 = MakeInstance("user", 56003);
    const auto g1 = MakeInstance("group", 56054);
    const auto f1 = MakeInstance("file", 56055);
    ZooKeeperServiceRegistrar r1(&owner1, u1, root);
    ZooKeeperServiceRegistrar r2(&owner2, u2, root);
    ZooKeeperServiceRegistrar r3(&owner3, u3, root);
    ZooKeeperServiceRegistrar group_registrar(&group_owner, g1, root);
    ZooKeeperServiceRegistrar file_registrar(&file_owner, f1, root);

    if (!Expect(
            r1.Start(std::chrono::milliseconds(3000)),
            "register U1"
        ) ||
        !Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {u1.target});
            }),
            "parent-create watch discovers U1"
        ) ||
        !Expect(
            r2.Start(std::chrono::milliseconds(3000)),
            "register U2"
        ) ||
        !Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {u1.target, u2.target});
            }),
            "child watch discovers U2"
        )) {
        r1.Stop(); r2.Stop(); r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        discovery_client->Stop();
        return 1;
    }

    if (!Expect(
            group_registrar.Start(std::chrono::milliseconds(3000)),
            "register GroupService"
        ) ||
        !Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {g1.target}, "group");
            }),
            "group watch discovers GroupService"
        )) {
        r1.Stop(); r2.Stop(); r3.Stop(); group_registrar.Stop(); file_registrar.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop(); group_owner.Stop(); file_owner.Stop();
        discovery_client->Stop();
        return 1;
    }

    if (!Expect(
            file_registrar.Start(std::chrono::milliseconds(3000)),
            "register FileService"
        ) ||
        !Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {f1.target}, "file");
            }),
            "file watch discovers FileService"
        )) {
        r1.Stop(); r2.Stop(); r3.Stop(); group_registrar.Stop(); file_registrar.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop(); group_owner.Stop(); file_owner.Stop();
        discovery_client->Stop();
        return 1;
    }

    ZooKeeperServiceEndpointProvider provider(
        discovery,
        discovery_config
    );
    const auto file_endpoint = provider.Resolve(ServiceKind::kFile);
    if (!Expect(
            file_endpoint && file_endpoint->target == f1.target,
            "ServiceKind::kFile resolves discovered FileService"
        )) {
        r1.Stop(); r2.Stop(); r3.Stop(); group_registrar.Stop(); file_registrar.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop(); group_owner.Stop(); file_owner.Stop();
        discovery_client->Stop();
        return 1;
    }
    const auto group_endpoint = provider.Resolve(ServiceKind::kGroup);
    if (!Expect(
            group_endpoint && group_endpoint->target == g1.target,
            "ServiceKind::kGroup resolves discovered GroupService"
        )) {
        r1.Stop(); r2.Stop(); r3.Stop(); group_registrar.Stop(); file_registrar.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop(); group_owner.Stop(); file_owner.Stop();
        discovery_client->Stop();
        return 1;
    }

    const auto rr1 = provider.Resolve(ServiceKind::kUser);
    const auto rr2 = provider.Resolve(ServiceKind::kUser);
    const auto rr3 = provider.Resolve(ServiceKind::kUser);
    const auto rr4 = provider.Resolve(ServiceKind::kUser);
    if (!Expect(
            rr1 && rr2 && rr3 && rr4 &&
            rr1->target == u1.target &&
            rr2->target == u2.target &&
            rr3->target == u1.target &&
            rr4->target == u2.target,
            "round-robin selects sorted U1/U2 deterministically"
        )) {
        r1.Stop(); r2.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        discovery_client->Stop();
        return 1;
    }

    const auto before_remove_generation =
        discovery->Snapshot("user").generation;
    r1.Stop();
    if (!Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {u2.target});
            }),
            "re-armed watch removes U1"
        ) ||
        !Expect(
            discovery->Snapshot("user").generation > before_remove_generation,
            "snapshot generation advances after membership removal"
        ) ||
        !Expect(
            r3.Start(std::chrono::milliseconds(3000)),
            "register U3"
        ) ||
        !Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {u2.target, u3.target});
            }),
            "second re-arm discovers U3"
        )) {
        r2.Stop(); r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        discovery_client->Stop();
        return 1;
    }

    const std::string bad_path = root + "/user/bad-node";
    if (!Expect(
            owner3.CreateEphemeral(bad_path, "{}") ==
                tinyimx::registry::zookeeper::OperationStatus::kOk,
            "inject malformed registration node"
        ) ||
        !Expect(
            WaitFor([&]() {
                const auto snapshot = discovery->Snapshot("user");
                return snapshot.last_refresh_ok &&
                       HasTargets(discovery, {u2.target, u3.target});
            }),
            "invalid metadata excluded without poisoning snapshot"
        )) {
        owner3.DeleteNode(bad_path, -1);
        r2.Stop(); r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        discovery_client->Stop();
        return 1;
    }
    owner3.DeleteNode(bad_path, -1);

    r2.Stop();
    if (!Expect(
            WaitFor([&]() {
                return HasTargets(discovery, {u3.target});
            }),
            "third watch event removes U2"
        )) {
        r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        discovery_client->Stop();
        return 1;
    }

    // Keep U3 alive while the discovery control-plane client is stopped. This
    // tests provider LKG/staleness semantics independently of service health.
    discovery_client->Stop();
    if (!Expect(
            WaitFor([&]() {
                return !discovery->Snapshot("user").last_refresh_ok;
            }, std::chrono::milliseconds(2000)),
            "control-plane loss marks snapshot refresh unhealthy"
        )) {
        r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        return 1;
    }

    const auto lkg = provider.Resolve(ServiceKind::kUser);
    if (!Expect(
            lkg && lkg->target == u3.target,
            "fresh last-known-good remains routable"
        )) {
        r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(850));
    if (!Expect(
            !provider.Resolve(ServiceKind::kUser).has_value(),
            "stale last-known-good is fenced"
        )) {
        r3.Stop();
        discovery->Stop();
        owner1.Stop(); owner2.Stop(); owner3.Stop();
        return 1;
    }

    discovery->Stop();
    r3.Stop();
    group_registrar.Stop(); file_registrar.Stop();
    Cleanup(&owner3, root);
    group_owner.Stop(); file_owner.Stop();
    owner1.Stop();
    owner2.Stop();
    owner3.Stop();

    std::cout
        << "===================================================================="
        << '\n';
    std::cout << "[PASS] M15-B ZooKeeper discovery integration tests\n";
    return 0;
}
