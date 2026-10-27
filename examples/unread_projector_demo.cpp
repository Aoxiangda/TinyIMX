#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/projection/unread/UnreadProjectionReader.h"
#include "services/projection/unread/UnreadProjector.h"
#ifdef TINYIMX_HAS_LOCAL_STORE_EXPERIMENT
#include "services/projection/unread/UnreadSnapshotRpc.h"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace {
std::atomic<bool> g_stop{false};
void HandleSignal(int) {
    g_stop.store(true, std::memory_order_release);
}
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::cerr
            << "usage: unread_projector_demo <config.json> "
               "[--rebuild-user <user_id>]\n";
        return 2;
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1])) {
        std::cerr << "config load failed: " << config.LastError() << '\n';
        return 2;
    }
    if (!config.UnreadProjection().enable) {
        std::cerr << "unread_projection.enable must be true\n";
        return 2;
    }
    const char* snapshot_batch_env = std::getenv("TINYIMX_UNREAD_SNAPSHOT_BATCH_ENABLE");
    if (snapshot_batch_env != nullptr && std::string(snapshot_batch_env) != "0" &&
        std::string(snapshot_batch_env) != "1") {
        std::cerr << "snapshot batch option must be 0 or 1\n";
        return 2;
    }
    const bool snapshot_batch = snapshot_batch_env != nullptr &&
        std::string(snapshot_batch_env) == "1";
    if (snapshot_batch && (config.UnreadProjection().owner != "projector" ||
                          config.UnreadProjection().shadow_mode)) {
        std::cerr << "snapshot batching requires exclusive projector writer\n";
        return 2;
    }
    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "logger init failed\n";
        return 2;
    }

    tinyimx::MySqlConnectionPool mysql_pool;
    tinyimx::RedisConnectionPool redis_pool;
    if (!mysql_pool.Initialize(config.MySql())) {
        std::cerr << "MySQL pool init failed\n";
        return 2;
    }
    if (!redis_pool.Initialize(config.Redis())) {
        std::cerr << "Redis pool init failed\n";
        return 2;
    }

    tinyimx::UnreadCountCache cache(&redis_pool);
    tinyimx::projection::unread::UnreadProjectionReader reader(&mysql_pool);
    tinyimx::projection::unread::UnreadProjectionSource* selected_reader=&reader;
    const auto native_flag=std::getenv("TINYIMX_NATIVE_PRIVATE_STORE_ENABLE");
    const auto native_target=std::getenv("TINYIMX_NATIVE_UNREAD_RPC_TARGET");
    if(native_flag&&std::string(native_flag)!="0"&&std::string(native_flag)!="1"){
        std::cerr<<"native store switch must be0or1\n";return 2;
    }
    const bool native=native_flag&&std::string(native_flag)=="1";
    const auto batch_rpc_flag=std::getenv("TINYIMX_NATIVE_UNREAD_BATCH_RPC_ENABLE");
    if(batch_rpc_flag&&std::string(batch_rpc_flag)!="0"&&std::string(batch_rpc_flag)!="1"){
        std::cerr<<"native unread batch RPC switch must be0or1\n";return 2;
    }
    const bool batch_rpc=batch_rpc_flag&&std::string(batch_rpc_flag)=="1";
    if(batch_rpc&&(!native||!snapshot_batch)){
        std::cerr<<"native unread batch RPC requires native owner and snapshot batching\n";return 2;
    }
#ifdef TINYIMX_HAS_LOCAL_STORE_EXPERIMENT
    std::unique_ptr<tinyimx::projection::unread::UnreadSnapshotRpcClient> owner_reader;
    if(native){
        if(!native_target||!native_target[0]){std::cerr<<"native unread owner target required\n";return 2;}
        try{owner_reader=std::make_unique<tinyimx::projection::unread::UnreadSnapshotRpcClient>(native_target,std::chrono::milliseconds(1000),batch_rpc);}
        catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
        selected_reader=owner_reader.get();
    }else if(native_target&&native_target[0]){std::cerr<<"owner target requires explicit native switch\n";return 2;}
#else
    if(native||(native_target&&native_target[0])){std::cerr<<"native unread owner support not built\n";return 2;}
