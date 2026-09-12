#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/repository/MessageRepository.h"
#include "services/outbox/OutboxRepository.h"

#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

std::string ResolveListenTarget(int argc, char* argv[]) {
    if (argc >= 3) {
        return argv[2];
    }

    const char* env =
        std::getenv("TINYIMX_MESSAGE_LISTEN_TARGET");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }

    return "127.0.0.1:50053";
}


tinyimx::registry::zookeeper::ServiceInstance
BuildServiceInstance(
    const tinyimx::ZooKeeperConfig& config,
    int selected_port
) {
    using tinyimx::registry::zookeeper::ServiceInstance;

    ServiceInstance instance;
    instance.service_name = "message";
    instance.target = ServiceInstance::BuildTarget(
        config.advertise_host,
        static_cast<std::uint16_t>(selected_port)
    );
    instance.instance_id = ServiceInstance::BuildInstanceId(
        instance.service_name,
        instance.target
    );
    instance.version = config.service_version;
    return instance;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";
    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "load config failed: "
                  << config.LastError() << '\n';
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 1;
    }

    if (!config.MySql().enable) {
        LOG_ERROR("MessageService requires mysql.enable=true");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);

    if (pthread_sigmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
        LOG_ERROR("MessageService failed to block shutdown signals");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    if (!mysql_pool.Initialize(config.MySql())) {
        LOG_ERROR("MessageService mysql pool initialize failed");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MessageRepository repository(&mysql_pool);
    tinyimx::outbox::OutboxRepository outbox_repository(&mysql_pool);
    tinyimx::message::MessageRepositoryAdapter repository_adapter(
        &repository,
        &mysql_pool,
        &outbox_repository
    );
    tinyimx::message::MessageApplicationService application_service(
        &repository_adapter
    );
    tinyimx::message::MessageServiceImpl service_impl(
        &application_service
    );
    tinyimx::message::MessageServiceServer server(&service_impl);

    const std::string listen_target =
        ResolveListenTarget(argc, argv);

    if (!server.Start(listen_target)) {
        LOG_ERROR(
            "MessageService start failed"
            << ", target=" << listen_target
        );
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    std::unique_ptr<tinyimx::registry::zookeeper::ZooKeeperClient>
        zookeeper_client;
    std::unique_ptr<
        tinyimx::registry::zookeeper::ZooKeeperServiceRegistrar
    > zookeeper_registrar;

    if (config.ZooKeeper().enable) {
        if (server.SelectedPort() <= 0 ||
            server.SelectedPort() > 65535) {
            LOG_ERROR("MessageService selected invalid gRPC port");
            server.Shutdown();
            server.Wait();
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        auto instance = BuildServiceInstance(
            config.ZooKeeper(),
            server.SelectedPort()
        );
        if (!instance.Valid()) {
            LOG_ERROR("MessageService ZooKeeper service instance invalid");
            server.Shutdown();
            server.Wait();
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        zookeeper_client = std::make_unique<
            tinyimx::registry::zookeeper::ZooKeeperClient
        >();
        if (!zookeeper_client->Start(config.ZooKeeper())) {
            LOG_ERROR(
                "MessageService ZooKeeper connect failed"
                << ", error=" << zookeeper_client->LastError()
            );
            server.Shutdown();
            server.Wait();
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        zookeeper_registrar = std::make_unique<
            tinyimx::registry::zookeeper::ZooKeeperServiceRegistrar
        >(
            zookeeper_client.get(),
            std::move(instance),
            config.ZooKeeper().service_root
        );
        if (!zookeeper_registrar->Start(
                std::chrono::milliseconds(
                    config.ZooKeeper().connect_timeout_ms
                )
            )) {
            LOG_ERROR(
                "MessageService ZooKeeper registration failed"
                << ", error=" << zookeeper_registrar->LastError()
            );
            zookeeper_registrar->Stop();
            zookeeper_client->Stop();
            server.Shutdown();
            server.Wait();
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }
    }

    LOG_INFO(
        "MessageService ready"
        << ", target=" << server.BoundTarget()
        << ", zookeeper_registered="
        << (zookeeper_registrar != nullptr ? 1 : 0)
        << (zookeeper_registrar != nullptr
                ? ", registry_path=" +
                    zookeeper_registrar->RegistrationPath()
                : std::string{})
    );

    auto* registrar_for_signal = zookeeper_registrar.get();

    std::thread signal_thread(
        [&server, registrar_for_signal, signal_set]() mutable {
            int signal_number = 0;
            if (sigwait(&signal_set, &signal_number) == 0) {
                LOG_INFO(
                    "MessageService shutdown signal received"
                    << ", signal=" << signal_number
                );
                if (registrar_for_signal != nullptr) {
                    registrar_for_signal->Stop();
                }
                server.Shutdown();
            }
        }
    );

    server.Wait();

    if (signal_thread.joinable()) {
        signal_thread.join();
    }

    if (zookeeper_registrar != nullptr) {
        zookeeper_registrar->Stop();
    }
    if (zookeeper_client != nullptr) {
        zookeeper_client->Stop();
    }

    mysql_pool.Shutdown();
    LOG_INFO("MessageService stopped");
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
