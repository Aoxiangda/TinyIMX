#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "services/repository/FriendRepository.h"
#include "services/social/application/FriendApplicationService.h"
#include "services/social/repository/FriendRepositoryAdapter.h"
#include "services/social/server/SocialServiceServer.h"
#include "services/social/service/SocialServiceImpl.h"

#include <pthread.h>
#include <signal.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::string ResolveListenTarget(
    int argc,
    char* argv[]
) {
    if (argc >= 3) {
        return argv[2];
    }

    const char* env =
        std::getenv("TINYIMX_SOCIAL_LISTEN_TARGET");

    if (env != nullptr && env[0] != '\0') {
        return env;
    }

    return "127.0.0.1:50051";
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
        LOG_ERROR(
            "SocialService requires mysql.enable=true"
        );
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
        LOG_ERROR("SocialService failed to block shutdown signals");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::MySqlConnectionPool mysql_pool;

    if (!mysql_pool.Initialize(config.MySql())) {
        LOG_ERROR("SocialService mysql pool initialize failed");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    tinyimx::FriendRepository repository(&mysql_pool);
    tinyimx::social::FriendRepositoryAdapter repository_adapter(
        &repository
    );
    tinyimx::social::FriendApplicationService application_service(
        &repository_adapter
    );
    tinyimx::social::SocialServiceImpl service_impl(
        &application_service
    );
    tinyimx::social::SocialServiceServer server(&service_impl);

    const std::string listen_target =
        ResolveListenTarget(argc, argv);

    if (!server.Start(listen_target)) {
        LOG_ERROR(
            "SocialService start failed"
            << ", target=" << listen_target
        );
        mysql_pool.Shutdown();
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    LOG_INFO(
        "SocialService ready"
        << ", target=" << server.BoundTarget()
    );

    std::thread signal_thread(
        [&server, signal_set]() mutable {
            int signal_number = 0;

            if (sigwait(
                    &signal_set,
                    &signal_number
                ) == 0) {
                LOG_INFO(
                    "SocialService shutdown signal received"
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
    LOG_INFO("SocialService stopped");
    tinyimx::Logger::Instance().Shutdown();
    return 0;
}
