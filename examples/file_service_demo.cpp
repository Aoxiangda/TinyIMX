#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "services/file/application/FileApplicationService.h"
#include "services/file/repository/FileRepositoryAdapter.h"
#include "services/file/server/FileServiceServer.h"
#include "services/file/service/FileServiceImpl.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/repository/FileRepository.h"

#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {
std::string ResolveListenTarget(int argc, char* argv[]) {
    if (argc >= 3) return argv[2];
    const char* env = std::getenv("TINYIMX_FILE_LISTEN_TARGET");
    return env && env[0] ? std::string(env) : std::string("127.0.0.1:50055");
}

tinyimx::registry::zookeeper::ServiceInstance BuildServiceInstance(
    const tinyimx::ZooKeeperConfig& config, int selected_port) {
    using tinyimx::registry::zookeeper::ServiceInstance;
    ServiceInstance instance;
    instance.service_name = "file";
    instance.target = ServiceInstance::BuildTarget(
        config.advertise_host, static_cast<std::uint16_t>(selected_port));
    instance.instance_id = ServiceInstance::BuildInstanceId(instance.service_name, instance.target);
    instance.version = config.service_version;
    return instance;
}
}  // namespace

int main(int argc, char* argv[]) {
    const std::string config_path = argc >= 2 ? argv[1] : "config/gateway.json";
    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) { std::cerr << "load config failed: " << config.LastError() << '\n'; return 1; }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 1;
    if (!config.MySql().enable) { LOG_ERROR("FileService requires mysql.enable=true"); return 1; }

    sigset_t signal_set; sigemptyset(&signal_set); sigaddset(&signal_set,SIGINT); sigaddset(&signal_set,SIGTERM);
    if (pthread_sigmask(SIG_BLOCK,&signal_set,nullptr) != 0) { LOG_ERROR("FileService failed to block shutdown signals"); return 1; }

    tinyimx::MySqlConnectionPool pool;
    if (!pool.Initialize(config.MySql())) { LOG_ERROR("FileService mysql pool initialize failed"); return 1; }
    tinyimx::FileRepository repository(&pool);
    tinyimx::file::FileRepositoryAdapter adapter(&repository, &pool);
    tinyimx::file::FileApplicationService application(&adapter);
    tinyimx::file::FileServiceImpl service_impl(&application);
    tinyimx::file::FileServiceServer server(&service_impl);
    const std::string target = ResolveListenTarget(argc, argv);
    if (!server.Start(target)) { LOG_ERROR("FileService start failed" << ", target=" << target); pool.Shutdown(); return 1; }

    std::unique_ptr<tinyimx::registry::zookeeper::ZooKeeperClient> zk_client;
    std::unique_ptr<tinyimx::registry::zookeeper::ZooKeeperServiceRegistrar> registrar;
    if (config.ZooKeeper().enable) {
        auto instance = BuildServiceInstance(config.ZooKeeper(), server.SelectedPort());
        if (!instance.Valid()) { LOG_ERROR("FileService ZooKeeper instance invalid"); server.Shutdown(); server.Wait(); pool.Shutdown(); return 1; }
        zk_client = std::make_unique<tinyimx::registry::zookeeper::ZooKeeperClient>();
        if (!zk_client->Start(config.ZooKeeper())) { LOG_ERROR("FileService ZooKeeper connect failed" << ", error=" << zk_client->LastError()); server.Shutdown(); server.Wait(); pool.Shutdown(); return 1; }
        registrar = std::make_unique<tinyimx::registry::zookeeper::ZooKeeperServiceRegistrar>(
            zk_client.get(), std::move(instance), config.ZooKeeper().service_root);
        if (!registrar->Start(std::chrono::milliseconds(config.ZooKeeper().connect_timeout_ms))) {
            LOG_ERROR("FileService ZooKeeper registration failed" << ", error=" << registrar->LastError());
            registrar->Stop(); zk_client->Stop(); server.Shutdown(); server.Wait(); pool.Shutdown(); return 1;
        }
    }

    LOG_INFO("FileService ready" << ", target=" << server.BoundTarget()
             << ", zookeeper_registered=" << (registrar ? 1 : 0));
    auto* registrar_for_signal = registrar.get();
    std::thread signal_thread([&server, registrar_for_signal, signal_set]() mutable {
        int signal_number = 0;
        if (sigwait(&signal_set,&signal_number) == 0) {
            if (registrar_for_signal) registrar_for_signal->Stop();
            server.Shutdown();
        }
    });
    server.Wait();
    if (signal_thread.joinable()) signal_thread.join();
    if (registrar) registrar->Stop();
    if (zk_client) zk_client->Stop();
    pool.Shutdown(); tinyimx::Logger::Instance().Shutdown();
    return 0;
}
