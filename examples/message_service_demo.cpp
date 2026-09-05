#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/repository/MessageRepository.h"

#include <pthread.h>
#include <signal.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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
    tinyimx::message::MessageRepositoryAdapter repository_adapter(
        &repository
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

    LOG_INFO(
        "MessageService ready"
        << ", target=" << server.BoundTarget()
    );

    std::thread signal_thread(
        [&server, signal_set]() mutable {
            int signal_number = 0;
            if (sigwait(&signal_set, &signal_number) == 0) {
                LOG_INFO(
                    "MessageService shutdown signal received"
                    << ", signal=" << signal_number
                );
                server.Shutdown();
            }
        }
    );

    server.Wait();

    if (signal_thread.joinable()) {
        signal_thread.join();
    }

    mysql_pool.Shutdown();
    LOG_INFO("MessageService stopped");
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
