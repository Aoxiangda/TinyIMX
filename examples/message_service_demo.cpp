#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"
#include "common/observability/ProcessTelemetry.h"
#include "services/registry/zookeeper/ServiceInstance.h"
#include "services/registry/zookeeper/ZooKeeperClient.h"
#include "services/registry/zookeeper/ZooKeeperServiceRegistrar.h"
#include "services/registry/zookeeper/ZooKeeperServiceDiscovery.h"
#include "services/rpc/GroupRpcClient.h"
#include "services/rpc/ServiceEndpointProvider.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/rpc/ZooKeeperServiceEndpointProvider.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/message/application/MessageEventFactory.h"
#include "services/message/server/MessageServiceServer.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/message/service/MessageCallbackService.h"
#include "services/message/service/MessageReadyBatchService.h"
#include "services/repository/MessageRepository.h"
#include "services/outbox/OutboxRepository.h"
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
#include "services/message/local/NativeMessageRuntime.h"
#endif

#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
    // Block before telemetry/gRPC creates threads; children inherit the mask.
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
        std::cerr << "failed to block shutdown signals\n";
        return 1;
    }

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
            "tinyimx-message-service",
            "message"
        )) {
        LOG_ERROR("tinyimx-message-service observability initialization failed");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!config.MySql().enable) {
        LOG_ERROR("MessageService requires mysql.enable=true");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    if (!tinyimx::message::MessageEventFactory::IsValidTopic(config.RocketMQ().message_topic)) {
        LOG_ERROR("MessageService invalid rocketmq.message_topic");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    const char* native_flag=std::getenv("TINYIMX_NATIVE_PRIVATE_STORE_ENABLE");
    const char* native_config=std::getenv("TINYIMX_NATIVE_PRIVATE_STORE_CONFIG");
    if((native_flag&&std::string(native_flag)!="0"&&std::string(native_flag)!="1")||
       (native_config&&native_config[0]&&(!native_flag||std::string(native_flag)!="1"))){
        LOG_ERROR("native Message options require explicit enable=0/1 and matching owner config");return 2;
    }
    const bool native_enabled=native_flag&&std::string(native_flag)=="1";
#ifndef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
    if(native_enabled){LOG_ERROR("native Message runtime support was not built");return 2;}
#endif
    tinyimx::MySqlConnectionPool mysql_pool;
    if (!mysql_pool.Initialize(config.MySql())) {
        LOG_ERROR("MessageService mysql pool initialize failed");
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }
    if(!native_enabled){
        auto lease=mysql_pool.Acquire();tinyimx::MySqlQueryResult owners;
        if(!lease||!lease->Query("SELECT COUNT(*)FROM information_schema.tables WHERE TABLE_SCHEMA=DATABASE()AND TABLE_NAME='im_native_private_owner'",&owners)||owners.rows.size()!=1||owners.rows[0].size()!=1){
            LOG_ERROR("Message private storage ownership preflight failed");return 1;
        }
        if(owners.rows[0][0]=="1"){
            if(!lease->Query("SELECT COUNT(*)FROM im_native_private_owner",&owners)||owners.rows!=std::vector<std::vector<std::string>>{{"0"}}){
                LOG_ERROR("registered native private history prevents SQL fallback; explicit migration required");return 1;
            }
        }else if(owners.rows[0][0]!="0"){LOG_ERROR("unexpected private owner schema");return 1;}
    }

    tinyimx::MessageRepository repository(&mysql_pool);
    tinyimx::outbox::OutboxRepository outbox_repository(&mysql_pool);
    tinyimx::message::MessageRepositoryAdapter repository_adapter(
        &repository,
        &mysql_pool,
        &outbox_repository,
        config.RocketMQ().message_topic
    );
    tinyimx::message::MessageRepositoryPort* selected_repository=&repository_adapter;
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
    std::unique_ptr<tinyimx::message::local::NativeMessageRuntime> native_runtime;
    if(native_enabled){
        if(!native_config||!native_config[0]){LOG_ERROR("native Message owner config required");return 2;}
        try{
            native_runtime=std::make_unique<tinyimx::message::local::NativeMessageRuntime>(
                &mysql_pool,&repository_adapter,config.RocketMQ(),native_config);
            selected_repository=native_runtime->Repository();
            LOG_WARN("experimental native private Message owner active; SQL group authority retained; internal user IDs immutable");
        }catch(const std::exception&e){LOG_ERROR("native Message initialization failed: "<<e.what());return 1;}
    }
#endif
    bool storage_ok=true;
    const auto close_storage=[&]{
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
        if(native_runtime)storage_ok=native_runtime->Shutdown()&&storage_ok;
#endif
        mysql_pool.Shutdown();
    };
    tinyimx::message::MessageApplicationService application_service(
        selected_repository
    );

    std::shared_ptr<
        tinyimx::registry::zookeeper::ZooKeeperClient
    > rpc_zookeeper_client;
    std::shared_ptr<
        tinyimx::registry::zookeeper::ZooKeeperServiceDiscovery
    > rpc_service_discovery;
    std::shared_ptr<const tinyimx::rpc::ServiceEndpointProvider>
        rpc_endpoint_provider;
    std::unique_ptr<tinyimx::rpc::GroupRpcClient> group_rpc_client;

    const auto& discovery_config = config.ServiceDiscovery();
    if (discovery_config.provider == "zookeeper") {
        rpc_zookeeper_client = std::make_shared<
            tinyimx::registry::zookeeper::ZooKeeperClient
        >();
        if (!rpc_zookeeper_client->Start(config.ZooKeeper())) {
            LOG_ERROR(
                "MessageService GroupService discovery client start failed"
                << ", error=" << rpc_zookeeper_client->LastError()
            );
            close_storage();
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
                "MessageService GroupService discovery initial sync failed"
                << ", error=" << rpc_service_discovery->LastError()
            );
            rpc_service_discovery->Stop();
            rpc_zookeeper_client->Stop();
            close_storage();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }

        rpc_endpoint_provider = std::make_shared<
            tinyimx::rpc::ZooKeeperServiceEndpointProvider
        >(rpc_service_discovery, discovery_config);
        group_rpc_client = std::make_unique<tinyimx::rpc::GroupRpcClient>(
            rpc_endpoint_provider
        );

        LOG_INFO(
            "MessageService GroupService RPC enabled"
            << ", provider=zookeeper"
            << ", root=" << config.ZooKeeper().service_root
        );
    } else {
        const char* group_target_env =
            std::getenv("TINYIMX_GROUP_RPC_TARGET");
        const std::string group_target =
            group_target_env != nullptr
                ? std::string(group_target_env)
                : std::string{};

        if (!group_target.empty()) {
            rpc_endpoint_provider = std::make_shared<
                tinyimx::rpc::StaticServiceEndpointProvider
            >(std::string{}, std::string{}, std::string{}, group_target);
            group_rpc_client = std::make_unique<tinyimx::rpc::GroupRpcClient>(
                rpc_endpoint_provider
            );
            LOG_INFO(
                "MessageService GroupService RPC enabled"
                << ", provider=static"
                << ", target=" << group_target
            );
        } else {
            LOG_WARN(
                "MessageService GroupService RPC disabled: "
                "TINYIMX_GROUP_RPC_TARGET is not set; "
                "new group messages will fail closed"
            );
        }
    }

    tinyimx::message::MessageServiceImpl service_impl(
        &application_service,
        group_rpc_client.get()
    );
    std::unique_ptr<tinyimx::message::MessageCallbackService> callback_service;
    if (const char* raw = std::getenv("TINYIMX_MESSAGE_CALLBACK_WORKERS")) {
        const std::string value(raw);
        if (value != "0") {
            if (value != "16" && value != "32") {
                std::cerr << "TINYIMX_MESSAGE_CALLBACK_WORKERS must be 0,16,32\n";
                return 2;
            }
            callback_service = std::make_unique<tinyimx::message::MessageCallbackService>(
                &application_service, group_rpc_client.get(), std::stoul(value), 128);
            LOG_INFO("bounded callback message RPC enabled, workers=" << value
                     << ", queue_capacity=128, methods=persist/get/confirm");
        }
    }
    std::unique_ptr<tinyimx::message::MessageReadyBatchService> ready_batch_service;
    if(const char* raw=std::getenv("TINYIMX_PRIVATE_READY_BATCH_SHARDS")){
        const std::string value(raw);
        if(value!="0"){
            if(callback_service || (value!="1"&&value!="4"&&value!="8")){
                std::cerr<<"ready batch shards must be0/1/4/8 and callback workers must be0\n";
                return 2;
            }
            ready_batch_service=std::make_unique<tinyimx::message::MessageReadyBatchService>(
                &application_service,group_rpc_client.get(),std::stoul(value),128);
            LOG_WARN("private ready batch RPC enabled, shards="<<value<<", global_capacity=128, max_batch=64");
        }
    }
    const auto drain_callbacks=[&]{
        if(callback_service)callback_service->Drain();
        if(ready_batch_service)ready_batch_service->Drain();
    };
    tinyimx::message::MessageServiceServer server(ready_batch_service
        ?static_cast<grpc::Service*>(ready_batch_service.get())
        :(callback_service ? static_cast<grpc::Service*>(callback_service.get()) : &service_impl));
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
    if(native_runtime&&!server.SetAuxiliaryService(native_runtime->SnapshotService())){
        LOG_ERROR("native owner snapshot RPC registration failed");drain_callbacks();close_storage();return 1;
    }
#endif

    const std::string listen_target =
        ResolveListenTarget(argc, argv);

    if (!server.Start(listen_target)) {
        drain_callbacks();
        LOG_ERROR(
            "MessageService start failed"
            << ", target=" << listen_target
        );
        if (rpc_service_discovery != nullptr) {
            rpc_service_discovery->Stop();
        }
        if (rpc_zookeeper_client != nullptr) {
            rpc_zookeeper_client->Stop();
        }
        close_storage();
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
            drain_callbacks();
            close_storage();
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
            drain_callbacks();
            close_storage();
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
            drain_callbacks();
            close_storage();
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
                "MessageService ZooKeeper registration failed"
                << ", error=" << zookeeper_registrar->LastError()
            );
            zookeeper_registrar->Stop();
            zookeeper_client->Stop();
            server.Shutdown();
            server.Wait();
            drain_callbacks();
            close_storage();
            tinyimx::Logger::Instance().Shutdown();
            return 1;
        }
    }

    bool dependencies_ready=true;
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
    dependencies_ready=!native_runtime||native_runtime->Healthy();
