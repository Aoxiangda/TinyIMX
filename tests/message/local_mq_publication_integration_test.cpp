// Real broker + Redis + loopback owner RPC qualification, never a throughput test.
#include "services/message/local/LocalEventRelay.h"
#include "services/message/local/LocalUnreadProjectionSource.h"
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "services/projection/unread/UnreadProjector.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/eventing/rocketmq/OptionalWireSendBatch.h"
#include "services/cache/UnreadCountCache.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <dlfcn.h>
using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::projection::unread;
using namespace tinyimx::eventing::rocketmq_transport;
namespace {
void Check(bool good,const char* text){if(!good)throw std::runtime_error(text);std::cout<<"PASS "<<text<<std::endl;}
template<class F>bool Wait(F&& f){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(45);while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return f();}
}
int main(int argc,char**argv){
    try{
        if(argc!=4&&argc!=5)return 2;const bool config_only=argc==5&&std::string(argv[4])=="--config-only";if(argc==5&&!config_only)return 2;const std::filesystem::path root=argv[2];const std::string trial=argv[3];if(trial!="r1"&&trial!="r2"&&trial!="r3"&&trial!="r4"&&trial!="r5")return 2;Config cfg;
        Check(root.is_absolute()&&(root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf107-live-")||root.string().starts_with("/mnt/tinyimx-benchmark/perf951-mq-smoke-"))&&std::filesystem::canonical(root)==root,"owned physical native/MQ fixture");
        if(!cfg.LoadFromFile(argv[1]))throw std::runtime_error("owned MQ fixture configuration rejected: "+cfg.LastError());
        Check(cfg.RocketMQ().message_topic=="codex_perf107_events_20261020_"+trial&&cfg.UnreadProjection().consumer_group=="codex_perf107_consumer_20261020_"+trial&&cfg.Redis().db==0&&cfg.Redis().port==26379,"exact owned broker/temporary Redis namespace");
        if(config_only){std::cout<<"OWNED_NATIVE_MQ_CONFIG_VALIDATED_NO_CONNECTIONS\n";return 0;}
        Check(Logger::Instance().Init(cfg.Logger()),"runtime logger ready");
        LocalPrivateStoreOptions store_options;store_options.directory=(root/"owner").string();store_options.storage_identity="perf107-live-"+trial;
        store_options.message_events_topic=cfg.RocketMQ().message_topic;store_options.first_message_id=7120000000000000ULL+std::stoull(trial.substr(1))*1000000000000ULL;
        store_options.initial_user_ids={500001,500002,500003};store_options.exact_user_locking=true;store_options.block_cache_bytes=8*1024*1024;
        LocalPrivateStore store(store_options);LocalUnreadProjectionSource source(&store);UnreadSnapshotRpcService service(&source);
        grpc::ServerBuilder builder;int port=0;builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);builder.RegisterService(&service);
        auto server=builder.BuildAndStart();Check(server&&port>0,"real owner snapshot RPC server listening");
        UnreadSnapshotRpcClient client("127.0.0.1:"+std::to_string(port),std::chrono::milliseconds(1000));
        RedisConnectionPool redis;Check(redis.Initialize(cfg.Redis()),"isolated real Redis connection pool");UnreadCountCache cache(&redis);
        RocketMQSimpleConsumerOptions co;co.endpoint=cfg.RocketMQ().endpoint;co.topic=cfg.RocketMQ().message_topic;co.consumer_group=cfg.UnreadProjection().consumer_group;
        co.filter_expression=kUnreadProjectionTagFilter;co.await_duration_ms=5000;co.request_timeout_ms=10000;co.tls=cfg.RocketMQ().tls;co.access_key=cfg.RocketMQ().access_key;co.access_secret=cfg.RocketMQ().access_secret;
        RocketMQSimpleConsumer consumer(co);Check(consumer.Start(),"actual MQ consumer starts with owner RPC active");
        RocketMQProducerOptions po;po.endpoint=cfg.RocketMQ().endpoint;po.topic=cfg.RocketMQ().message_topic;po.request_timeout_ms=10000;po.tls=cfg.RocketMQ().tls;po.access_key=cfg.RocketMQ().access_key;po.access_secret=cfg.RocketMQ().access_secret;
        RocketMQProducer producer(po);Check(producer.Start(),"actual MQ producer starts with both dependency families active");
        const bool wire_batch=producer.MaxBatchMessages()>1;
        using Api=::ROCKETMQ_NAMESPACE::TinyIMXBatchSendApiV1;
        using Stats=::ROCKETMQ_NAMESPACE::TinyIMXBatchSendStatsV1;
        using Getter=const Api*(*)();
        const auto getter=reinterpret_cast<Getter>(dlsym(RTLD_DEFAULT,"tinyimx_rocketmq_batch_send_api_v1"));
        const auto* batch_api=getter?getter():nullptr;Stats before_batch{};
        if(wire_batch){Check(tinyimx::eventing::rocketmq_transport::detail::CompatibleWireSendApi(batch_api),"enabled real producer has exact batch API");batch_api->read_stats(&before_batch);}
        UnreadProjectorOptions projection_options;projection_options.mode=UnreadProjectorMode::kWriter;projection_options.topic=po.topic;projection_options.batch_size=8;projection_options.worker_threads=1;projection_options.invisible_duration_ms=15000;
        UnreadProjector projector(&consumer,&client,&cache,projection_options);Check(projector.Start(),"real projector reads authoritative native snapshots over network");
        LocalEventRelayOptions relay_options;relay_options.workers=2;relay_options.max_held=8;relay_options.scan_page=16;relay_options.sync_interval=std::chrono::milliseconds(20);
        if(wire_batch){relay_options.workers=1;relay_options.max_held=16;relay_options.progress_batch_size=16;}
        LocalEventRelay relay(&store,&producer,relay_options);
        for(unsigned i=0;i<12;++i){const auto p=store.PersistPrivateMessage(i<8?500001:500003,500002,"live-"+std::to_string(i),1,"same durable body");Check(p.Accepted(),"message and publication intent commit together");}
        Check(relay.Start(),"native event relay starts against actual SDK producer");
        auto matches=[&](std::int64_t a,std::int64_t b,std::int64_t total){auto x=cache.GetPrivateUnread(500002,500001),y=cache.GetPrivateUnread(500002,500003),z=cache.GetTotalUnread(500002);return x.Completed()&&y.Completed()&&z.Completed()&&x.count==a&&y.count==b&&z.count==total;};
        Check(Wait([&]{return store.Stats().pending_events==0&&projector.Stats().reconciled_total>=12&&matches(8,4,12);}),"all12 native events publish/consume/ACK and reconcile real Redis via RPC");
        const auto read=store.MarkDialogRead(500002,500001);Check(read.Succeeded()&&read.affected_rows==8,"read transition changes owner state and adds original read event atomically");
        Check(Wait([&]{return store.Stats().pending_events==0&&projector.Stats().reconciled_total>=13&&matches(0,4,4);}),"read event converges zero peer count and exact total in real cache");
        Check(relay.Shutdown(),"relay drains current real sends and syncs progress before producer stop");
        const auto relay_stats=relay.Stats();Check(relay_stats.published==13&&relay_stats.quarantined==0&&relay_stats.held==0&&!relay_stats.failed,"real publication has no lost held work or quarantined records");
        if(wire_batch){
            Stats after_batch{};batch_api->read_stats(&after_batch);
            Check(after_batch.rpc_submissions-before_batch.rpc_submissions==1&&
                  after_batch.submitted_entries-before_batch.submitted_entries==12&&
                  after_batch.rpc_terminals-before_batch.rpc_terminals==1&&
                  after_batch.malformed_responses==before_batch.malformed_responses&&
                  after_batch.synchronous_timeouts==before_batch.synchronous_timeouts,
                  "one real SDK RPC publishes all12 durable native events with exact receipts");
        }
        projector.Shutdown();const auto projection_stats=projector.Stats();
        Check(projection_stats.reconciled_total>=13&&projection_stats.ack_failure_total==0&&projection_stats.poison_failure_total==0,"actual consumer completes projection before ACK without poison or ACK error");
        Check(cache.ReplaceUserUnreadProjection(500002,{{500001,99},{500003,99}},198).Succeeded(),"owned cache intentionally stale for rebuild qualification");
        Check(projector.RebuildUser(500002)&&matches(0,4,4),"network owner rebuild restores all historical peers including zero count");
        producer.Stop();consumer.Stop();server->Shutdown();server->Wait();
        Check(!client.LoadUserSnapshot(500002).success,"stopped authoritative owner fails closed without SQL fallback");
        Check(store.Stats().pending_events==0&&store.ListFailedEvents(256).events.empty()&&store.ReadUserUnreadSnapshot(500002).total_unread==4&&store.CountPending(500002).count==4,"final durable messages, receiver states and unread truth retained");
        redis.Shutdown();Check(store.Shutdown(),"native owner closes after relay and RPC completion");Logger::Instance().Shutdown();
        std::cout<<"LOCAL_NATIVE_MQ_REDIS_RPC_RUNTIME_PASS events=13 messages=12\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
