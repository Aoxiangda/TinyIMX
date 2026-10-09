#include "common/config/ConfigTypes.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

using tinyimx::ZooKeeperConfig;
using tinyimx::registry::zookeeper::NodeRecord;
using tinyimx::registry::zookeeper::OperationStatus;
using tinyimx::registry::zookeeper::ZooKeeperClient;

void Usage(const char* program) {
    std::cerr
        << "usage: " << program
        << " <connect> <path> <present|absent> [timeout_ms]\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        Usage(argv[0]);
        return 2;
    }

    const std::string connect = argv[1];
    const std::string path = argv[2];
    const std::string expectation = argv[3];
    const int timeout_ms =
        argc >= 5 ? std::atoi(argv[4]) : 5000;

    if (expectation != "present" && expectation != "absent") {
        Usage(argv[0]);
        return 2;
    }

    ZooKeeperConfig config;
    config.enable = true;
    config.connect_string = connect;
    config.session_timeout_ms = 6000;
    config.connect_timeout_ms = std::max(timeout_ms, 1000);

    ZooKeeperClient client;
    if (!client.Start(config)) {
        std::cerr << "connect failed: " << client.LastError() << '\n';
        return 1;
    }

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    NodeRecord node;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto status = client.GetNode(path, &node);
        if (expectation == "present" &&
            status == OperationStatus::kOk) {
            std::cout
                << "path=" << path << '\n'
                << "ephemeral_owner=" << node.ephemeral_owner << '\n'
                << "version=" << node.version << '\n'
                << "data=" << node.data << '\n';
            client.Stop();
            return 0;
        }
        if (expectation == "absent" &&
            status == OperationStatus::kNoNode) {
            std::cout << "path_absent=" << path << '\n';
            client.Stop();
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cerr
        << "timeout waiting for path expectation"
        << ", path=" << path
        << ", expected=" << expectation << '\n';
    client.Stop();
    return 1;
}