#endif
    if (!dependencies_ready||!server.SetReady(true)) {
        std::cerr << "RPC readiness initialization failed\n";
        server.Shutdown();
        server.Wait();
        drain_callbacks();
        return 1;
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
    std::atomic<bool> native_failed{false};

    std::thread signal_thread(
        [&server, registrar_for_signal, signal_set, &native_failed
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
            ,owner=native_runtime.get()
#endif
        ]() mutable {
            int signal_number = 0;
            for(;;){
                timespec timeout{0,200000000};
                const int result=sigtimedwait(&signal_set,nullptr,&timeout);
                if(result>0){signal_number=result;break;}
#ifdef TINYIMX_HAS_NATIVE_MESSAGE_RUNTIME
                if(owner&&!owner->Healthy()){native_failed=true;LOG_ERROR("native Message dependency owner failed; stop RPC readiness and drain");break;}
#endif
                if(errno!=EAGAIN&&errno!=EINTR){LOG_ERROR("Message signal wait failed");native_failed=true;break;}
            }
            (void)server.SetReady(false);
                LOG_INFO(
                    "MessageService shutdown signal received"
                    << ", signal=" << signal_number
                );
                if (registrar_for_signal != nullptr) {
                    registrar_for_signal->Stop();
                }
                server.Shutdown();
        }
    );

    server.Wait();
    drain_callbacks();

    if (signal_thread.joinable()) {
        signal_thread.join();
    }

    if (zookeeper_registrar != nullptr) {
        zookeeper_registrar->Stop();
    }
    if (zookeeper_client != nullptr) {
        zookeeper_client->Stop();
    }
    if (rpc_service_discovery != nullptr) {
        rpc_service_discovery->Stop();
    }
    if (rpc_zookeeper_client != nullptr) {
        rpc_zookeeper_client->Stop();
    }

    close_storage();
    LOG_INFO("MessageService stopped");
    process_telemetry.Shutdown();
    tinyimx::Logger::Instance().Shutdown();
    return storage_ok&&!native_failed.load()?0:1;
}
