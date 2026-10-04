#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/logging/LogMacros.h"
#include "common/observability/ProcessTelemetry.h"
#include "common/net/EventLoop.h"
#include "gateway/business/BusinessExecutor.h"
#include "common/net/InetAddress.h"
#include "gateway/GatewayServer.h"
#include "common/cache/RedisConnectionPool.h"
#include "services/cache/OnlineStatusCache.h"
#include "services/cache/UnreadCountCache.h"
#include "services/registry/GatewayRegistry.h"
#include "services/registry/GatewayRegistryLease.h"
#include "services/registry/GatewayDiscovery.h"
#include "gateway/GatewayRouteResolver.h"
#include "common/net/EventLoopThread.h"
#include "gateway/GatewayPeerTransportManager.h"
#include "gateway/GroupFanoutCoordinator.h"
#include "services/rpc/SocialRpcClient.h"
#include "services/rpc/UserRpcClient.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/GroupRpcClient.h"
#include "services/rpc/FileRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/rpc/ZooKeeperServiceEndpointProvider.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"

#include <csignal>
#include <iostream>
#include <string>
#include <memory>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

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

    tinyimx::ProcessTelemetry process_telemetry;
    if (!process_telemetry.Initialize(
            config,
            "tinyimx-gateway",
            "gateway"
        )) {
        std::cerr << "gateway observability initialization failed\n";
        tinyimx::Logger::Instance().Shutdown();
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
        options.io_thread_count = static_cast<std::size_t>(config.Server().io_thread_count);
        options.listen_backlog = config.Server().backlog;
        if (const auto* value = std::getenv("TINYIMX_DURABLE_PRIVATE_RECOVERY_ENABLE")) {
            const std::string flag(value);
            if (flag != "0" && flag != "1") {
                LOG_ERROR("TINYIMX_DURABLE_PRIVATE_RECOVERY_ENABLE must be 0 or 1");
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }
            options.enable_durable_private_recovery = flag == "1";
        }

        /*
        * ============================================================
        * M13 slow-business fault injection
        * ============================================================
        *
        * 示例：
        *
        * TINYIMX_FAULT_HISTORY_BUSINESS_DELAY_MS=500
        * ./build/linux-debug/gateway_demo ...
        */
        const char*
            history_business_delay_env =
                std::getenv(
                    "TINYIMX_FAULT_HISTORY_"
                    "BUSINESS_DELAY_MS"
                );


        if (
            history_business_delay_env !=
            nullptr
        ) {
            const std::string delay_text{
                history_business_delay_env
            };

            try {
                std::size_t parsed_length = 0;

                const long long delay_ms =
                    std::stoll(
                        delay_text,
                        &parsed_length
                    );


                /*
                * 必须完整解析。
                *
                * "500abc"
                *
                * 不能偷偷当500接受。
                */
                if (
                    parsed_length !=
                        delay_text.size() ||
                    delay_ms < 0 ||
                    delay_ms > 60000
                ) {
                    LOG_ERROR(
                        "invalid history business "
                        "delay fault injection"
                        << ", value="
                        << delay_text
                        << ", allowed_range_ms="
                        << "0..60000"
                    );

                    tinyimx::Logger::
                        Instance().
                        Shutdown();

                    return 1;
                }


                options.
                    history_business_delay_for_test =
                        std::chrono::milliseconds(
                            delay_ms
                        );


                if (delay_ms > 0) {
                    LOG_WARN(
                        "M13 history business delay "
                        "fault injection enabled"
                        << ", delay_ms="
                        << delay_ms
                    );
                }
            }
            catch (...) {
                LOG_ERROR(
                    "invalid history business "
                    "delay fault injection"
                    << ", value="
                    << delay_text
                );

                tinyimx::Logger::
                    Instance().
                    Shutdown();

                return 1;
            }
        }



        std::unique_ptr<tinyimx::RedisConnectionPool> redis_pool;
        std::unique_ptr<tinyimx::GatewayRegistry> gateway_registry;
        std::unique_ptr<tinyimx::GatewayRegistryLease> gateway_registry_lease;
        std::unique_ptr<tinyimx::GatewayDiscovery> gateway_discovery;
        std::unique_ptr<tinyimx::OnlineStatusCache> online_status_cache;
        std::unique_ptr<tinyimx::UnreadCountCache> unread_count_cache;
        std::unique_ptr<tinyimx::GatewayRouteResolver> gateway_route_resolver;
        std::unique_ptr<tinyimx::EventLoopThread> gateway_peer_loop_thread;
        std::shared_ptr<tinyimx::GatewayPeerTransportManager> gateway_peer_transport_manager;
        std::unique_ptr<tinyimx::GroupFanoutCoordinator> group_fanout_coordinator;
        std::unique_ptr<tinyimx::BusinessExecutor> business_executor;
        std::unique_ptr<tinyimx::BusinessExecutor> message_executor;
        std::unique_ptr<tinyimx::BusinessExecutor> presence_executor;
        std::unique_ptr<tinyimx::BusinessExecutor> replay_executor;
        std::shared_ptr<const tinyimx::rpc::ServiceEndpointProvider>
            service_endpoint_provider;
        std::shared_ptr<
            tinyimx::registry::zookeeper::ZooKeeperClient
        > rpc_zookeeper_client;
        std::shared_ptr<
            tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery
        > rpc_service_discovery;
        std::unique_ptr<tinyimx::rpc::SocialRpcClient>
            social_rpc_client;
        std::unique_ptr<tinyimx::rpc::UserRpcClient>
            user_rpc_client;
        std::unique_ptr<tinyimx::rpc::MessageRpcClient>
            message_rpc_client;
        std::unique_ptr<tinyimx::rpc::GroupRpcClient>
            group_rpc_client;
        std::unique_ptr<tinyimx::rpc::FileRpcClient>
            file_rpc_client;
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


        /*
        * ============================================================
        * M13 Business Execution Runtime
        * ============================================================
        *
        * M13-C1正式解除Business Runtime与通用ThreadPool配置耦合。
        *
        * 旧配置没有business_runtime段时，Config层仍会兼容继承：
        * thread_pool.worker_threads -> worker_threads
        * thread_pool.queue_capacity -> max_pending_tasks
        *
        * 新部署可独立调优Runtime容量、stripe、deadline和drain预算。
        */
        const auto& business_config =
            config.BusinessRuntime();


        tinyimx::BusinessExecutorOptions
            business_options;

        business_options.name = "gateway-foreground-runtime";


        business_options.worker_threads =
            business_config.worker_threads;


        business_options.max_pending_tasks =
            business_config.max_pending_tasks;


        business_options.stripe_count =
            business_config.stripe_count;


        business_options.per_stripe_queue_capacity =
            business_config.per_stripe_queue_capacity;


        business_options.default_deadline =
            std::chrono::milliseconds(
                business_config.default_deadline_ms
            );


        business_options.shutdown_timeout =
            std::chrono::milliseconds(
                business_config.shutdown_timeout_ms
            );


        business_executor =
            std::make_unique<
                tinyimx::BusinessExecutor
            >(
                business_options
            );


        if (!business_executor->Start()) {
            LOG_ERROR(
                "gateway business runtime start failed"
            );


            business_executor.reset();
if (redis_pool) {
                redis_pool->Shutdown();
            }


            tinyimx::Logger::
                Instance().
                Shutdown();


            return 1;
        }


        LOG_INFO(
            "gateway business runtime started"
            << ", workers="
            << business_options.worker_threads
            << ", max_pending="
            << business_options.max_pending_tasks
            << ", stripes="
            << business_options.stripe_count
            << ", per_stripe_capacity="
            << business_options.
                per_stripe_queue_capacity
            << ", default_deadline_ms="
            << business_options.
                default_deadline.count()
            << ", shutdown_timeout_ms="
            << business_options.
                shutdown_timeout.count()
        );

        const auto& discovery_config = config.ServiceDiscovery();

        if (discovery_config.provider == "zookeeper") {
            rpc_zookeeper_client = std::make_shared<
                tinyimx::registry::zookeeper::ZooKeeperClient
            >();

            if (!rpc_zookeeper_client->Start(config.ZooKeeper())) {
                LOG_ERROR(
                    "gateway ZooKeeper discovery client start failed"
                    << ", error=" << rpc_zookeeper_client->LastError()
                );
                business_executor->ShutdownGraceful();
if (redis_pool) {
                    redis_pool->Shutdown();
                }
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }

            rpc_service_discovery = std::make_shared<
                tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery
            >(
                rpc_zookeeper_client,
                config.ZooKeeper().service_root,
                discovery_config
            );

            if (!rpc_service_discovery->Start(
                    std::chrono::milliseconds(
                        discovery_config.initial_sync_timeout_ms
                    )
                )) {
                LOG_ERROR(
                    "gateway ZooKeeper service discovery initial sync failed"
                    << ", error=" << rpc_service_discovery->LastError()
                );
                rpc_service_discovery->Stop();
                rpc_zookeeper_client->Stop();
                business_executor->ShutdownGraceful();
if (redis_pool) {
                    redis_pool->Shutdown();
                }
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }

            service_endpoint_provider = std::make_shared<
                tinyimx::rpc::ZooKeeperServiceEndpointProvider
            >(
                rpc_service_discovery,
                discovery_config
            );

            // Dynamic clients stay attached even when a service currently has
            // zero instances. Future membership watches can make them usable
            // without restarting the Gateway.
            social_rpc_client =
                std::make_unique<tinyimx::rpc::SocialRpcClient>(
                    service_endpoint_provider
                );
            user_rpc_client =
                std::make_unique<tinyimx::rpc::UserRpcClient>(
                    service_endpoint_provider
                );
            message_rpc_client =
                std::make_unique<tinyimx::rpc::MessageRpcClient>(
                    service_endpoint_provider
                );
            group_rpc_client =
                std::make_unique<tinyimx::rpc::GroupRpcClient>(
                    service_endpoint_provider
                );
            file_rpc_client =
                std::make_unique<tinyimx::rpc::FileRpcClient>(
                    service_endpoint_provider
                );

            LOG_INFO(
                "gateway dynamic RPC discovery enabled"
                << ", provider=zookeeper"
                << ", root=" << config.ZooKeeper().service_root
                << ", stale_after_ms="
                << discovery_config.snapshot_stale_after_ms
                << ", retain_lkg="
                << discovery_config.retain_last_known_good
            );
        } else {
            const char* social_rpc_target_env =
                std::getenv("TINYIMX_SOCIAL_RPC_TARGET");
            const char* user_rpc_target_env =
                std::getenv("TINYIMX_USER_RPC_TARGET");
            const char* message_rpc_target_env =
                std::getenv("TINYIMX_MESSAGE_RPC_TARGET");
            const char* group_rpc_target_env =
                std::getenv("TINYIMX_GROUP_RPC_TARGET");
            const char* file_rpc_target_env =
                std::getenv("TINYIMX_FILE_RPC_TARGET");

            const std::string social_rpc_target =
                social_rpc_target_env != nullptr
                    ? std::string(social_rpc_target_env)
                    : std::string{};
            const std::string user_rpc_target =
                user_rpc_target_env != nullptr
                    ? std::string(user_rpc_target_env)
                    : std::string{};
            const std::string message_rpc_target =
                message_rpc_target_env != nullptr
                    ? std::string(message_rpc_target_env)
                    : std::string{};
            const std::string group_rpc_target =
                group_rpc_target_env != nullptr
                    ? std::string(group_rpc_target_env)
                    : std::string{};
            const std::string file_rpc_target =
                file_rpc_target_env != nullptr
                    ? std::string(file_rpc_target_env)
                    : std::string{};

            if (!social_rpc_target.empty() ||
                !user_rpc_target.empty() ||
                !message_rpc_target.empty() ||
                !group_rpc_target.empty() ||
                !file_rpc_target.empty()) {
                service_endpoint_provider =
                    std::make_shared<
                        tinyimx::rpc::StaticServiceEndpointProvider
                    >(
                        social_rpc_target,
                        user_rpc_target,
                        message_rpc_target,
                        group_rpc_target,
                        file_rpc_target
                    );
            }

            if (!social_rpc_target.empty()) {
                social_rpc_client =
                    std::make_unique<tinyimx::rpc::SocialRpcClient>(
                        service_endpoint_provider
                    );
                LOG_INFO(
                    "gateway SocialService RPC enabled"
                    << ", provider=static"
                    << ", target=" << social_rpc_target
                );
            } else {
                LOG_WARN(
                    "gateway SocialService RPC disabled: "
                    "TINYIMX_SOCIAL_RPC_TARGET is not set"
                );
            }

            if (!user_rpc_target.empty()) {
                user_rpc_client =
                    std::make_unique<tinyimx::rpc::UserRpcClient>(
                        service_endpoint_provider
                    );
                LOG_INFO(
                    "gateway UserService RPC enabled"
                    << ", provider=static"
                    << ", target=" << user_rpc_target
                );
            } else {
                LOG_WARN(
                    "gateway UserService RPC disabled: "
                    "TINYIMX_USER_RPC_TARGET is not set; "
                    "Login will fail closed with auth_unavailable"
                );
            }

            if (!message_rpc_target.empty()) {
                message_rpc_client =
                    std::make_unique<tinyimx::rpc::MessageRpcClient>(
                        service_endpoint_provider
                    );
                LOG_INFO(
                    "gateway MessageService RPC enabled"
                    << ", provider=static"
                    << ", target=" << message_rpc_target
                );
            } else {
                LOG_WARN(
                    "gateway MessageService RPC disabled: "
                    "TINYIMX_MESSAGE_RPC_TARGET is not set; "
                    "Chat persistence/History/ConversationList will fail closed"
                );
            }

            if (!group_rpc_target.empty()) {
                group_rpc_client =
                    std::make_unique<tinyimx::rpc::GroupRpcClient>(
                        service_endpoint_provider
                    );
                LOG_INFO(
                    "gateway GroupService RPC enabled"
                    << ", provider=static"
                    << ", target=" << group_rpc_target
                );
            } else {
                LOG_WARN(
                    "gateway GroupService RPC disabled: "
                    "TINYIMX_GROUP_RPC_TARGET is not set; "
                    "Group control requests will fail closed"
                );
            }

            if (!file_rpc_target.empty()) {
                file_rpc_client =
                    std::make_unique<tinyimx::rpc::FileRpcClient>(
                        service_endpoint_provider
                    );
                LOG_INFO(
                    "gateway FileService RPC enabled"
                    << ", provider=static"
                    << ", target=" << file_rpc_target
                );
            } else {
                LOG_WARN(
                    "gateway FileService RPC disabled: "
                    "TINYIMX_FILE_RPC_TARGET is not set; "
                    "File control requests will fail closed"
                );
            }
        }

        tinyimx::GatewayServer gateway(
            &loop,
            listen_address,
            options
        );

        gateway.SetBusinessExecutor(
            business_executor.get()
        );

        if (social_rpc_client) {
            gateway.SetSocialRpcClient(
                social_rpc_client.get()
            );
        }

        if (user_rpc_client) {
            gateway.SetUserRpcClient(user_rpc_client.get());
        }

        if (message_rpc_client) {
            gateway.SetMessageRpcClient(message_rpc_client.get());
        }

        if (group_rpc_client) {
            gateway.SetGroupRpcClient(group_rpc_client.get());
        }
        if (file_rpc_client) {
            gateway.SetFileRpcClient(file_rpc_client.get());
        }
if (online_status_cache) {
            gateway.SetOnlineStatusCache(online_status_cache.get());
        }

        if (unread_count_cache) {
            gateway.SetUnreadCountCache(unread_count_cache.get());
            gateway.SetUnreadProjectionWriteEnabled(
                config.UnreadProjection().owner == "gateway"
            );
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
if (redis_pool) {
                    redis_pool->Shutdown();
                }

                tinyimx::Logger::
                    Instance().
                    Shutdown();

                return 1;
            }
        }


        /*
         * ============================================================
         * Optional Multi-Gateway Runtime
         * ============================================================
         *
         * gateway_registry.enable=false is a valid standalone mode.
         *
         * Registry / Discovery / PeerTransport / RouteResolver form one
         * capability group.  They must either be initialized from a live
         * GatewayDiscovery instance or remain completely disabled.
         *
         * Never dereference gateway_discovery when registry is disabled.
         */
        if (gateway_discovery) {
            gateway.SetGatewayPeerVerifyCallback(
                [discovery = gateway_discovery.get()](
                    const std::string& source_gateway_id,
                    const std::string& source_lease_token,
                    std::string* error_message) {
                    if (error_message != nullptr) {
                        error_message->clear();
                    }

                    if (
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
                !local_gateway_record.has_value() ||
                local_gateway_record->lease_token.empty()
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
                        if (gateway_id.empty()) {
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
        } else {
            /*
             * Standalone / fault-test mode:
             *
             * Redis presence can remain enabled, but cross-Gateway routing
             * and peer transport are deliberately unavailable because no
             * registry snapshot exists.
             */
            LOG_INFO(
                "gateway registry disabled"
                << ", peer_verifier=0"
                << ", peer_transport=0"
                << ", route_resolver=0"
            );
        }


        // P0 Message Plane bulkhead. Private chat send / receiver ACK / peer
        // forward execute synchronous RPCs and must not occupy Login/control
        // workers. Keep the queue bounded: capacity is not a latency fix.
        tinyimx::BusinessExecutorOptions message_options;
        message_options.name = "gateway-message-runtime";
        message_options.worker_threads =
            std::max<std::size_t>(
                4,
                std::min<std::size_t>(
                    8,
                    business_options.worker_threads * 2
                )
            );
        message_options.max_pending_tasks = business_options.max_pending_tasks;
        message_options.stripe_count = business_options.stripe_count;
        message_options.per_stripe_queue_capacity =
            business_options.per_stripe_queue_capacity;
        message_options.default_deadline = business_options.default_deadline;
        message_options.shutdown_timeout = business_options.shutdown_timeout;

        message_executor = std::make_unique<tinyimx::BusinessExecutor>(
            message_options
        );
        if (!message_executor->Start()) {
            LOG_ERROR("gateway message runtime start failed");
            gateway.Stop();
            business_executor->ShutdownGraceful();
            if (redis_pool) {
                redis_pool->Shutdown();
            }
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        LOG_INFO(
            "gateway message runtime started"
            << ", workers=" << message_options.worker_threads
            << ", max_pending=" << message_options.max_pending_tasks
            << ", stripes=" << message_options.stripe_count
            << ", per_stripe_capacity="
            << message_options.per_stripe_queue_capacity
            << ", default_deadline_ms="
            << message_options.default_deadline.count()
        );

        gateway.SetMessageExecutor(message_executor.get());

        // Presence/online-status maintenance has a dedicated bulkhead. During a
        // Gateway failover thousands of clients can reconnect and authenticate at
        // once; heartbeat refresh and disconnect cleanup must not contend with
        // Login/control tasks in the foreground runtime. The larger bounded queue
        // absorbs one 10k-class maintenance burst while 256 stripes keep per-user
        // ordering without manufacturing hot stripes.
        tinyimx::BusinessExecutorOptions presence_options;
        presence_options.name = "gateway-presence-runtime";
        presence_options.worker_threads =
            std::max<std::size_t>(
                2,
                std::min<std::size_t>(4, business_options.worker_threads)
            );
        presence_options.max_pending_tasks =
            std::max<std::size_t>(
                4096,
                std::min<std::size_t>(
                    16384,
                    business_options.max_pending_tasks * 32
                )
            );
        presence_options.stripe_count =
            std::max<std::size_t>(
                128,
                std::min<std::size_t>(
                    256,
                    business_options.stripe_count * 4
                )
            );
        presence_options.per_stripe_queue_capacity =
            std::max<std::size_t>(
                64,
                business_options.per_stripe_queue_capacity
            );
        presence_options.default_deadline = std::chrono::milliseconds(10000);
        presence_options.shutdown_timeout = business_options.shutdown_timeout;

        presence_executor = std::make_unique<tinyimx::BusinessExecutor>(
            presence_options
        );
        if (!presence_executor->Start()) {
            LOG_ERROR("gateway presence runtime start failed");
            gateway.Stop();
            if (message_executor) message_executor->ShutdownGraceful();
            business_executor->ShutdownGraceful();
            if (redis_pool) {
                redis_pool->Shutdown();
            }
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        LOG_INFO(
            "gateway presence runtime started"
            << ", workers=" << presence_options.worker_threads
            << ", max_pending=" << presence_options.max_pending_tasks
            << ", stripes=" << presence_options.stripe_count
            << ", per_stripe_capacity="
            << presence_options.per_stripe_queue_capacity
            << ", default_deadline_ms="
            << presence_options.default_deadline.count()
        );

        gateway.SetPresenceExecutor(presence_executor.get());

        // Isolate durable replay from latency-sensitive foreground/message work. Replay
        // keeps per-user ordering but has its own bounded queue and workers,
        // preventing a reconnect storm from amplifying foreground queue delay.
        tinyimx::BusinessExecutorOptions replay_options;
        replay_options.name = "gateway-replay-runtime";
        replay_options.worker_threads =
            std::max<std::size_t>(1, std::min<std::size_t>(2, business_options.worker_threads));
        replay_options.max_pending_tasks =
            std::min<std::size_t>(256, business_options.max_pending_tasks);
        replay_options.stripe_count = business_options.stripe_count;
        replay_options.per_stripe_queue_capacity =
            std::max<std::size_t>(
                1,
                std::min<std::size_t>(
                    16,
                    std::min(
                        business_options.per_stripe_queue_capacity,
                        replay_options.max_pending_tasks
                    )
                )
            );
        replay_options.default_deadline = std::chrono::milliseconds(5000);
        replay_options.shutdown_timeout = business_options.shutdown_timeout;

        replay_executor = std::make_unique<tinyimx::BusinessExecutor>(
            replay_options
        );
        if (!replay_executor->Start()) {
            LOG_ERROR("gateway replay runtime start failed");
            gateway.Stop();
            if (presence_executor) presence_executor->ShutdownGraceful();
            if (message_executor) message_executor->ShutdownGraceful();
            business_executor->ShutdownGraceful();
            if (redis_pool) {
                redis_pool->Shutdown();
            }
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        LOG_INFO(
            "gateway replay runtime started"
            << ", workers=" << replay_options.worker_threads
            << ", max_pending=" << replay_options.max_pending_tasks
            << ", stripes=" << replay_options.stripe_count
            << ", per_stripe_capacity=" << replay_options.per_stripe_queue_capacity
            << ", default_deadline_ms=" << replay_options.default_deadline.count()
        );

        gateway.SetReplayExecutor(
            replay_executor.get()
        );

        const char* group_fanout_enable_env = std::getenv("TINYIMX_GROUP_FANOUT_ENABLE");
        const bool group_fanout_enabled =
            group_fanout_enable_env != nullptr && std::string(group_fanout_enable_env) == "1";
        if (group_fanout_enabled) {
            if (!message_rpc_client) {
                LOG_ERROR("group fanout requires MessageService RPC client");
                gateway.Stop();
                if (presence_executor) presence_executor->ShutdownGraceful();
                if (message_executor) message_executor->ShutdownGraceful();
                if (business_executor) business_executor->ShutdownGraceful();
                if (replay_executor) replay_executor->ShutdownGraceful();
if (redis_pool) redis_pool->Shutdown();
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }
            tinyimx::GroupFanoutCoordinatorOptions fanout_options;
            fanout_options.gateway_id = options.gateway_id;
            auto parse_u32_env = [](const char* name, std::uint32_t fallback) {
                const char* value = std::getenv(name);
                if (value == nullptr || *value == '\0') return fallback;
                try {
                    const auto parsed = std::stoul(value);
                    return parsed > 0 && parsed <= 600000
                        ? static_cast<std::uint32_t>(parsed) : fallback;
                } catch (...) { return fallback; }
            };
            fanout_options.batch_size = std::min<std::size_t>(
                parse_u32_env("TINYIMX_GROUP_FANOUT_BATCH_SIZE", 64), 256);
            fanout_options.lease_ms = parse_u32_env("TINYIMX_GROUP_FANOUT_LEASE_MS", 5000);
            fanout_options.submitted_retry_ms = parse_u32_env(
                "TINYIMX_GROUP_FANOUT_ACK_RETRY_MS", 3000);
            fanout_options.failure_retry_ms = parse_u32_env(
                "TINYIMX_GROUP_FANOUT_FAILURE_RETRY_MS", 1000);
            fanout_options.recovery_interval = std::chrono::milliseconds(
                parse_u32_env("TINYIMX_GROUP_FANOUT_RECOVERY_MS", 1000));
            const char* fault_pause_after_claim_env =
                std::getenv("TINYIMX_FAULT_GROUP_FANOUT_PAUSE_AFTER_CLAIM_MS");
            if (fault_pause_after_claim_env != nullptr &&
                *fault_pause_after_claim_env != '\0') {
                try {
                    const auto parsed = std::stoul(fault_pause_after_claim_env);
                    if (parsed > 0 && parsed <= 60000) {
                        fanout_options.fault_pause_after_claim =
                            std::chrono::milliseconds(parsed);
                        LOG_WARN("group fanout post-claim crash-window fault injection enabled"
                                 << ", pause_ms=" << parsed);
                    }
                } catch (...) {
                    LOG_WARN("invalid TINYIMX_FAULT_GROUP_FANOUT_PAUSE_AFTER_CLAIM_MS ignored");
                }
            }
            group_fanout_coordinator = std::make_unique<tinyimx::GroupFanoutCoordinator>(
                message_rpc_client.get(), &gateway, fanout_options);
            if (!group_fanout_coordinator->Start()) {
                LOG_ERROR("group fanout coordinator start failed");
                gateway.Stop();
                if (presence_executor) presence_executor->ShutdownGraceful();
                if (message_executor) message_executor->ShutdownGraceful();
                if (business_executor) business_executor->ShutdownGraceful();
                if (replay_executor) replay_executor->ShutdownGraceful();
if (redis_pool) redis_pool->Shutdown();
                tinyimx::Logger::Instance().Shutdown();
                return 1;
            }
        } else {
            LOG_INFO("group fanout coordinator disabled; set TINYIMX_GROUP_FANOUT_ENABLE=1 to enable M17-B2 delivery");
        }

        std::cout << "========== TinyIMX Gateway Demo ==========\n";


        std::cout << "Gateway instance ID: " << options.gateway_id<< '\n';
        std::cout << "Listening on " << listen_address.ToString() << '\n';
        std::cout << "IO thread count: " << options.io_thread_count << '\n';
        std::cout << "Listen backlog: " << options.listen_backlog << '\n';
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
        gateway.StopPrivateReplayAdmission();
        if (group_fanout_coordinator) {
            group_fanout_coordinator->Stop();
        }
        if (gateway_peer_transport_manager) {
            gateway_peer_transport_manager->Stop();
        }

        if (gateway_discovery) {
            gateway_discovery->Stop();
        }

        if (gateway_registry_lease) {
            gateway_registry_lease->Stop();
        }


        /*
        * ============================================================
        * M13 Business Runtime graceful drain
        * ============================================================
        *
        * 此时base EventLoop已经退出，但Sub-Reactor、Repository、
        * MySQL / Redis依旧存活。
        *
        * 必须先停止Business Runtime接收新任务并完成：
        *
        * queued work
        * running work
        * pending completions
        *
        * 然后才能Stop Gateway并销毁业务依赖。
        */
        if (message_executor) {
            const auto before_stats = message_executor->GetStats();
            LOG_INFO(
                "gateway message runtime draining"
                << ", pending=" << before_stats.current_pending_tasks
                << ", active=" << before_stats.current_active_tasks
                << ", accepted=" << before_stats.accepted_total
            );

            const bool within_budget = message_executor->ShutdownGraceful();
            const auto after_stats = message_executor->GetStats();
            const std::uint64_t lifecycle_accounted =
                after_stats.completed_total +
                after_stats.worker_exception_total +
                after_stats.deadline_expired_before_start_total +
                after_stats.cancelled_before_start_total +
                after_stats.completion_dropped_total +
                after_stats.completion_exception_total;
            const std::uint64_t accepted_unaccounted =
                after_stats.accepted_total >= lifecycle_accounted
                    ? after_stats.accepted_total - lifecycle_accounted
                    : 0;

            LOG_INFO(
                "gateway message runtime drained"
                << ", within_budget=" << within_budget
                << ", submitted=" << after_stats.submitted_total
                << ", accepted=" << after_stats.accepted_total
                << ", completed=" << after_stats.completed_total
                << ", rejected_overload="
                << after_stats.rejected_overload_total
                << ", rejected_hot_key="
                << after_stats.rejected_hot_key_total
                << ", rejected_deadline="
                << after_stats.rejected_deadline_total
                << ", deadline_before_start="
                << after_stats.deadline_expired_before_start_total
                << ", cancelled_before_start="
                << after_stats.cancelled_before_start_total
                << ", peak_pending=" << after_stats.peak_pending_tasks
                << ", peak_active=" << after_stats.peak_active_tasks
                << ", avg_queue_wait_ms="
                << after_stats.average_queue_wait_ms
                << ", max_queue_wait_ms="
                << after_stats.max_queue_wait_ms
                << ", avg_execution_ms="
                << after_stats.average_execution_ms
                << ", max_execution_ms="
                << after_stats.max_execution_ms
                << ", accepted_unaccounted=" << accepted_unaccounted
            );
        }


        if (presence_executor) {
            const auto before_stats = presence_executor->GetStats();
            LOG_INFO(
                "gateway presence runtime draining"
                << ", pending=" << before_stats.current_pending_tasks
                << ", active=" << before_stats.current_active_tasks
                << ", accepted=" << before_stats.accepted_total
            );

            const bool within_budget = presence_executor->ShutdownGraceful();
            const auto after_stats = presence_executor->GetStats();
            const std::uint64_t lifecycle_accounted =
                after_stats.completed_total +
                after_stats.worker_exception_total +
                after_stats.deadline_expired_before_start_total +
                after_stats.cancelled_before_start_total +
                after_stats.completion_dropped_total +
                after_stats.completion_exception_total;
            const std::uint64_t accepted_unaccounted =
                after_stats.accepted_total >= lifecycle_accounted
                    ? after_stats.accepted_total - lifecycle_accounted
                    : 0;

            LOG_INFO(
                "gateway presence runtime drained"
                << ", within_budget=" << within_budget
                << ", submitted=" << after_stats.submitted_total
                << ", accepted=" << after_stats.accepted_total
                << ", completed=" << after_stats.completed_total
                << ", rejected_overload="
                << after_stats.rejected_overload_total
                << ", rejected_hot_key="
                << after_stats.rejected_hot_key_total
                << ", rejected_deadline="
                << after_stats.rejected_deadline_total
                << ", deadline_before_start="
                << after_stats.deadline_expired_before_start_total
                << ", cancelled_before_start="
                << after_stats.cancelled_before_start_total
                << ", peak_pending=" << after_stats.peak_pending_tasks
                << ", peak_active=" << after_stats.peak_active_tasks
                << ", avg_queue_wait_ms="
                << after_stats.average_queue_wait_ms
                << ", max_queue_wait_ms="
                << after_stats.max_queue_wait_ms
                << ", avg_execution_ms="
                << after_stats.average_execution_ms
                << ", max_execution_ms="
                << after_stats.max_execution_ms
                << ", accepted_unaccounted=" << accepted_unaccounted
            );
        }


        if (business_executor) {
            const auto before_stats =
                business_executor->GetStats();


            LOG_INFO(
                "gateway business runtime draining"
                << ", pending="
                << before_stats.
                    current_pending_tasks
                << ", active="
                << before_stats.
                    current_active_tasks
                << ", pending_completions="
                << before_stats.
                    pending_completions
                << ", accepted="
                << before_stats.
                    accepted_total
            );


            const bool within_budget =
                business_executor->
                    ShutdownGraceful();


            const auto after_stats =
                business_executor->GetStats();

            const std::uint64_t lifecycle_accounted =
                after_stats.completed_total +
                after_stats.worker_exception_total +
                after_stats.deadline_expired_before_start_total +
                after_stats.cancelled_before_start_total +
                after_stats.completion_dropped_total +
                after_stats.completion_exception_total;

            const std::uint64_t accepted_unaccounted =
                after_stats.accepted_total >= lifecycle_accounted
                    ? after_stats.accepted_total - lifecycle_accounted
                    : 0;


            LOG_INFO(
                "gateway business runtime drained"
                << ", within_budget="
                << within_budget
                << ", submitted="
                << after_stats.submitted_total
                << ", accepted="
                << after_stats.accepted_total
                << ", completed="
                << after_stats.completed_total
                << ", rejected_overload="
                << after_stats.
                    rejected_overload_total
                << ", rejected_hot_key="
                << after_stats.
                    rejected_hot_key_total
                << ", rejected_deadline="
                << after_stats.
                    rejected_deadline_total
                << ", rejected_shutdown="
                << after_stats.
                    rejected_shutdown_total
                << ", rejected_invalid="
                << after_stats.
                    rejected_invalid_total
                << ", deadline_before_start="
                << after_stats.
                    deadline_expired_before_start_total
                << ", cancelled_before_start="
                << after_stats.
                    cancelled_before_start_total
                << ", deadline_terminal_callback="
                << after_stats.
                    deadline_terminal_callback_total
                << ", deadline_terminal_dropped="
                << after_stats.
                    deadline_terminal_dropped_total
                << ", deadline_terminal_exceptions="
                << after_stats.
                    deadline_terminal_exception_total
                << ", worker_exceptions="
                << after_stats.
                    worker_exception_total
                << ", completion_dropped="
                << after_stats.
                    completion_dropped_total
                << ", completion_exceptions="
                << after_stats.
                    completion_exception_total
                << ", current_pending="
                << after_stats.
                    current_pending_tasks
                << ", current_active="
                << after_stats.
                    current_active_tasks
                << ", pending_completions="
                << after_stats.
                    pending_completions
                << ", peak_pending="
                << after_stats.
                    peak_pending_tasks
                << ", peak_active="
                << after_stats.
                    peak_active_tasks
                << ", avg_queue_wait_ms="
                << after_stats.
                    average_queue_wait_ms
                << ", max_queue_wait_ms="
                << after_stats.
                    max_queue_wait_ms
                << ", avg_execution_ms="
                << after_stats.
                    average_execution_ms
                << ", max_execution_ms="
                << after_stats.
                    max_execution_ms
                << ", lifecycle_accounted="
                << lifecycle_accounted
                << ", accepted_unaccounted="
                << accepted_unaccounted
            );
        }


        if (replay_executor) {
            const bool replay_within_budget =
                replay_executor->ShutdownGraceful();
            const auto replay_stats = replay_executor->GetStats();
            const std::uint64_t replay_accounted =
                replay_stats.completed_total +
                replay_stats.worker_exception_total +
                replay_stats.deadline_expired_before_start_total +
                replay_stats.cancelled_before_start_total +
                replay_stats.completion_dropped_total +
                replay_stats.completion_exception_total;
            const std::uint64_t replay_unaccounted =
                replay_stats.accepted_total >= replay_accounted
                    ? replay_stats.accepted_total - replay_accounted
                    : 0;

            LOG_INFO(
                "gateway replay runtime drained"
                << ", within_budget=" << replay_within_budget
                << ", submitted=" << replay_stats.submitted_total
                << ", accepted=" << replay_stats.accepted_total
                << ", completed=" << replay_stats.completed_total
                << ", deadline_before_start="
                << replay_stats.deadline_expired_before_start_total
                << ", cancelled_before_start="
                << replay_stats.cancelled_before_start_total
                << ", peak_pending=" << replay_stats.peak_pending_tasks
                << ", peak_active=" << replay_stats.peak_active_tasks
                << ", avg_queue_wait_ms=" << replay_stats.average_queue_wait_ms
                << ", max_queue_wait_ms=" << replay_stats.max_queue_wait_ms
                << ", accepted_unaccounted=" << replay_unaccounted
            );
        }

        if (rpc_service_discovery) {
            rpc_service_discovery->Stop();
        }

        if (rpc_zookeeper_client) {
            rpc_zookeeper_client->Stop();
        }

        gateway.Stop();
if (redis_pool) {
            redis_pool->Shutdown();
        }

        std::cout << "TinyIMX gateway demo stopped\n";
        std::cout << "==========================================\n";

        g_loop = nullptr;
    }

    process_telemetry.Shutdown();
    tinyimx::Logger::Instance().Shutdown();

    return 0;
}
