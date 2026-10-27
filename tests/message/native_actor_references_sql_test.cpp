#include "services/message/local/NativeActorReferences.h"
#include "services/message/local/MySqlLegacyIdentity.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include <chrono>
#include <csignal>
#include <filesystem>
#include <future>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);std::cout<<"PASS "<<text<<std::endl;}
bool Execute(MySqlConnectionPool&p,const std::string&q){auto c=p.Acquire();return c&&c->Execute(q);}
std::string Scalar(MySqlConnectionPool&p,const std::string&q){auto c=p.Acquire();MySqlQueryResult rows;Check(c&&c->Query(q,&rows)&&rows.rows.size()==1&&rows.rows[0].size()==1,"bounded owned SQL observer");return rows.rows[0][0];}
template<class F>bool Reject(F&& f){try{f();return false;}catch(const std::exception&){return true;}}
}
int main(int argc,char**argv){
    try{
        if(argc!=4)return 2;Config cfg;const std::filesystem::path root=argv[2];const std::string mode=argv[3];
        Check(cfg.LoadFromFile(argv[1])&&(cfg.MySql().database=="codex_perf146_20261020_r2"||cfg.MySql().database=="codex_perf110_20261020_r1"||cfg.MySql().database=="codex_perf110_20261020_r2"||cfg.MySql().database=="codex_perf110_20261020_r3"||cfg.MySql().database=="codex_perf110_20261020_r4"||cfg.MySql().database=="codex_perf110_20261020_r5"||cfg.MySql().database=="codex_perf110_20261020_r6"||cfg.MySql().database=="codex_perf110_20261020_r7"||cfg.MySql().database=="codex_perf110_20261020_r8"||cfg.MySql().database=="codex_perf110_20261020_r9"||cfg.MySql().database=="codex_perf110_20261020_r10"||cfg.MySql().database=="codex_perf110_20261020_r11"||cfg.MySql().database=="codex_perf110_20261020_r12"||cfg.MySql().database=="codex_perf937_20261024_r1"||cfg.MySql().database=="codex_perf937_20261024_r2"||cfg.MySql().database=="codex_perf937_20261024_r3"||cfg.MySql().database=="codex_perf937_20261024_r4"||cfg.MySql().database=="codex_perf937_20261024_r5"||cfg.MySql().database=="codex_perf937_20261024_r6"||cfg.MySql().database=="codex_perf937_20261024_r7"||cfg.MySql().database=="codex_perf937_20261024_r8"||cfg.MySql().database=="codex_perf937_20261024_r9"||cfg.MySql().database=="codex_perf961_20261025_r1"||cfg.MySql().database=="codex_perf961_20261025_r2"||cfg.MySql().database=="codex_perf961_20261025_r3"||cfg.MySql().database=="codex_perf961_20261025_r4"||cfg.MySql().database=="codex_perf961_20261025_r5"||cfg.MySql().database=="codex_perf961_20261025_r6"||cfg.MySql().database=="codex_perf961_20261025_r7"||cfg.MySql().database=="codex_perf961_20261025_r8"||cfg.MySql().database=="codex_perf961_20261025_r9"||cfg.MySql().database=="codex_perf971_20261025_r1"||cfg.MySql().database=="codex_perf971_20261025_r2"||cfg.MySql().database=="codex_perf971_20261025_r3"||cfg.MySql().database=="codex_perf971_20261025_r4"||cfg.MySql().database=="codex_perf971_20261025_r5"||cfg.MySql().database=="codex_perf971_20261025_r6"||cfg.MySql().database=="codex_perf971_20261025_r7"||cfg.MySql().database=="codex_perf971_20261025_r8"||cfg.MySql().database=="codex_perf971_20261025_r9"||cfg.MySql().database=="codex_perf971_20261025_r10"),"exact owned lifetime schema");
        Check(root.is_absolute()&&(root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf110-")||root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/fk-free-qualification-r146/")||root.string().starts_with("/mnt/tinyimx-benchmark/native-reference-ready-perf937-sql-r1/")||root.string().starts_with("/mnt/tinyimx-benchmark/native-reference-ready-perf937-sql-r2/")||root.string().starts_with("/mnt/tinyimx-benchmark/native-reference-current-perf961-r1/")||root.string().starts_with("/mnt/tinyimx-benchmark/native-reference-budget-perf971-r1/"))&&std::filesystem::canonical(root)==root,"owned physical lifetime fixture");
        Check(Logger::Instance().Init(cfg.Logger()),"reference fixture logger");MySqlConnectionPool pool;auto c=cfg.MySql();c.pool_size=mode=="batch-production"?32:4;Check(pool.Initialize(c),"bounded real SQL reference pool");
        if(mode=="contracts"||mode.starts_with("crash-"))Check(Execute(pool,"INSERT INTO im_users(user_id,username,nickname,status)VALUES(520001,'native_actor_1','Actor1',1),(520002,'native_actor_2','Actor2',1),(520003,'native_actor_3','Actor3',1),(520004,'native_actor_4','Actor4',1)"),"new users have no legacy message/relation references");
        auto oracle=std::make_shared<MySqlLegacyIdentity>(&pool);LocalPrivateStoreOptions options;
        options.directory=(root/"owner").string();options.storage_identity="native-lifetime-"+cfg.MySql().database;options.first_message_id=7210000000000000ULL;
        options.message_events_topic="codex_perf110_events_20261020_"+cfg.MySql().database.substr(cfg.MySql().database.size()-2);
        options.initial_user_ids={500001,500002};options.exact_user_locking=true;options.block_cache_bytes=8*1024*1024;
        options.identity_policy=oracle->Policy();options.resolve_identity=[oracle](auto f,auto t,const auto&id){return oracle->Resolve(f,t,id);};
        std::unique_ptr<NativeActorReferences> refs;options.before_new_message_commit=[&](auto f,auto t){return refs->EnsureActors(f,t);};
        if(mode=="batch-valid"||mode=="batch-invalid"||mode=="batch-production"){
            const bool production=mode=="batch-production";
            const unsigned caller_count=production?8:32;
            std::string insert="INSERT INTO im_users(user_id,username,nickname,status)VALUES";
            for(unsigned i=0;i<64;++i){if(i)insert+=',';insert+='('+std::to_string(530001+i)+",'batch_actor_"+std::to_string(i)+"','Batch',1)";}
            Check(Execute(pool,insert),"create exact64 new SQL-only batch actors");
            refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
            MySqlConnectionPool lock_pool;auto lock_config=c;lock_config.pool_size=1;
            Check(lock_pool.Initialize(lock_config),"one separate owned SQL lock connection");auto blocker=lock_pool.Acquire();
            Check(blocker&&blocker->Execute("LOCK TABLES im_native_private_actor_refs WRITE"),"hold only the owned reference table before cold burst");
            std::vector<std::future<MessageRepositoryMutationResult>> calls;
            for(unsigned i=0;i<caller_count;++i)calls.push_back(std::async(std::launch::async,[&,i]{
                return refs->EnsureActors(530001+2*i,mode=="batch-invalid"&&i==31?999999999:530002+2*i);
            }));
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            const unsigned expected_available=production?27:0; // owner plus4 independent SQL batches.
            while(pool.AvailableCount()>expected_available&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const bool sql_waiting=pool.AvailableCount()==expected_available;
            const bool released=blocker->Execute("UNLOCK TABLES");blocker.Reset();
            bool outcomes=true;
            for(unsigned i=0;i<caller_count;++i){const bool succeeded=calls[i].get().Succeeded();outcomes=(succeeded==!(mode=="batch-invalid"&&i==31))&&outcomes;}
            // Every future is collected before asserting, even if one failed.
            Check(sql_waiting&&released,"real SQL wait and explicit release observed");
            Check(outcomes,"valid pairs succeed and a missing parent fails independently");
            Check(Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")==std::to_string(mode=="batch-invalid"?62:caller_count*2),"SQL batch has exactly the valid actor rows");
            Check(store.Stats().message_commits==0&&store.Shutdown(),"reference-only qualification creates no business message");refs.reset();
            refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore restored(options);refs->ReconcileBeforeServing(restored);
            Check(Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="0","restart removes only conservative unreferenced batch pins");
            Check(Scalar(pool,"SELECT COUNT(*)FROM im_users WHERE user_id BETWEEN 530001 AND 530064")=="64","all logical parent accounts remain");
            Check(restored.Shutdown(),"batch qualification engine closes");refs.reset();lock_pool.Shutdown();pool.Shutdown();Logger::Instance().Shutdown();
            std::cout<<"NATIVE_REFERENCE_READY_SQL_BATCH_PASS\n";return 0;
        }
        if(mode!="contracts"){
            if(mode!="crash-after-pin"&&mode!="crash-after-local-sync"&&mode!="verify-after-pin"&&mode!="verify-after-local-sync")return 2;
            refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore::CommitObserver observer;
            if(mode.starts_with("crash-"))observer=[&](auto point,auto mid){
                const auto target=mode=="crash-after-pin"?LocalPrivateStore::CommitPoint::kBeforeWrite:LocalPrivateStore::CommitPoint::kAfterSyncBeforeReply;
                if(point==target){std::cout<<"OWNED_LOCAL_REFERENCES_CRASH_READY "<<mode<<' '<<mid<<std::endl;std::raise(SIGSTOP);}
            };
            LocalPrivateStore store(options,observer);refs->ReconcileBeforeServing(store);
            if(mode.starts_with("crash-")){(void)store.PersistPrivateMessage(520001,520002,"reference-crash",1,"owned durable message");return 3;}
            const auto found=store.FindPrivateMessageByClientMessageId(520001,"reference-crash");
            if(mode=="verify-after-pin"){
                Check(found.Succeeded()&&!found.Found()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="0","pin-before-local-commit crash removes only conservative orphan references");
                Check(Execute(pool,"UPDATE im_users SET status=2 WHERE user_id IN(520001,520002)"),"unreferenced accounts use status lifecycle without physical deletion");
            }else{
                Check(found.Found()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="2","accepted local message and both durable references survive death");
                Check(Execute(pool,"UPDATE im_users SET status=2 WHERE user_id IN(520001,520002)")&&Scalar(pool,"SELECT COUNT(*) FROM im_users WHERE user_id IN(520001,520002)")=="2","recovery and account disable retain both parent identities");
                const auto retry=store.PersistPrivateMessage(520001,520002,"reference-crash",1,"owned durable message");Check(retry.outcome==PersistPrivateMessageOutcome::kReused&&retry.message_id==found.record.message_id,"lost durable reply preserves CID and one message");
            }
            Check(store.Shutdown(),"recovered native reference store closes");refs.reset();pool.Shutdown();Logger::Instance().Shutdown();return 0;
        }
        {
            refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
            Check(refs->Healthy()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_owner")=="1","explicit singleton owner registered before serving");
            Check(Reject([&]{NativeActorReferences duplicate(&pool,options);}),"same SQL schema rejects concurrent owner without releasing first owner lock");
            Check(refs->Healthy()&&Scalar(pool,"SELECT IS_USED_LOCK(CONCAT('timx:',SHA2(DATABASE(),224)))")==std::to_string(refs->OwnerSqlSessionId()),"failed duplicate construction leaves original lease intact");
            const auto bad=store.PersistPrivateMessage(520001,999999999,"missing",1,"body");
            Check(!bad.Accepted()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="0","missing actor creates neither references nor local state");
            Check(!store.PersistPrivateMessage(520001,520002,"oversize",1,std::string(65536,'x')).Accepted()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="0","body validation precedes durable actor pinning");
            const auto p=store.PersistPrivateMessage(520001,520002,"lifetime",1,"durable");Check(p.Accepted()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="2","new message pins both current SQL identities before local commit");
            Check(Execute(pool,"UPDATE im_users SET status=2 WHERE user_id=520002")&&Scalar(pool,"SELECT COUNT(*) FROM im_native_private_actor_refs")=="2","status lifecycle retains native references without FK cascade");
            Check(Execute(pool,"UPDATE im_users SET nickname='updated' WHERE user_id=520001"),"normal public profile update remains available");
            Check(Scalar(pool,"SELECT COUNT(*) FROM information_schema.referential_constraints WHERE CONSTRAINT_SCHEMA=DATABASE()")=="0","application identity policy requires no physical foreign keys");
            Check(store.PersistPrivateMessage(520001,520002,"lifetime",1,"durable").outcome==PersistPrivateMessageOutcome::kReused&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="2","CID retry does not duplicate references");
            Check(refs->EnsureActors(520003,520004).Succeeded()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="4","simulate pin committed before an uncommitted native message");
            Check(store.Shutdown(),"first native owner closes before SQL lease release");refs.reset();
        }
        auto wrong=options;wrong.directory=(root/"other-owner").string();
        Check(Reject([&]{NativeActorReferences invalid(&pool,wrong);})&&!std::filesystem::exists(wrong.directory),"different path cannot reinterpret existing native owner metadata");
        const auto saved=root/"saved-owner";std::filesystem::rename(options.directory,saved);
        bool rejected=false;try{rejected=Reject([&]{NativeActorReferences missing(&pool,options);});}catch(...){std::filesystem::rename(saved,options.directory);throw;}
        std::filesystem::rename(saved,options.directory);Check(rejected,"missing registered data rejects startup instead of creating an empty replacement");
        {
            refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
            Check(Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="2"&&store.FindPrivateMessageByClientMessageId(520001,"lifetime").Found(),"startup reconciles exact durable actor index and removes only orphan pins");
            Check(Execute(pool,"UPDATE im_users SET status=2 WHERE user_id IN(520003,520004)"),"reconciled orphan-only accounts retain immutable identity");
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_users WHERE user_id=520002")=="1","live message actor identity remains after reconciliation");
            Check(Execute(pool,"KILL CONNECTION "+std::to_string(refs->OwnerSqlSessionId())),"terminate only this owned reference lease session");
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(refs->Healthy()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(20));
            Check(!refs->Healthy()&&!store.PersistPrivateMessage(520001,520002,"lease-lost",1,"denied").Accepted()&&store.Stats().message_commits==0,"lost SQL owner lease fences new native commits without reconnecting ownership");
            Check(store.Shutdown(),"fenced owner closes physical engine");refs.reset();
        }
        const char* concurrent=std::getenv("TINYIMX_NATIVE_CONCURRENT_ACTOR_PINS_ENABLE");
        if(concurrent&&std::string(concurrent)=="1"){
            {
                refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
                auto blocker=pool.Acquire();MySqlQueryResult row;
                Check(blocker&&blocker->BeginTransaction()&&blocker->Execute("INSERT INTO im_native_private_actor_refs(owner_slot,user_id)VALUES(1,500003)"),"hold exact synthetic actor-registry key in a real transaction");
                auto blocked=std::async(std::launch::async,[&]{return refs->EnsureActors(500003,500004);});
                const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(pool.AvailableCount()!=1&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                const bool inside_sql=pool.AvailableCount()==1;
                auto duplicate=std::async(std::launch::async,[&]{return refs->EnsureActors(500003,500004);});
                const bool duplicate_waits=duplicate.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout;
                auto independent=std::async(std::launch::async,[&]{return refs->EnsureActors(500005,500006);});
                const bool independent_early=independent.wait_for(std::chrono::seconds(2))==std::future_status::ready;
                const bool blocked_waits=blocked.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout;
                // Release the real blocker before asserting, including negative
                // outcomes, so a regression cannot strand async destructors.
                const bool released=blocker->Rollback();blocker.Reset();
                const auto first=blocked.get(),same=duplicate.get(),other=independent.get();
                Check(inside_sql&&blocked_waits,"cold first actor actually waits on held registry write key");
                Check(duplicate_waits&&same.Succeeded(),"overlapping actor reservation waits and resolves to the durable registration");
                Check(independent_early&&other.Succeeded(),"unrelated cold pair commits while first SQL row remains blocked");
                Check(released&&first.Succeeded()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="6","parallel registrations retain exactly one logical reference per actor");
                Check(!refs->EnsureActors(500007,999999999).Succeeded(),"missing logical parent rejects uncommitted actor registration");
                Check(refs->EnsureActors(500007,500008).Succeeded()&&Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="8","failed pin releases its reservation and allows a valid retry");
                Check(store.Shutdown(),"parallel registration owner closes before reconciliation");refs.reset();
            }
            {
                refs=std::make_unique<NativeActorReferences>(&pool,options);LocalPrivateStore store(options);refs->ReconcileBeforeServing(store);
                Check(Scalar(pool,"SELECT COUNT(*)FROM im_native_private_actor_refs")=="2"&&store.FindPrivateMessageByClientMessageId(520001,"lifetime").Found(),"restart removes parallel orphan pins while preserving all durable message actors");
                Check(store.Shutdown(),"reconciled parallel owner closes");refs.reset();
            }
        }
        pool.Shutdown();Logger::Instance().Shutdown();std::cout<<"NATIVE_ACTOR_REFERENCES_SQL_PASS\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
