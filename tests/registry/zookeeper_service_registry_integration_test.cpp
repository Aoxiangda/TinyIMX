#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/registry/zookeeper/ZooKeeperTypes.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

namespace {

using tinyimx::ZooKeeperConfig;
using tinyimx::registry::zookeeper::NodeRecord;
using tinyimx::registry::zookeeper::OperationStatus;
using tinyimx::registry::zookeeper::ServiceInstance;
using tinyimx::registry::zookeeper::ZooKeeperClient;
using tinyimx::registry::zookeeper::ZooKeeperServiceRegistrar;

bool Expect(bool condition, const std::string& name) {
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        return false;
    }
    std::cout << "[PASS] " << name << '\n';
    return true;
}

ZooKeeperConfig MakeConfig(
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

ServiceInstance MakeInstance(const std::string& suffix) {
    ServiceInstance instance;
    instance.service_name = "message";
    instance.target = "127.0.0.1:55053";
    instance.instance_id =
        "message@127.0.0.1:55053-" + suffix;
    instance.version = "v1";
    return instance;
}

void CleanupParents(
    ZooKeeperClient* client,
    const std::string& root
) {
    if (client == nullptr) {
        return;
    }
    client->DeleteNode(root + "/message", -1);
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
            : "/tinyimx-m15-a-test/services";

    const auto config = MakeConfig(connect, root);
    const auto instance = MakeInstance(
        std::to_string(static_cast<long long>(::getpid()))
    );
    const std::string path = instance.BuildPath(root);

    std::cout
        << "========== TinyIMX M15-A ZooKeeper Integration Tests ==========\n";

    ZooKeeperClient owner;
    if (!Expect(owner.Start(config), "owner ZooKeeper session connected")) {
        std::cerr << "error=" << owner.LastError() << '\n';
        return 1;
    }

    ZooKeeperServiceRegistrar registrar(
        &owner,
        instance,
        root
    );
    if (!Expect(
            registrar.Start(std::chrono::milliseconds(5000)),
            "ephemeral service registration confirmed"
        )) {
        std::cerr << "error=" << registrar.LastError() << '\n';
        owner.Stop();
        return 1;
    }

    NodeRecord node;
    if (!Expect(
            owner.GetNode(path, &node) == OperationStatus::kOk,
            "registered znode readable"
        ) ||
        !Expect(
            node.ephemeral_owner == owner.SessionId() &&
            node.ephemeral_owner != 0,
            "ephemeralOwner matches owner session"
        ) ||
        !Expect(
            node.data == instance.Serialize(),
            "registered metadata exact"
        )) {
        registrar.Stop();
        owner.Stop();
        return 1;
    }

    ZooKeeperClient contender;
    if (!Expect(
            contender.Start(config),
            "contender ZooKeeper session connected"
        )) {
        registrar.Stop();
        owner.Stop();
        return 1;
    }

    ZooKeeperServiceRegistrar conflicting(
        &contender,
        instance,
        root
    );
    if (!Expect(
            !conflicting.Start(std::chrono::milliseconds(1500)),
            "different session cannot steal deterministic path"
        ) ||
        !Expect(
            conflicting.LastError().find("another ZooKeeper session") !=
                std::string::npos,
            "ownership conflict is explicit"
        )) {
        conflicting.Stop();
        contender.Stop();
        registrar.Stop();
        owner.Stop();
        return 1;
    }

    registrar.Stop();
    if (!Expect(
            contender.GetNode(path, &node) == OperationStatus::kNoNode,
            "graceful unregister removes owned znode"
        )) {
        contender.Stop();
        owner.Stop();
        return 1;
    }

    ZooKeeperServiceRegistrar takeover(
        &contender,
        instance,
        root
    );
    if (!Expect(
            takeover.Start(std::chrono::milliseconds(3000)),
            "replacement session registers after owner release"
        )) {
        std::cerr << "error=" << takeover.LastError() << '\n';
        contender.Stop();
        owner.Stop();
        return 1;
    }

    takeover.Stop();
    CleanupParents(&contender, root);
    contender.Stop();
    owner.Stop();

    std::cout << "=================================================================\n";
    std::cout << "[PASS] M15-A ZooKeeper registry integration tests\n";
    return 0;
}
