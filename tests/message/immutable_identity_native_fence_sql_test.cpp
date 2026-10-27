#include "services/message/local/MySqlLegacyIdentity.h"
#include "services/message/local/NativeActorReferences.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);std::cout<<"PASS "<<why<<std::endl;}
std::string Scalar(MySqlConnectionPool& p,const std::string& q){
    auto c=p.Acquire();MySqlQueryResult rows;
    Check(c&&c->Query(q,&rows)&&rows.rows.size()==1&&rows.rows[0].size()==1,"owned SQL scalar");return rows.rows[0][0];
}
bool Execute(MySqlConnectionPool& p,const std::string& q){auto c=p.Acquire();return c&&c->Execute(q);}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& schema,
    const std::shared_ptr<MySqlLegacyIdentity>& identity,std::unique_ptr<NativeActorReferences>& refs){
    LocalPrivateStoreOptions o;o.directory=(root/"owner").string();o.storage_identity="perf611-"+schema;
    o.message_events_topic="codex_perf611_identity_native";o.first_message_id=7611000000000000ULL;
    o.initial_user_ids={500001,500002};o.exact_user_locking=true;o.persist_batch_enable=true;o.block_cache_bytes=8*1024*1024;
    o.identity_policy=identity->Policy();o.resolve_identity=[identity](auto f,auto t,const auto& id){return identity->Resolve(f,t,id);};
    o.before_new_message_commit=[&refs](auto f,auto t){return refs->EnsureActors(f,t);};
    // The production runtime additionally checks closing and relay health. This
    // limited integration exercises only the real SQL owner admission clause.
    o.admit_events=[&refs]{return refs&&refs->Healthy();};return o;
}
}
int main(int argc,char**argv){
    try{
        if(argc!=4)return 2;Config cfg;const std::filesystem::path root=argv[2];const std::string mode=argv[3];
        Check((mode=="release"||mode=="sanitizer")&&cfg.LoadFromFile(argv[1])&&
            (cfg.MySql().database=="codex_perf611_20261022_r1_"+mode||cfg.MySql().database=="codex_perf611_20261022_r2_"+mode||
             cfg.MySql().database=="codex_perf994_20261025_r1_"+mode||
             cfg.MySql().database=="codex_perf994_20261025_r2_"+mode),"exact new owned fixture schema");
        const std::string attempt=cfg.MySql().database=="codex_perf611_20261022_r1_"+mode?"r1":"r2";
        const bool current_fixture=cfg.MySql().database=="codex_perf994_20261025_r1_"+mode||cfg.MySql().database=="codex_perf994_20261025_r2_"+mode;
        const std::string current_attempt=cfg.MySql().database=="codex_perf994_20261025_r1_"+mode?"r1":"r2";
        Check(root.is_absolute()&&
            (current_fixture?root.string()=="/mnt/tinyimx-benchmark/immutable-identity-current-perf994-"+current_attempt+"/native-"+mode:
             root.string()=="/dev/shm/perf611-native-fence-"+attempt+"/native-"+mode)&&
            std::filesystem::canonical(root)==root,"exact new temporary native fixture");
        auto settings=cfg.MySql();settings.pool_size=4;std::uint64_t first_mid=0,second_mid=0;
        {
            MySqlConnectionPool pool;Check(pool.Initialize(settings),"live bounded SQL pool");
            auto identity=std::make_shared<MySqlLegacyIdentity>(&pool,true);std::unique_ptr<NativeActorReferences> refs;
            auto options=Options(root,settings.database,identity,refs);refs=std::make_unique<NativeActorReferences>(&pool,options);
            LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
            Check(Scalar(pool,"SELECT COUNT(*) FROM information_schema.referential_constraints WHERE CONSTRAINT_SCHEMA=DATABASE()") =="0","FK-free fixture uses application reference ownership");
            const auto a=store.PersistPrivateMessage(500001,500002,"first",1,"durable first");
            const auto b=store.PersistPrivateMessage(500002,500001,"second",1,"durable second");
            Check(a.Accepted()&&b.Accepted()&&a.message_id<b.message_id,"cached path persists two ordered durable messages");first_mid=a.message_id;second_mid=b.message_id;
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="2"&&store.Stats().message_commits==2,"two logical references and exactly two native commits");
            Check(identity->Resolve(500001,500003,"metadata_only").status==MessageApplicationStatus::kSucceeded&&
                identity->ImmutableCacheStats().positive_users==3,"positive-only metadata cache contains SQL-verified third user");
            const auto before=store.Stats();auto killer=pool.Acquire();Check(static_cast<bool>(killer),"hold same owned user's independent fault-injection connection");
            const auto owner=refs->OwnerSqlSessionId();pool.Shutdown();
            Check(identity->Resolve(500001,500002,"cached_no_pool").status==MessageApplicationStatus::kSucceeded,"stable cached metadata alone remains readable without SQL pool");
            Check(killer->Execute("KILL CONNECTION "+std::to_string(owner)),"kill only this owned fixture's native SQL owner session");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(refs->Healthy()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Check(!refs->Healthy(),"real owner monitor detects lost SQL ownership without reconnecting");
            const auto blocked=store.PersistPrivateMessage(500001,500002,"fenced_new",1,"must not commit");
            Check(!blocked.Accepted(),"known cached users cannot bypass owner admission fence");
            const auto unknown=store.PersistPrivateMessage(500001,999999999,"unknown",1,"must not commit");
            Check(!unknown.Accepted(),"unverified user remains rejected without SQL authority");
            const auto retry=store.PersistPrivateMessage(500001,500002,"first",1,"durable first");
            Check(retry.outcome==PersistPrivateMessageOutcome::kReused&&retry.message_id==first_mid,"already durable CID retry returns original MID without new mutation");
            const auto batch=store.PersistPrivateMessages({{500001,500002,"fenced_batch",1,"must not commit"},
                {500001,999999999,"missing_batch",1,"must not commit"},{500001,500002,"first",1,"durable first"}});
            Check(batch.size()==3&&!batch[0].Accepted()&&!batch[1].Accepted()&&batch[2].outcome==PersistPrivateMessageOutcome::kReused&&batch[2].message_id==first_mid,
                "mixed atomic-batch path fences new items and preserves durable retry");
            const auto after=store.Stats();Check(after.message_commits==before.message_commits&&after.pending_events==before.pending_events,
                "owner loss creates neither new messages nor outbox events");
            Check(store.Shutdown(),"fenced store shuts down cleanly");refs.reset();killer.Reset();
        }
        {
            MySqlConnectionPool pool;Check(pool.Initialize(settings),"fresh pool after owner loss");
            auto identity=std::make_shared<MySqlLegacyIdentity>(&pool,true);std::unique_ptr<NativeActorReferences> refs;
            auto options=Options(root,settings.database,identity,refs);refs=std::make_unique<NativeActorReferences>(&pool,options);
            LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
            const auto a=store.GetPrivateMessage(first_mid),b=store.GetPrivateMessage(second_mid);
            Check(a.Found()&&b.Found()&&a.record.content=="durable first"&&b.record.content=="durable second","same owner reopens both durable bodies/MIDs after lost SQL session");
            Check(store.PersistPrivateMessage(500001,500002,"first",1,"durable first").outcome==PersistPrivateMessageOutcome::kReused,"restart retains CID idempotency");
            const auto c=store.PersistPrivateMessage(500001,500002,"after_restart",1,"durable third");
            Check(c.Accepted()&&c.message_id>second_mid&&store.Stats().message_commits==1,"new ownership admits exactly one new ordered message");
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="2"&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="0","native restart retains exact logical references without SQL message duplication");
            Check(store.Shutdown(),"recovered native store closes before quiesced lifecycle test");refs.reset();
        }
        {
            // Only this newly owned fixture's unreferenced metadata-only user is
            // purged, after every store, owner and positive cache is destroyed.
            MySqlConnectionPool pool;Check(pool.Initialize(settings),"quiesced fixture lifecycle pool");
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs WHERE user_id=500003")=="0","purged fixture user has no native message references");
            Check(Execute(pool,"DELETE FROM im_users WHERE user_id=500003"),"quiesced purge touches only owned unreferenced user");
            {MySqlLegacyIdentity fresh(&pool,true);Check(fresh.Resolve(500001,500003,"purged").status!=MessageApplicationStatus::kSucceeded,"recreated cache rejects physically purged immutable parent");}
            Check(Execute(pool,"INSERT INTO im_users SELECT * FROM im_users_before WHERE user_id=500003"),"restore exact owned fixture row after purge contract");
            {MySqlLegacyIdentity fresh(&pool,true);Check(fresh.Resolve(500001,500003,"restored").status==MessageApplicationStatus::kSucceeded,"recreated cache accepts restored SQL-authoritative parent");}
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_users")=="5","owned SQL users restored exactly");
        }
        std::cout<<"PASS IMMUTABLE_IDENTITY_NATIVE_OWNER_FENCE mode="<<mode<<" durable_messages=3 physical_foreign_keys=0 actual_runtime_MQ_test_pending=1 formal_performance_pending=1\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