#endif

    if (argc == 4) {
        if (std::string(argv[2]) != "--rebuild-user") {
            std::cerr << "unknown option: " << argv[2] << '\n';
            return 2;
        }
        std::uint64_t user_id = 0;
        try {
            user_id = std::stoull(argv[3]);
        } catch (...) {
            std::cerr << "invalid user_id\n";
            return 2;
        }

        // Rebuild does not need to consume MQ; use a null consumer only after
        // the durable snapshot has been applied below through a small local
        // projector composition with no run-loop start.
        class NoopConsumer final : public tinyimx::eventing::EventConsumer {
        public:
            tinyimx::eventing::EventReceiveResult Receive(std::size_t, int) override {
                return {};
            }
            tinyimx::eventing::EventAckResult Ack(
                const tinyimx::eventing::ConsumedEvent&
            ) override {
                return {};
            }
        } noop_consumer;

        tinyimx::projection::unread::UnreadProjectorOptions options;
        options.mode = tinyimx::projection::unread::UnreadProjectorMode::kWriter;
        tinyimx::projection::unread::UnreadProjector projector(
            &noop_consumer,
            selected_reader,
            &cache,
            options
        );
        std::string error;
        if (!projector.RebuildUser(user_id, &error)) {
            std::cerr << "rebuild failed: " << error << '\n';
            return 1;
        }
        std::cout << "unread projection rebuilt for user=" << user_id << '\n';
        return 0;
    }

    const auto& projection_config = config.UnreadProjection();
    if (projection_config.owner == "gateway" && !projection_config.shadow_mode) {
        std::cerr
            << "projector run rejected: owner=gateway requires shadow_mode=true\n";
        return 2;
    }

    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions consumer_options;
    consumer_options.endpoint = config.RocketMQ().endpoint;
    consumer_options.topic = config.RocketMQ().message_topic;
    consumer_options.consumer_group = projection_config.consumer_group;
    consumer_options.filter_expression =
        tinyimx::projection::unread::kUnreadProjectionTagFilter;
    consumer_options.request_timeout_ms =
        projection_config.consumer_request_timeout_ms;
    consumer_options.await_duration_ms = projection_config.await_duration_ms;
    consumer_options.tls = config.RocketMQ().tls;
    consumer_options.access_key = config.RocketMQ().access_key;
    consumer_options.access_secret = config.RocketMQ().access_secret;

    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumer consumer(
        std::move(consumer_options)
    );
    if (!consumer.Start()) {
        std::cerr << consumer.LastError() << '\n';
        return 2;
    }

    tinyimx::projection::unread::UnreadProjectorOptions options;
    options.mode = projection_config.owner == "projector"
        ? tinyimx::projection::unread::UnreadProjectorMode::kWriter
        : tinyimx::projection::unread::UnreadProjectorMode::kShadow;
    options.topic = config.RocketMQ().message_topic;
    options.batch_size = projection_config.batch_size;
    options.worker_threads = projection_config.worker_threads;
    const char* batch_ack = std::getenv("TINYIMX_UNREAD_ACK_BATCH_ENABLE");
    const bool batch_ack_enabled = batch_ack != nullptr && std::string(batch_ack) == "1";
    options.parallel_batch_ack = batch_ack_enabled;
    options.batch_snapshot_reads = snapshot_batch;
    options.invisible_duration_ms = projection_config.invisible_duration_ms;
    options.receive_error_backoff_ms = projection_config.receive_error_backoff_ms;

    tinyimx::projection::unread::UnreadProjector projector(
        &consumer,
        selected_reader,
        &cache,
        std::move(options)
    );
    if (!projector.Start()) {
        std::cerr << "projector start failed: " << projector.LastError() << '\n';
        return 2;
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    while (!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    projector.Shutdown();
    const auto wire_ack = consumer.ReadWireAckStats();
    consumer.Stop();
    const auto stats = projector.Stats();
    std::cout
        << "unread projector stopped"
        << ", received=" << stats.received_total
        << ", reconciled=" << stats.reconciled_total
        << ", shadow_match=" << stats.shadow_match_total
        << ", shadow_mismatch=" << stats.shadow_mismatch_total
        << ", process_failure=" << stats.process_failure_total
        << ", ack_failure=" << stats.ack_failure_total
        << ", snapshot_batch_calls=" << stats.snapshot_batch_calls
        << ", snapshot_batch_dialogs=" << stats.snapshot_batch_dialogs
        << ", ack_batch_enabled=" << batch_ack_enabled
        << ", wire_ack_scope=process_global_new_api_only"
        << ", wire_ack_started=" << wire_ack.started
        << ", wire_ack_enabled=" << wire_ack.wire_batch_enabled
        << ", wire_ack_capability=" << wire_ack.capability_available
        << ", wire_ack_snapshot_available=" << wire_ack.snapshot_available
        << ", wire_ack_api_calls=" << wire_ack.api_calls
        << ", wire_ack_rpc_submissions=" << wire_ack.rpc_submissions
        << ", wire_ack_entries=" << wire_ack.submitted_entries
        << ", wire_ack_rpc_terminals=" << wire_ack.rpc_terminals
        << '\n';

    redis_pool.Shutdown();
    mysql_pool.Shutdown();
    return 0;
}
