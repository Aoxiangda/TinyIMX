#include "services/message/local/NativeActorReferences.h"
#include "services/message/local/MySqlLegacyIdentity.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool ok,const char* why){
    if(!ok)throw std::runtime_error(why);
    std::cout<<"PASS "<<why<<std::endl;
}
bool Execute(MySqlConnectionPool& pool,const std::string& sql){
    auto c=pool.Acquire();return c&&c->Execute(sql);
}
std::string Scalar(MySqlConnectionPool& pool,const std::string& sql){
    auto c=pool.Acquire();MySqlQueryResult out;
    if(!c||!c->Query(sql,&out)||out.rows.size()!=1||out.rows[0].size()!=1)
        throw std::runtime_error("owned fixture scalar failed");
    return out.rows[0][0];
}
}
int main(int argc,char** argv){
    try{
        if(argc!=5)return 2;
        const std::string flag=argv[3],mode=argv[4];
        Check(flag=="0"||flag=="1","exact factor value");
        const char* env=std::getenv("TINYIMX_NATIVE_GUARDED_REFERENCE_INSERT_ENABLE");
        Check(env&&flag==env,"requested factor equals actual process environment");
        const auto category=mode=="contracts"?"contracts":
            (mode.ends_with("-pin")?"pin":(mode.ends_with("-local")?"local":"invalid"));
        Config cfg;Check(cfg.LoadFromFile(argv[1])&&
            (cfg.MySql().database=="codex_perf1343_20261026_r1_d"+flag+"_"+category||
             cfg.MySql().database=="codex_perf1343_20261026_r2_d"+flag+"_"+category),
            "exact new owned SQL schema");
        const std::string attempt=cfg.MySql().database.starts_with("codex_perf1343_20261026_r1_")?"r1":"r2";
        const std::filesystem::path root=argv[2];
        Check(root==std::filesystem::path("/mnt/tinyimx-benchmark/guarded-reference-perf1343-"+attempt)/
            ("d"+flag+"-"+category)&&std::filesystem::canonical(root)==root,
            "exact canonical new fixture root");
        auto settings=cfg.MySql();settings.pool_size=4;
        MySqlConnectionPool pool;Check(pool.Initialize(settings),"four live SQL connections");
        auto identity=std::make_shared<MySqlLegacyIdentity>(&pool,true);
        LocalPrivateStoreOptions options;
        options.directory=(root/"owner").string();options.storage_identity="perf1343-"+settings.database;
        options.message_events_topic="codex_perf1343_guarded_references";
        options.first_message_id=7634300000000000ULL;
        options.initial_user_ids={500001,500002};
        options.exact_user_locking=true;options.persist_batch_enable=true;
        options.block_cache_bytes=8*1024*1024;
        options.identity_policy=identity->Policy();
        options.resolve_identity=[identity](auto f,auto t,const auto& cid){return identity->Resolve(f,t,cid);};
        std::unique_ptr<NativeActorReferences> refs;
        options.before_new_message_commit=[&refs](auto f,auto t){return refs->EnsureActors(f,t);};
        options.admit_events=[&refs]{return refs&&refs->Healthy();};
        refs=std::make_unique<NativeActorReferences>(&pool,options);
        LocalPrivateStore::CommitObserver observer;
        if(mode.starts_with("crash-"))observer=[&](auto point,auto){
            const auto target=mode=="crash-pin"?LocalPrivateStore::CommitPoint::kBeforeWrite:
                LocalPrivateStore::CommitPoint::kAfterSyncBeforeReply;
            if(point==target){std::cout<<"GUARDED_REFERENCE_CRASH_READY "<<mode<<std::endl;std::raise(SIGSTOP);}
        };
        {
            LocalPrivateStore store(options,observer);refs->ReconcileBeforeServing(store);
            if(mode.starts_with("crash-")){
                (void)store.PersistPrivateMessage(500001,500002,"lost-reply",1,"exact durable payload");
                return 3;
            }
            if(mode.starts_with("verify-")){
                const auto found=store.FindPrivateMessageByClientMessageId(500001,"lost-reply");
                if(mode=="verify-pin"){
                    Check(found.Succeeded()&&!found.Found()&&
                        Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="0",
                        "death after pin before local commit removes orphan pins only");
                }else{
                    Check(found.Found()&&found.record.content=="exact durable payload"&&
                        Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="2",
                        "death after local sync retains message and both logical parents");
                    const auto retry=store.PersistPrivateMessage(500001,500002,"lost-reply",1,"exact durable payload");
                    Check(retry.outcome==PersistPrivateMessageOutcome::kReused&&
                        retry.message_id==found.record.message_id,
                        "lost durable reply resolves to exact original CID and MID");
                }
                Check(store.Shutdown(),"recovered fixture closes");refs.reset();
                std::cout<<"GUARDED_REFERENCE_REAL_RECOVERY_PASS"<<std::endl;return 0;
            }
            Check(mode=="contracts","known fixture mode");
            Check(Scalar(pool,"SELECT COUNT(*) FROM information_schema.referential_constraints WHERE CONSTRAINT_SCHEMA=DATABASE()")=="0",
                  "physical FK count stays zero");
            Check(refs->EnsureActors(500001,500002).Succeeded()&&
                Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="2",
                "cold valid pair creates exactly both durable pins");
            auto stats=refs->SqlStats();
            Check(flag=="0"?(stats.guarded_insert_attempts==0):
                (stats.guarded_insert_attempts==1&&stats.guarded_insert_fast_successes==1&&stats.guarded_insert_readbacks==0),
                "new cold pair uses one guarded command without readback when enabled");
            Check(!refs->EnsureActors(500003,999999999).Succeeded()&&
                Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs WHERE user_id=500003")=="0",
                "one missing parent leaves no partial valid-parent registration");
            const auto invalid_before=refs->SqlStats();
            Check(!refs->EnsureActors(0,500003).Succeeded()&&!refs->EnsureActors(500003,500003).Succeeded()&&
                refs->SqlStats().guarded_insert_attempts==invalid_before.guarded_insert_attempts,
                "zero and same-actor requests do not reach SQL");
            Check(Execute(pool,"INSERT INTO im_native_private_actor_refs(owner_slot,user_id)VALUES(1,500003)"),
                "seed only exact conservative orphan pin");
            auto before=refs->SqlStats();
            Check(refs->EnsureActors(500003,500004).Succeeded()&&
                Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="4",
                "mixed existing and new pins are disambiguated without another INSERT");
            stats=refs->SqlStats();
            Check(flag=="0"||(stats.guarded_insert_attempts==before.guarded_insert_attempts+1&&
                stats.guarded_insert_readbacks==before.guarded_insert_readbacks+1),
                "mixed duplicate result is one write plus read-only validation");
            Check(Execute(pool,"INSERT INTO im_native_private_actor_refs(owner_slot,user_id)VALUES(1,500005),(1,500006)")&&
                refs->EnsureActors(500005,500006).Succeeded(),
                "all-duplicate SQL pins remain successful after read-only checks");
            // Save the owner in this new fixture only. Temporary tables belong
            // to pooled sessions and cannot be used across leases.
            Check(Execute(pool,"CREATE TABLE perf1343_owner_saved LIKE im_native_private_owner")&&
                Execute(pool,"INSERT INTO perf1343_owner_saved SELECT * FROM im_native_private_owner")&&
                Execute(pool,"DELETE FROM im_native_private_owner WHERE owner_slot=1"),
                "remove only new fixture owner row while advisory owner lease remains");
            Check(!refs->EnsureActors(500007,500008).Succeeded()&&
                Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs WHERE user_id IN(500007,500008)")=="0",
                "missing owner cannot create any new pin");
            Check(Execute(pool,"INSERT INTO im_native_private_owner SELECT * FROM perf1343_owner_saved"),
                "restore exact new fixture singleton owner");
            Check(refs->EnsureActors(500007,500008).Succeeded(),
                "failed owner condition releases reservations for a valid later call");
            {
                std::vector<MySqlConnectionLease> leases;
                for(unsigned i=0;i<3;++i){
                    auto lease=pool.Acquire();
                    Check(lease&&lease->Execute("SET SESSION innodb_lock_wait_timeout=1"),
                        "bound every available fixture session lock wait");
                    leases.push_back(std::move(lease));
                }
                leases.clear();
                auto blocker=pool.Acquire();
                Check(blocker&&blocker->BeginTransaction()&&
                    blocker->Execute("INSERT INTO im_native_private_actor_refs(owner_slot,user_id)VALUES(1,500009)"),
                    "hold only exact new fixture registration key");
                before=refs->SqlStats();
                const bool rejected=!refs->EnsureActors(500009,500010).Succeeded();
                const auto after=refs->SqlStats();
                const bool released=blocker->Rollback();blocker.Reset();
                Check(rejected&&released&&
                    Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs WHERE user_id IN(500009,500010)")=="0",
                    "statement timeout returns failure without partial durable pins");
                Check(flag=="0"||(after.guarded_insert_attempts==before.guarded_insert_attempts+1&&
                    after.guarded_insert_readbacks==before.guarded_insert_readbacks),
                    "failed SQL statement is not replayed or read back as success");
                Check(refs->EnsureActors(500009,500010).Succeeded(),
                    "explicit new caller succeeds after failed registration reservation is released");
            }
            {
                std::string insert="INSERT INTO im_users(user_id,username,nickname,status)VALUES";
                for(unsigned i=0;i<64;++i){
                    if(i)insert+=',';
                    insert+='('+std::to_string(530001+i)+",'guard_batch_"+std::to_string(i)+"','Fixture',1)";
                }
                Check(Execute(pool,insert),"create exact64 new batch-only parents");
                std::vector<std::future<MessageRepositoryMutationResult>> calls;calls.reserve(32);
                auto blocker=pool.Acquire();
                Check(blocker&&blocker->Execute("LOCK TABLES im_native_private_actor_refs WRITE"),
                    "hold only new fixture registration table for ready-batch burst");
                struct Unlock {
                    MySqlConnectionLease& lease;
                    ~Unlock(){if(lease)(void)lease->Execute("UNLOCK TABLES");}
                } unlock{blocker};
                for(unsigned i=0;i<32;++i)calls.push_back(std::async(std::launch::async,[&,i]{
                    return refs->EnsureActors(530001+2*i,i==31?999999999:530002+2*i);
                }));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                const bool released=blocker->Execute("UNLOCK TABLES");blocker.Reset();
                bool correct=true;
                for(unsigned i=0;i<32;++i)correct=(calls[i].get().Succeeded()==(i!=31))&&correct;
                Check(released&&correct&&
                    Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs WHERE user_id BETWEEN 530001 AND 530064")=="62",
                    "mixed ready batches retain every valid pair and no half of missing pair");
            }
            const auto sent=store.PersistPrivateMessage(500001,500002,"complete",1,"body");
            Check(sent.Accepted()&&
                store.PersistPrivateMessage(500001,500002,"complete",1,"body").outcome==PersistPrivateMessageOutcome::kReused,
                "one durable message and CID idempotency remain intact");
            Check(Execute(pool,"UPDATE im_users SET status=2 WHERE user_id=500002")&&
                Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs WHERE user_id IN(500001,500002)")=="2",
                "status lifecycle retains both immutable parent identities");
            const auto owner_id=refs->OwnerSqlSessionId();
            Check(Execute(pool,"KILL CONNECTION "+std::to_string(owner_id)),
                "terminate only owned fixture SQL ownership session");
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(refs->Healthy()&&std::chrono::steady_clock::now()<until)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            Check(!refs->Healthy()&&!store.PersistPrivateMessage(500003,500004,"fenced",1,"denied").Accepted(),
                "lost owner lease fences new native messages despite positive identity cache");
            Check(store.Shutdown(),"first owner closes");refs.reset();
        }
        refs=std::make_unique<NativeActorReferences>(&pool,options);
        {
            LocalPrivateStore restored(options);refs->ReconcileBeforeServing(restored);
            Check(restored.FindPrivateMessageByClientMessageId(500001,"complete").Found()&&
                Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="2",
                "reopen retains durable actors and removes only uncommitted orphan pins");
            Check(restored.Shutdown(),"reconciled fixture closes");refs.reset();
        }
        std::cout<<"GUARDED_REFERENCE_REAL_SQL_CONTRACTS_PASS flag="<<flag<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
