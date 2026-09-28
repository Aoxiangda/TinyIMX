#include "common/config/Config.h"
#include "common/concurrency/ThreadPool.h"
#include "common/logging/Logger.h"
#include "common/net/Buffer.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "common/net/TcpServer.h"
#include "common/observability/TelemetryRuntime.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

bool ConnectAndClose(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    const bool ok = ::connect(
        fd,
        reinterpret_cast<sockaddr*>(&address),
        static_cast<socklen_t>(sizeof(address))
    ) == 0;

    ::close(fd);
    return ok;
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string config_path =
        argc >= 2 ? argv[1] : "config/observability.example.json";

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "config load failed: " << config.LastError() << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 2;
    }

    tinyimx::TelemetryIdentity identity;
    identity.service_name = "tinyimx-observability-demo";
    identity.service_namespace = "tinyimx";
    identity.service_instance_id = config.App().instance_id;
    identity.deployment_environment = config.App().env;

    auto& telemetry = tinyimx::TelemetryRuntime::Instance();
    if (!telemetry.Initialize(config.Observability(), identity)) {
        std::cerr << "telemetry init failed\n";
        tinyimx::Logger::Instance().Shutdown();
        return 3;
    }

    tinyimx::ThreadPoolOptions options;
    options.name = "observability-demo-pool";
    options.worker_threads = 2;
    options.queue_capacity = 32;
    options.queue_full_policy = tinyimx::QueueFullPolicy::kBlock;
    options.enable_dynamic_resize = false;

    tinyimx::ThreadPool pool(options);
    if (!pool.Start()) {
        std::cerr << "thread pool start failed\n";
        telemetry.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 4;
    }

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 8; ++i) {
        futures.push_back(pool.Submit([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5 + i));
            return i;
        }));
    }
    for (auto& future : futures) {
        (void)future.get();
    }

    tinyimx::EventLoop loop;
    tinyimx::InetAddress address(config.Server().host, config.Server().port);
    tinyimx::TcpServer server(&loop, address, "observability-demo-server", 0);
    server.SetConnectionCallback([](const tinyimx::TcpConnectionPtr&) {});
    server.SetMessageCallback([](const tinyimx::TcpConnectionPtr&, tinyimx::Buffer* buffer) {
        if (buffer != nullptr) {
            buffer->RetrieveAll();
        }
    });

    if (!server.Start()) {
        std::cerr << "tcp server start failed\n";
        pool.Shutdown(tinyimx::ShutdownMode::kGraceful);
        telemetry.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 5;
    }

    bool client_ok = false;
    std::thread client([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client_ok = ConnectAndClose(static_cast<std::uint16_t>(config.Server().port));
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        loop.Quit();
    });

    loop.Loop();
    client.join();
    server.Stop();
    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);

    if (!client_ok) {
        std::cerr << "tcp client failed\n";
        telemetry.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 6;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(
        config.Observability().metric_export_interval_ms + 200
    ));

    const bool flush_ok = telemetry.ForceFlush();
    telemetry.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    std::cout << "M20_OBSERVABILITY_DEMO=PASS"
              << " flush=" << (flush_ok ? "true" : "false")
              << " endpoint=" << config.Observability().otlp_endpoint
              << '\n';
    return 0;
}
