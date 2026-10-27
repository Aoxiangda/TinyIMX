#include "services/message/local/NativeMessageRuntime.h"
#include "services/message/local/NativePublicationPolicy.h"
#include "services/message/local/NativeActorReferences.h"
#include "services/message/local/MySqlLegacyIdentity.h"
#include "services/message/local/LocalMessageRepositoryAdapter.h"
#include "services/message/local/LocalUnreadProjectionSource.h"
#include "services/message/local/LocalEventRelay.h"
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "common/config/ConfigTypes.h"
#include "common/logging/LogMacros.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace tinyimx::message::local {
class NativeMessageRuntime::Impl {
public:
    std::shared_ptr<MySqlLegacyIdentity> identity;
    std::unique_ptr<NativeActorReferences> references;
    std::unique_ptr<LocalPrivateStore> store;
    std::unique_ptr<LocalMessageRepositoryAdapter> repository;
    std::unique_ptr<LocalUnreadProjectionSource> source;
    std::unique_ptr<projection::unread::UnreadSnapshotRpcService> snapshot;
    std::unique_ptr<eventing::rocketmq_transport::RocketMQProducer> producer;
    std::unique_ptr<LocalEventRelay> relay;
    std::atomic<bool> closing{false};bool shutdown_result{true};
    Impl(MySqlConnectionPool*pool,MessageRepositoryPort*groups,const RocketMQConfig&mq,const std::string& filename){
        if(!pool||!groups||!mq.enable)throw std::invalid_argument("native Message requires SQL/group authority and enabled MQ");
        const std::filesystem::path file(filename);
        if(!file.is_absolute()||!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>4*1024*1024)throw std::invalid_argument("native owner config requires bounded absolute file");
        std::ifstream input(file);nlohmann::json config;input>>config;
        if(config.at("schema").get<unsigned>()!=1)throw std::invalid_argument("native owner configuration version mismatch");
        const auto publication=ReadNativePublicationPolicy(config);
        LocalPrivateStoreOptions options;options.directory=config.at("directory").get<std::string>();options.storage_identity=config.at("storage_identity").get<std::string>();
        options.first_message_id=config.at("first_message_id").get<std::uint64_t>();options.initial_user_ids=config.at("initial_user_ids").get<std::vector<std::uint64_t>>();
        options.block_cache_bytes=config.value("block_cache_bytes",std::size_t(128*1024*1024));options.max_pending_events=config.value("max_pending_events",std::size_t(65536));
        options.exact_user_locking=true;options.message_events_topic=mq.message_topic;
        options.persist_batch_enable=config.value("persist_batch_enable",false);
        identity=std::make_shared<MySqlLegacyIdentity>(pool,config.value("immutable_ascii_identity_cache_enable",false));options.identity_policy=identity->Policy();
        options.resolve_identity=[id=identity](auto from,auto to,const auto&cid){return id->Resolve(from,to,cid);};
        references=std::make_unique<NativeActorReferences>(pool,options);
        options.before_new_message_commit=[this](auto from,auto to){return references->EnsureActors(from,to);};
        options.admit_events=[this]{return !closing.load()&&store&&store->Healthy()&&references->Healthy()&&relay&&relay->Healthy();};
        store=std::make_unique<LocalPrivateStore>(std::move(options));references->ReconcileBeforeServing(*store);
        repository=std::make_unique<LocalMessageRepositoryAdapter>(store.get(),groups,
            config.value("receiver_confirmation_batch_enable",false));
        source=std::make_unique<LocalUnreadProjectionSource>(store.get());snapshot=std::make_unique<projection::unread::UnreadSnapshotRpcService>(source.get());
        eventing::rocketmq_transport::RocketMQProducerOptions producer_options;producer_options.endpoint=mq.endpoint;producer_options.topic=mq.message_topic;
        producer_options.request_timeout_ms=mq.request_timeout_ms;producer_options.tls=mq.tls;producer_options.access_key=mq.access_key;producer_options.access_secret=mq.access_secret;
        producer=std::make_unique<eventing::rocketmq_transport::RocketMQProducer>(std::move(producer_options));
        if(!producer->Start())throw std::runtime_error("native Message MQ producer startup failed");
        LocalEventRelayOptions relay_options;
        relay_options.workers=publication.workers;
        relay_options.max_held=publication.max_held;
        relay_options.progress_batch_size=publication.progress_batch_size;
        const char* health_flag=std::getenv("TINYIMX_NATIVE_NONBLOCKING_RELAY_HEALTH_ENABLE");
        relay_options.nonblocking_health=health_flag&&health_flag[0]=='1'&&health_flag[1]=='\0';
        LOG_INFO("native publication configuration, workers="<<relay_options.workers
                 <<", max_held="<<relay_options.max_held
                 <<", progress_batch_size="<<relay_options.progress_batch_size
                 <<", source=native_owner.publication (independent of SQL outbox_relay)");
        relay=std::make_unique<LocalEventRelay>(store.get(),producer.get(),std::move(relay_options));if(!relay->Start())throw std::runtime_error("native Message publication startup failed");
    }
    ~Impl(){Shutdown();}
    bool Shutdown()noexcept{
        if(closing.exchange(true))return shutdown_result;
        try{
            if(relay)shutdown_result=relay->Shutdown()&&shutdown_result;
            if(relay&&store){const auto r=relay->Stats();const auto s=store->Stats();LOG_INFO("native private runtime drained, published="<<r.published<<", quarantined="<<r.quarantined<<", held="<<r.held<<", pending_events="<<s.pending_events<<", new_messages="<<s.message_commits<<", success="<<shutdown_result<<", publication_batches="<<s.publication_batches
                    <<", confirmation_commits="<<s.confirmation_commits<<", read_commits="<<s.read_commits
                    <<", id_reservations="<<s.id_reservations<<", engine_wal_syncs="<<s.engine_wal_syncs
                    <<", message_commit_batches="<<store->MessageCommitBatches());
                const auto identity_stats=identity->ImmutableCacheStats();
                if(identity_stats.hits||identity_stats.sql_checks)LOG_INFO("native immutable identity cache, hits="<<identity_stats.hits
                    <<", sql_checks="<<identity_stats.sql_checks<<", positive_users="<<identity_stats.positive_users);
                const char* scan_flag=std::getenv("TINYIMX_NATIVE_EMPTY_EVENT_SCAN_GUARD_ENABLE");
                if(scan_flag&&(scan_flag[0]=='0'||scan_flag[0]=='1')&&scan_flag[1]=='\0')
                    std::clog<<"NATIVE_EVENT_SCAN_SUMMARY enabled="<<scan_flag
                        <<" scans="<<s.event_scans_started<<" empty_skipped="<<s.empty_event_scans_skipped
                        <<" pending="<<s.pending_events<<" published="<<r.published
                        <<" quarantined="<<r.quarantined<<" success="<<shutdown_result<<std::endl;
                const char* index_flag=std::getenv("TINYIMX_NATIVE_PENDING_EVENT_INDEX_ENABLE");
                if(index_flag&&(index_flag[0]=='0'||index_flag[0]=='1')&&index_flag[1]=='\0')
                    std::clog<<"NATIVE_EVENT_INDEX_SUMMARY requested="<<index_flag
                        <<" active="<<s.pending_event_index_active<<" queries="<<s.pending_index_queries
                        <<" scans="<<s.event_scans_started<<" pending="<<s.pending_events
                        <<" published="<<r.published<<" quarantined="<<r.quarantined
                        <<" success="<<shutdown_result<<std::endl;
                const char* held_flag=std::getenv("TINYIMX_NATIVE_RELAY_HELD_FILTER_ENABLE");
                if(held_flag&&(held_flag[0]=='0'||held_flag[0]=='1')&&held_flag[1]=='\0')
                    std::clog<<"NATIVE_RELAY_DECODE_SUMMARY filter="<<r.held_filter_enabled
                        <<" decoded="<<s.event_decode_attempts<<" held_skipped="<<s.held_events_skipped
                        <<" pending="<<s.pending_events<<" success="<<shutdown_result<<std::endl;
                const char* demand_flag=std::getenv("TINYIMX_NATIVE_RELAY_DEMAND_PAGE_ENABLE");
                if(demand_flag&&(demand_flag[0]=='0'||demand_flag[0]=='1')&&demand_flag[1]=='\0')
                    std::clog<<"NATIVE_RELAY_PAGE_SUMMARY demand="<<r.demand_page_enabled
                        <<" pages="<<r.pending_pages<<" decoded="<<r.decoded_page_records
                        <<" unvisited="<<r.page_records_unvisited<<" enqueued="<<r.enqueued_events
                        <<" pending="<<s.pending_events<<" success="<<shutdown_result<<std::endl;
            }
            if(producer)producer->Stop();if(store)shutdown_result=store->Shutdown()&&shutdown_result;
        }
        catch(...){shutdown_result=false;}
        // Release the SQL owner only after the local engine and relay stop.
        references.reset();return shutdown_result;
    }
};
NativeMessageRuntime::NativeMessageRuntime(MySqlConnectionPool*p,MessageRepositoryPort*g,const RocketMQConfig&m,const std::string&f):impl_(std::make_unique<Impl>(p,g,m,f)){}
NativeMessageRuntime::~NativeMessageRuntime()=default;
MessageRepositoryPort*NativeMessageRuntime::Repository()const noexcept{return impl_->repository.get();}
grpc::Service*NativeMessageRuntime::SnapshotService()const noexcept{return impl_->snapshot.get();}
bool NativeMessageRuntime::Healthy()const noexcept{return !impl_->closing.load()&&impl_->store&&impl_->store->Healthy()&&impl_->references&&impl_->references->Healthy()&&impl_->relay&&impl_->relay->Healthy();}
bool NativeMessageRuntime::Shutdown()noexcept{return impl_->Shutdown();}
}
