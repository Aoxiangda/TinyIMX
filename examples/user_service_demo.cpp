#include "common/config/Config.h"
#include "common/concurrency/ThreadPool.h"
#include "common/concurrency/ThreadPoolTypes.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "common/observability/ProcessTelemetry.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/repository/UserRepository.h"
#include "services/user/application/UserApplicationService.h"
#include "services/user/repository/UserRepositoryAdapter.h"
#include "services/user/server/UserServiceServer.h"
#include "services/user/service/UserServiceImpl.h"

#include <pthread.h>
#include <signal.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {

std::string ResolveListenTarget(
    int argc,
    char* argv[]
) {
    if (argc >= 3) {
        return argv[2];
    }

    const char* env =
        std::getenv("TINYIMX_USER_LISTEN_TARGET");

    if (env != nullptr && env[0] != '\0') {
        return env;
    }

    return "127.0.0.1:50052";
}


int ResolvePositiveEnv(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }

    try {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

tinyimx::registry::zookeeper::ServiceInstance
BuildServiceInstance(
    const tinyimx::ZooKeeperConfig& config,
    int selected_port
) {
    using tinyimx::registry::zookeeper::ServiceInstance;

    ServiceInstance instance;
    instance.service_name = "user";
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

    tinyimx::ProcessTelemetry process_telemetry;
    if (!process_telemetry.Initialize(
            config,
            "tinyimx-user-service",
            "user"
        )) {
        LOG_ERROR("tinyimx-user-service observability initialization failed");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!config.MySql().enable) {
        LOG_ERROR("UserService requires mysql.enable=true");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);

    if (pthread_sigmask(
            SIG_BLOCK,
            &signal_set,
            nullptr
        ) != 0) {
        LOG_ERROR("UserService failed to block shutdown signals");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        LOG_ERROR("UserService mysql pool initialize failed");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::UserRepository repository(&mysql_pool);
    tinyimx::user::UserRepositoryAdapter repository_adapter(
        &repository
    );
    tinyimx::user::UserApplicationService application_service(
        &repository_adapter
    );

    tinyimx::ThreadPoolOptions metadata_options;
    metadata_options.name = "user-login-metadata";
    metadata_options.worker_threads = 1;
    metadata_options.queue_capacity = 2048;
    metadata_options.queue_full_policy = tinyimx::QueueFullPolicy::kDiscard;
    metadata_options.enable_dynamic_resize = false;
    tinyimx::ThreadPool metadata_executor(metadata_options);
    if (!metadata_executor.Start()) {
        LOG_ERROR("UserService login metadata executor start failed");
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::user::UserServiceImpl service_impl(
        &application_service,
        &metadata_executor
    );
    tinyimx::user::UserServiceServer server(&service_impl);

    const unsigned int hw = std::max(1u, std::thread::hardware_concurrency());
    tinyimx::user::UserServiceServerOptions grpc_options;

    // gRPC sync-server min/max pollers are PER completion queue. Keep the
    // aggregate maximum close to the available CPU count because Authenticate
    // performs CPU-bound PBKDF2 work; oversubscribing it only increases tail
    // latency and context switching.
    const int default_num_cqs = std::max(
        1,
        std::min(2, static_cast<int>(hw / 4))
    );
    const int default_max_pollers_per_cq = std::max(
        1,
        static_cast<int>((hw + static_cast<unsigned int>(default_num_cqs) - 1) /
                         static_cast<unsigned int>(default_num_cqs))
    );
    const int default_min_pollers_per_cq = std::max(
        1,
        std::min(2, default_max_pollers_per_cq)
    );

    grpc_options.sync_num_cqs = ResolvePositiveEnv(
        "TINYIMX_USER_GRPC_NUM_CQS",
        default_num_cqs
    );
    grpc_options.sync_min_pollers = ResolvePositiveEnv(
        "TINYIMX_USER_GRPC_MIN_POLLERS",
        default_min_pollers_per_cq
    );
    grpc_options.sync_max_pollers = ResolvePositiveEnv(
        "TINYIMX_USER_GRPC_MAX_POLLERS",
        std::max(
            grpc_options.sync_min_pollers,
            default_max_pollers_per_cq
        )
    );

    const std::string listen_target =
        ResolveListenTarget(argc, argv);

    if (!server.Start(listen_target, grpc_options)) {
        LOG_ERROR(
            "UserService start failed"
            << ", target=" << listen_target
        );
        metadata_executor.Shutdown(
            tinyimx::ShutdownMode::kGraceful,
            std::chrono::milliseconds(5000)
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
            LOG_ERROR("UserService selected invalid gRPC port");
            server.Shutdown();
            server.Wait();
            metadata_executor.Shutdown(
                tinyimx::ShutdownMode::kGraceful,
                std::chrono::milliseconds(5000)
            );
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        auto instance = BuildServiceInstance(
            config.ZooKeeper(),
            server.SelectedPort()
        );
        if (!instance.Valid()) {
            LOG_ERROR("UserService ZooKeeper service instance invalid");
            server.Shutdown();
            server.Wait();
            metadata_executor.Shutdown(
                tinyimx::ShutdownMode::kGraceful,
                std::chrono::milliseconds(5000)
            );
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        zookeeper_client = std::make_unique<
            tinyimx::registry::zookeeper::ZooKeeperClient
        >();
        if (!zookeeper_client->Start(config.ZooKeeper())) {
            LOG_ERROR(
                "UserService ZooKeeper connect failed"
                << ", error=" << zookeeper_client->LastError()
            );
            server.Shutdown();
            server.Wait();
            metadata_executor.Shutdown(
                tinyimx::ShutdownMode::kGraceful,
                std::chrono::milliseconds(5000)
            );
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
                    config.ZooKeeper().registration_timeout_ms
                )
            )) {
            LOG_ERROR(
                "UserService ZooKeeper registration failed"
                << ", error=" << zookeeper_registrar->LastError()
            );
            zookeeper_registrar->Stop();
            zookeeper_client->Stop();
            server.Shutdown();
            server.Wait();
            metadata_executor.Shutdown(
                tinyimx::ShutdownMode::kGraceful,
                std::chrono::milliseconds(5000)
            );
            mysql_pool.Shutdown();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }
    }

    LOG_INFO(
        "UserService ready"
        << ", target=" << server.BoundTarget()
        << ", grpc_sync_num_cqs=" << grpc_options.sync_num_cqs
        << ", grpc_sync_min_pollers=" << grpc_options.sync_min_pollers
        << ", grpc_sync_max_pollers=" << grpc_options.sync_max_pollers
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

            if (sigwait(
                    &signal_set,
                    &signal_number
                ) == 0) {
                LOG_INFO(
                    "UserService shutdown signal received"
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

    metadata_executor.Shutdown(
        tinyimx::ShutdownMode::kGraceful,
        std::chrono::milliseconds(5000)
    );
    const auto metadata_stats = metadata_executor.GetStats();
    LOG_INFO(
        "UserService login metadata executor stopped"
        << ", submitted=" << metadata_stats.submitted_task_count
        << ", completed=" << metadata_stats.completed_task_count
        << ", rejected=" << metadata_stats.rejected_task_count
        << ", peak_queue=" << metadata_stats.peak_queue_size
    );
    mysql_pool.Shutdown();
    LOG_INFO("UserService stopped");
    process_telemetry.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
