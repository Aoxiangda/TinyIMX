#include "services/message/local/LocalPrivateStore.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <sys/resource.h>

int main(int argc,char**argv) {
    using namespace tinyimx;using namespace tinyimx::message;using namespace tinyimx::message::local;
    try {
        if(argc!=6)return 2;
        const std::string backend=argv[1],workload=argv[2],label=argv[3];const std::filesystem::path base=argv[5];
        const auto enabled=[](const char* name){const auto value=std::getenv(name);if(!value)return false;if(std::string(value)!="0"&&std::string(value)!="1")throw std::runtime_error("invalid experiment switch");return std::string(value)=="1";};
        const bool exact_users=enabled("TINYIMX_LOCAL_STORE_EXACT_USERS_ENABLE"),trace_phases=enabled("TINYIMX_LOCAL_STORE_PHASE_TRACE_ENABLE");
        struct OperationTrace {std::int64_t lock_start{0},lock_ns{0},write_start{0},write_ns{0};};
        static thread_local OperationTrace thread_trace;
        if((backend!="mysql"&&backend!="native")||(workload!="send"&&workload!="send-confirm")||
            (label!="a-sql"&&label!="b-native"&&label!="c-native"&&label!="d-sql"))return 2;
        if(!base.is_absolute()||!base.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf104-")||std::filesystem::canonical(base)!=base)return 2;
        Config cfg;if(!cfg.LoadFromFile(argv[4])||cfg.MySql().database!=(workload=="send"?"codex_perf104_20261020_r1":"codex_perf104_20261020_r2")||cfg.MySql().pool_size!=32)
            throw std::runtime_error("owned resource config guard failed");
        if(!Logger::Instance().Init(cfg.Logger()))throw std::runtime_error("resource logger init");
        std::unique_ptr<MySqlConnectionPool> pool;std::unique_ptr<MessageRepository> messages;std::unique_ptr<outbox::OutboxRepository> events;
        std::unique_ptr<MessageRepositoryAdapter> sql;std::unique_ptr<LocalPrivateStore> native;
        if(backend=="mysql") {
            pool=std::make_unique<MySqlConnectionPool>();if(!pool->Initialize(cfg.MySql()))throw std::runtime_error("resource pool initialization failed");
            messages=std::make_unique<MessageRepository>(pool.get());events=std::make_unique<outbox::OutboxRepository>(pool.get());
            sql=std::make_unique<MessageRepositoryAdapter>(messages.get(),pool.get(),events.get(),"codex_perf104_events_20261020_r3");
        }else {
            LocalPrivateStoreOptions options;options.directory=(base/("native-"+workload)).string();options.storage_identity="perf104-resource-"+workload;
            options.message_events_topic="codex_perf104_events_20261020_r3";options.first_message_id=7070000000000000ULL;
            options.exact_user_locking=exact_users;
            for(std::uint64_t id=500001;id<=510000;++id)options.initial_user_ids.push_back(id);
            LocalPrivateStore::CommitObserver observer;
            if(trace_phases)observer=[](auto point,std::uint64_t){
                const auto now=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if(point==LocalPrivateStore::CommitPoint::kBeforeLocks)thread_trace.lock_start=now;
                else if(point==LocalPrivateStore::CommitPoint::kAfterLocks)thread_trace.lock_ns=now-thread_trace.lock_start;
                else if(point==LocalPrivateStore::CommitPoint::kBeforeWrite)thread_trace.write_start=now;
                else if(point==LocalPrivateStore::CommitPoint::kAfterSyncBeforeReply)thread_trace.write_ns=now-thread_trace.write_start;
            };
            native=std::make_unique<LocalPrivateStore>(std::move(options),std::move(observer));
        }
        constexpr unsigned count=2000,threads=8;
        const auto unique=std::chrono::steady_clock::now().time_since_epoch().count();const std::string prefix="p104-"+label+'-'+std::to_string(unique)+'-';
        std::vector<nlohmann::json> samples(count);std::atomic<unsigned> next{0},bad{0};std::vector<std::thread> workers;std::atomic<bool> start{false};
        rusage cpu_before{},cpu_after{};getrusage(RUSAGE_SELF,&cpu_before);const auto begin=std::chrono::steady_clock::now();
        for(unsigned thread=0;thread<threads;++thread)workers.emplace_back([&]{
            while(!start.load())std::this_thread::yield();
            for(;;){const auto i=next.fetch_add(1);if(i>=count)break;
                try {
                    const std::uint64_t from=500001+i%10000,to=500001+((from-500001)^1);const auto cid=prefix+std::to_string(i);const std::string body(128,'x');
                    thread_trace={};
                    const auto t0=std::chrono::steady_clock::now();auto result=native?native->PersistPrivateMessage(from,to,cid,1,body):sql->PersistPrivateMessage(from,to,cid,1,body);
                    const auto t1=std::chrono::steady_clock::now();
                    if(!result.Accepted()||result.outcome!=PersistPrivateMessageOutcome::kCreated||result.record.content!=body||result.record.client_message_id!=cid||result.record.from_user_id!=from||result.record.to_user_id!=to)throw std::runtime_error("resource durable result mismatch");
                    if(workload=="send-confirm") {
                        MessageRepositoryMutationResult ack;
                        if(native)ack=native->ConfirmReceiver(result.message_id,to);
                        else {auto fast=sql->TryConfirmReceiverOwnedIfPending(result.message_id,to);if(!fast)throw std::runtime_error("fresh owned receiver update did not transition");ack=*fast;}
                        if(!ack.Succeeded()||ack.affected_rows!=1)throw std::runtime_error("receiver confirmation mismatch");
                    }
                    const auto t2=std::chrono::steady_clock::now();samples[i]={{"index",i},{"mid",result.message_id},
                        {"persist_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(t1-t0).count()},
                        {"confirm_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t1).count()},
                        {"whole_ns",std::chrono::duration_cast<std::chrono::nanoseconds>(t2-t0).count()}};
                    if(native&&trace_phases){samples[i]["persist_lock_ns"]=thread_trace.lock_ns;samples[i]["persist_engine_write_ns"]=thread_trace.write_ns;}
                }catch(...){++bad;}
            }
        });
        start=true;for(auto&worker:workers)worker.join();const auto end=std::chrono::steady_clock::now();getrusage(RUSAGE_SELF,&cpu_after);
        if(bad)throw std::runtime_error("resource operation failed; no partial performance result");
        const auto micros=[](const timeval& t){return static_cast<std::int64_t>(t.tv_sec)*1000000+t.tv_usec;};
        nlohmann::json counters;
        if(native){const auto s=native->Stats();counters={{"critical_message_commits",s.message_commits},{"critical_receiver_commits",s.confirmation_commits},{"actual_engine_wal_syncs",s.engine_wal_syncs},{"pending_events",s.pending_events}};if(!native->Shutdown())throw std::runtime_error("native shutdown sync failed");}
        if(pool)pool->Shutdown();Logger::Instance().Shutdown();
        const auto seconds=std::chrono::duration<double>(end-begin).count();
        std::cout<<"LOCAL_STORE_RESOURCE "<<nlohmann::json({{"backend",backend},{"workload",workload},{"label",label},{"completed",count},{"threads",threads},{"seconds",seconds},{"repository_only_completed_s",count/seconds},
            {"native_exact_user_locking",exact_users},{"native_phase_trace_enabled",trace_phases},
            {"process_user_us",micros(cpu_after.ru_utime)-micros(cpu_before.ru_utime)},{"process_system_us",micros(cpu_after.ru_stime)-micros(cpu_before.ru_stime)},
            {"process_cpu_comparison_limit","Native process includes its engine; SQL client process excludes MySQL CPU. Do not compare process CPU as equal server resource cost."},
            {"native_counters",counters},{"samples",samples},{"scope","Physical-disk repository-only closed loop, durable message+event and optional durable receiver state. No Gateway, permission RPC, MQ publishing/Redis projection, user-registration migration or full IM capacity claim."}}).dump()<<'\n';
        return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';Logger::Instance().Shutdown();return 1;}
}
