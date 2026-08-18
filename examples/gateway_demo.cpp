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
#include "services/registry/GatewayRegistry.h"
#include "services/registry/GatewayRegistryLease.h"
#include "services/registry/GatewayDiscovery.h"
#include "gateway/GatewayRouteResolver.h"
#include "common/net/EventLoopThread.h"
#include "gateway/GatewayPeerTransportManager.h"

#include <csignal>
#include <iostream>
#include <string>
#include <memory>
#include <atomic>
#include <cstdlib>

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
        options.gateway_id = config.App().instance_id;
        options.max_body_size = config.Protocol().max_body_size;
        options.close_on_decode_error = true;
        options.io_thread_count =static_cast<std::size_t>(config.Server().io_thread_count);

        std::unique_ptr<tinyimx::MySqlConnectionPool> mysql_pool;
        std::unique_ptr<tinyimx::MessageRepository> message_repository;
        std::unique_ptr<tinyimx::UserRepository> user_repository;
        std::unique_ptr<tinyimx::FriendRepository> friend_repository;
        std::unique_ptr<tinyimx::FriendRequestRepository> friend_request_repository;

        std::unique_ptr<tinyimx::RedisConnectionPool> redis_pool;
        std::unique_ptr<tinyimx::GatewayRegistry> gateway_registry;
        std::unique_ptr<tinyimx::GatewayRegistryLease> gateway_registry_lease;
        std::unique_ptr<tinyimx::GatewayDiscovery> gateway_discovery;
        std::unique_ptr<tinyimx::OnlineStatusCache> online_status_cache;
        std::unique_ptr<tinyimx::UnreadCountCache> unread_count_cache;
        std::unique_ptr<tinyimx::GatewayRouteResolver> gateway_route_resolver;
        std::unique_ptr<tinyimx::EventLoopThread> gateway_peer_loop_thread;
        std::shared_ptr<tinyimx::GatewayPeerTransportManager> gateway_peer_transport_manager;

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

        const char*
            drop_first_peer_delivered_response =
                std::getenv(
                    "TINYIMX_FAULT_DROP_FIRST_"
                    "PEER_DELIVERED_RESPONSE"
                );


        if (
            drop_first_peer_delivered_response != nullptr &&
            std::string(
                drop_first_peer_delivered_response
            ) == "1"
        ) {
            auto already_dropped =
                std::make_shared<
                    std::atomic<bool>
                >(
                    false
                );


            gateway.
                SetGatewayPeerResponseDropCallbackForTest(
                    [
                        already_dropped
                    ](
                        const tinyimx::
                            GatewayForwardChatResponse&
                                response
                    ) {
                        /*
                        * 只针对真正第一次业务执行成功的
                        * Response。
                        *
                        * Retry经过Dedup后返回：
                        *
                        * Delivered
                        * duplicate=true
                        *
                        * 必须正常返回给Gateway A。
                        */
                        if (
                            response.status !=
                                tinyimx::
                                    GatewayForwardChatStatus::
                                        kDelivered ||
                            response.duplicate
                        ) {
                            return false;
                        }


                        bool expected =
                            false;


                        return already_dropped->
                            compare_exchange_strong(
                                expected,
                                true
                            );
                    }
                );


            LOG_WARN(
                "gateway demo response-loss "
                "fault injection enabled"
            );
        }


        if (!gateway.Start()) {
            LOG_ERROR("gateway demo start failed");

            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        if (config.GatewayRegistry().enable) {
            if (!redis_pool) {
                LOG_ERROR(
                    "gateway registry requires "
                    "initialized redis pool"
                );

                gateway.Stop();

                if (mysql_pool) {
                    mysql_pool->Shutdown();
                }

                tinyimx::Logger::
                    Instance().
                    Shutdown();

                return 1;
            }

            gateway_registry =
                std::make_unique<
                    tinyimx::GatewayRegistry
                >(
                    redis_pool.get()
                );

            tinyimx::GatewayRegistryLeaseOptions
                lease_options;

            lease_options.gateway_id =
                config.App().instance_id;

            lease_options.advertise_host =
                config.GatewayRegistry().
                    advertise_host;

            lease_options.advertise_port =
                config.ServerPort();

            lease_options.lease_ttl_seconds =
                config.GatewayRegistry().
                    lease_ttl_seconds;

            lease_options.
                heartbeat_interval_seconds =
                    config.GatewayRegistry().
                        heartbeat_interval_seconds;

            gateway_registry_lease =
                std::make_unique<
                    tinyimx::GatewayRegistryLease
                >(
                    gateway_registry.get(),
                    std::move(lease_options),
                    [&loop]() {
                        LOG_ERROR(
                            "gateway registry lease lost, "
                            "stopping gateway runtime"
                        );

                        loop.Quit();
                    }
                );

            if (!gateway_registry_lease->Start()) {
                LOG_ERROR(
                    "gateway registry lease "
                    "start failed"
                );

                gateway_registry_lease.reset();
                gateway_registry.reset();

                gateway.Stop();

                if (mysql_pool) {
                    mysql_pool->Shutdown();
                }

                if (redis_pool) {
                    redis_pool->Shutdown();
                }

                tinyimx::Logger::
                    Instance().
                    Shutdown();

                return 1;
            }

            gateway_discovery =
                std::make_unique<
                    tinyimx::GatewayDiscovery
                >(
                    gateway_registry.get(),
                    config.GatewayRegistry().
                        discovery_refresh_interval_seconds
                );

            if (!gateway_discovery->Start()) {
                LOG_ERROR(
                    "gateway discovery start failed"
                );

                gateway_discovery.reset();

                gateway_registry_lease->Stop();
                gateway_registry_lease.reset();

                gateway_registry.reset();

                gateway.Stop();

                if (mysql_pool) {
                    mysql_pool->Shutdown();
                }

                if (redis_pool) {
                    redis_pool->Shutdown();
                }

                tinyimx::Logger::
                    Instance().
                    Shutdown();

                return 1;
            }
        }


        gateway.SetGatewayPeerVerifyCallback(
            [discovery = gateway_discovery.get()](
                const std::string& source_gateway_id,
                const std::string& source_lease_token,
                std::string* error_message) {
                if (error_message != nullptr) {
                    error_message->clear();
                }

                if (
                    discovery == nullptr ||
                    source_gateway_id.empty() ||
                    source_lease_token.empty()
                ) {
                    if (error_message != nullptr) {
                        *error_message =
                            "invalid gateway peer identity";
                    }

                    return false;
                }

                const auto record =
                    discovery->FindById(
                        source_gateway_id
                    );

                if (!record.has_value()) {
                    if (error_message != nullptr) {
                        *error_message =
                            "source gateway is not active";
                    }

                    return false;
                }

                if (
                    record->lease_token !=
                    source_lease_token
                ) {
                    if (error_message != nullptr) {
                        *error_message =
                            "gateway lease token mismatch";
                    }

                    return false;
                }

                return true;
            }
        );

        const auto local_gateway_record =
            gateway_discovery->FindById(
                config.App().instance_id
            );

        if (
            !local_gateway_record.
                has_value() ||
            local_gateway_record->
                lease_token.empty()
        ) {
            LOG_ERROR(
                "gateway local registry "
                "record unavailable"
            );

            return 1;
        }

        gateway_peer_loop_thread =
            std::make_unique<
                tinyimx::EventLoopThread
            >();

        tinyimx::EventLoop*
            gateway_peer_loop =
                gateway_peer_loop_thread->
                    StartLoop();

        if (gateway_peer_loop == nullptr) {
            LOG_ERROR(
                "gateway peer event loop "
                "start failed"
            );

            return 1;
        }

        tinyimx::
            GatewayPeerTransportManagerOptions
                peer_manager_options;

            peer_manager_options.local_gateway_id =
                config.App().instance_id;

            peer_manager_options.local_lease_token =
                local_gateway_record->
                    lease_token;

            peer_manager_options.max_body_size =
                config.Protocol().
                    max_body_size;

            /*
             * ForwardChat在Request Timeout后
             * 最多安全Retry一次。
             *
             * Retry仍保持同一个message_id，
             * B端依靠Memory + MySQL Durable
             * Dedup避免重复用户副作用。
             */
            peer_manager_options.
                max_request_retries = 1;


            peer_manager_options.
                request_retry_initial_delay =
                    std::chrono::milliseconds(
                        100
                    );


            peer_manager_options.
                request_retry_max_delay =
                    std::chrono::milliseconds(
                        1000
                    );

            gateway_peer_transport_manager =
                std::make_shared<
                    tinyimx::
                        GatewayPeerTransportManager
                >(
                    gateway_peer_loop,
                    peer_manager_options
                );


            gateway_peer_transport_manager->
                SetGatewayResolverCallback(
                    [
                        discovery =
                            gateway_discovery.get()
                    ](
                        const std::string&
                            gateway_id
                    )
                        -> std::optional<
                            tinyimx::
                                GatewayInstanceRecord
                        > {
                        if (
                            discovery == nullptr ||
                            gateway_id.empty()
                        ) {
                            return std::nullopt;
                        }

                        return discovery->FindById(
                            gateway_id
                        );
                    }
                );

            if (
                !gateway_peer_transport_manager->
                    Start()
            ) {
                LOG_ERROR(
                    "gateway peer transport manager "
                    "start failed"
                );

                return 1;
            }

            gateway.SetGatewayPeerTransportManager(
                gateway_peer_transport_manager.get()
            );
        if (online_status_cache) {
            gateway_route_resolver =
                std::make_unique<
                    tinyimx::GatewayRouteResolver
                >(
                    config.App().instance_id,
                    online_status_cache.get(),
                    gateway_discovery.get()
                );

            gateway.SetGatewayRouteResolver(
                gateway_route_resolver.get()
            );
        }

        std::cout << "========== TinyIMX Gateway Demo ==========\n";


        std::cout << "Gateway instance ID: " << options.gateway_id<< '\n';
        std::cout << "Listening on " << listen_address.ToString() << '\n';
        std::cout << "IO thread count: " << options.io_thread_count << '\n';
        std::cout << "Reactor mode: " << (options.io_thread_count == 0 ? "single reactor"
                    : "main/sub reactor") << '\n';
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

        LOG_INFO(
            "gateway demo started"
            << ", gateway_id="
            << options.gateway_id
            << ", listen="
            << listen_address.ToString()
        );

        loop.Loop();
        if (gateway_peer_transport_manager) {
            gateway_peer_transport_manager->Stop();
        }

        if (gateway_discovery) {
            gateway_discovery->Stop();
        }

        if (gateway_registry_lease) {
            gateway_registry_lease->Stop();
        }

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