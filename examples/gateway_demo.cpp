#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"
#include "common/net/EventLoop.h"
#include "common/net/InetAddress.h"
#include "gateway/GatewayServer.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/MessageRepository.h"
#include "services/repository/UserRepository.h"
#include "common/cache/RedisConnectionPool.h"
#include "services/cache/OnlineStatusCache.h"
#include "services/cache/UnreadCountCache.h"
#include "services/repository/FriendRepository.h"
#include "services/repository/FriendRequestRepository.h"

#include <csignal>
#include <iostream>
#include <string>
#include <memory>

namespace {

tinyimx::EventLoop* g_loop = nullptr;

void HandleSignal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        if (g_loop != nullptr) {
            g_loop->Quit();
        }
    }
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

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    {
        tinyimx::InetAddress listen_address(
            config.ServerHost(),
            config.ServerPort()
        );

        if (!listen_address.IsValid()) {
            LOG_ERROR("invalid gateway listen address"
                      << ", host=" << config.ServerHost()
                      << ", port=" << config.ServerPort());

            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::EventLoop loop;
        g_loop = &loop;

        if (!loop.IsValid()) {
            LOG_ERROR("gateway event loop is invalid");

            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        tinyimx::GatewayServerOptions options;
        options.name = "tinyimx-gateway";
        options.max_body_size = config.Protocol().max_body_size;
        options.close_on_decode_error = true;

        std::unique_ptr<tinyimx::MySqlConnectionPool> mysql_pool;
        std::unique_ptr<tinyimx::MessageRepository> message_repository;
        std::unique_ptr<tinyimx::UserRepository> user_repository;
        std::unique_ptr<tinyimx::FriendRepository> friend_repository;
        std::unique_ptr<tinyimx::FriendRequestRepository>
            friend_request_repository;

        std::unique_ptr<tinyimx::RedisConnectionPool> redis_pool;
        std::unique_ptr<tinyimx::OnlineStatusCache> online_status_cache;
        std::unique_ptr<tinyimx::UnreadCountCache> unread_count_cache;


        if (config.MySql().enable) {
            mysql_pool = std::make_unique<tinyimx::MySqlConnectionPool>();

            if (!mysql_pool->Initialize(config.MySql())) {
                LOG_ERROR("gateway mysql pool initialize failed");
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }

            user_repository =
                std::make_unique<tinyimx::UserRepository>(
                    mysql_pool.get()
                );

            message_repository =
                std::make_unique<tinyimx::MessageRepository>(
                    mysql_pool.get()
                );

            friend_repository =
                std::make_unique<tinyimx::FriendRepository>(
                    mysql_pool.get()
                );

            friend_request_repository =
                std::make_unique<
                    tinyimx::FriendRequestRepository
                >(
                    mysql_pool.get()
                );

            LOG_INFO("gateway mysql persistence enabled");
        }

        if (config.Redis().enable) {
            redis_pool = std::make_unique<tinyimx::RedisConnectionPool>();

            if (!redis_pool->Initialize(config.Redis())) {
                LOG_ERROR("gateway redis pool initialize failed");
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }

            online_status_cache =
                std::make_unique<tinyimx::OnlineStatusCache>(
                    redis_pool.get()
                );

            unread_count_cache =
                std::make_unique<tinyimx::UnreadCountCache>(
                    redis_pool.get()
                );

            LOG_INFO("gateway redis cache enabled");
            LOG_INFO("gateway redis online status cache enabled");
        }

        tinyimx::GatewayServer gateway(
            &loop,
            listen_address,
            options
        );

        if (user_repository) {
            gateway.SetUserRepository(user_repository.get());
        }

        if (message_repository) {
            gateway.SetMessageRepository(message_repository.get());
        }

        if (friend_repository) {
            gateway.SetFriendRepository(friend_repository.get());
        }

        if (friend_request_repository) {
            gateway.SetFriendRequestRepository(
                friend_request_repository.get()
            );
        }

        if (online_status_cache) {
            gateway.SetOnlineStatusCache(online_status_cache.get());
        }

        if (unread_count_cache) {
            gateway.SetUnreadCountCache(unread_count_cache.get());
        }


        if (!gateway.Start()) {
            LOG_ERROR("gateway demo start failed");

            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }


        std::cout << "========== TinyIMX Gateway Demo ==========\n";
        std::cout << "Listening on " << listen_address.ToString() << '\n';
        /*
            std::cout << "Test client:\n";
            std::cout << "  ./build/linux-debug/protocol_echo_client_demo "
                    << "127.0.0.1 " << config.ServerPort() << '\n';
            std::cout << "Current behavior:\n";
            std::cout << "  login_request -> login_response\n";
            std::cout << "  chat_message  -> chat_ack\n";
            std::cout << "  heartbeat     -> heartbeat pong\n";

        */

        std::cout << "Test clients:\n";
        std::cout << "  ./build/linux-debug/gateway_session_client_demo "
                << "127.0.0.1 " << config.ServerPort() << '\n';
        std::cout << "  ./build/linux-debug/gateway_offline_client_demo "
                << "127.0.0.1 " << config.ServerPort() << '\n';

        std::cout << "Current behavior:\n";
        std::cout << "  login_request -> bind user session\n";
        std::cout << "  online chat_message -> forward to target user\n";
        std::cout << "  offline chat_message -> persist in MySQL\n";
        std::cout << "  target login -> load and push pending messages\n";
        std::cout << "  heartbeat -> heartbeat pong\n";
        std::cout << "Press Ctrl-C to stop gateway.\n";

        LOG_INFO("gateway demo started"
                 << ", listen=" << listen_address.ToString());

        loop.Loop();

        gateway.Stop();

        if (mysql_pool) {
            mysql_pool->Shutdown();
        }

        if (redis_pool) {
            redis_pool->Shutdown();
        }

        std::cout << "TinyIMX gateway demo stopped\n";
        std::cout << "==========================================\n";

        g_loop = nullptr;
    }

    tinyimx::Logger::Instance().Shutdown();

    return 0;
}