#include "common/db/LogicalReferences.h"
#include "services/message/local/NativeActorReferences.h"
#include "services/message/local/NativePersistencePhaseTrace.h"
#include "services/message/local/NativeReferenceReadyBatch.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/LogMacros.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
namespace tinyimx::message::local {
namespace {
using Status=MessageApplicationStatus;
void Require(bool good,const char* text){if(!good)throw std::runtime_error(text);}
std::string Hex(const std::string& value){const char*d="0123456789ABCDEF";std::string out;out.reserve(value.size()*2);for(unsigned char c:value){out+=d[c>>4];out+=d[c&15];}return out;}
constexpr const char* kLockName="CONCAT('timx:',SHA2(DATABASE(),224))";
bool ConcurrentPinsEnabled()noexcept{
    static const bool enabled=[](){const char*v=std::getenv("TINYIMX_NATIVE_CONCURRENT_ACTOR_PINS_ENABLE");return v&&v[0]=='1'&&v[1]=='\0';}();
    return enabled;
}
bool ReadyReferenceBatchEnabled()noexcept{
    static const bool enabled=[](){const char*v=std::getenv("TINYIMX_NATIVE_REFERENCE_READY_BATCH_ENABLE");return v&&v[0]=='1'&&v[1]=='\0';}();
    return enabled;
}
bool GuardedReferenceInsertEnabled()noexcept{
    static const bool enabled=[](){const char*v=std::getenv("TINYIMX_NATIVE_GUARDED_REFERENCE_INSERT_ENABLE");return v&&v[0]=='1'&&v[1]=='\0';}();
    return enabled;
}
}
class NativeActorReferences::Impl {
public:
    MySqlConnectionPool* pool;MySqlConnectionLease owner;
    LocalPrivateStoreOptions options;
    std::string fingerprint;bool registered{false},ready{false};
    std::atomic<bool> healthy{true};std::uint64_t session{0};
    std::mutex actor_mutex,monitor_mutex;std::condition_variable monitor_cv,actor_cv;bool stopping{false};std::thread monitor;
    std::set<std::uint64_t> pinned,pinning;
    std::atomic<std::uint64_t> guarded_insert_attempts{0},guarded_insert_fast_successes{0},guarded_insert_readbacks{0};
    std::unique_ptr<NativeReferenceReadyBatch> reference_batch;
    Impl(MySqlConnectionPool*p,const LocalPrivateStoreOptions&o):pool(p),options(o){
        try{
        Require(pool&&options.resolve_identity&&options.identity_policy=="mysql-8.0.40-unicode-ci-padspace-v1","native references require fixed SQL identity authority");
        Require(!ReadyReferenceBatchEnabled()||ConcurrentPinsEnabled(),"ready reference batching requires concurrent actor reservations");
        if(ReadyReferenceBatchEnabled())reference_batch=std::make_unique<NativeReferenceReadyBatch>([this](const auto& ids){return PinRows(ids);},NativeReferenceReadyBatch::ParallelSqlBatches(pool->Size()));
        std::ifstream f("/etc/machine-id");std::string machine;std::getline(f,machine);
        Require(machine.size()==32&&std::all_of(machine.begin(),machine.end(),[](unsigned char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');}),"native owner requires stable local machine identity");
        const std::filesystem::path path(options.directory);
        Require(path.is_absolute()&&path.lexically_normal()==path&&std::filesystem::weakly_canonical(path.parent_path())==path.parent_path()&&options.directory.size()<=1024,"native owner requires canonical local path");
        auto users=options.initial_user_ids;std::sort(users.begin(),users.end());
        const auto metadata=nlohmann::json{{"format",1},{"store_format",3},{"machine_id",machine},{"directory",options.directory},
            {"storage_identity",options.storage_identity},{"topic",options.message_events_topic},{"first_mid",options.first_message_id},{"identity_policy",options.identity_policy},{"initial_user_ids",users}}.dump();
        Require(metadata.size()<=4*1024*1024,"native owner metadata exceeds bound");fingerprint="UNHEX(SHA2(0x"+Hex(metadata)+",256))";
        owner=pool->Acquire();Require(static_cast<bool>(owner),"native owner SQL lease unavailable");
        MySqlQueryResult rows;
        Require(owner->Query(std::string("SELECT CONNECTION_ID(),GET_LOCK(")+kLockName+",0)",&rows)&&rows.rows.size()==1&&rows.rows[0].size()==2&&rows.rows[0][1]=="1","another native Message owner is active");
        session=std::stoull(rows.rows[0][0]);
        Require(owner->Query("SELECT @@global.innodb_flush_log_at_trx_commit,@@global.sync_binlog,@@global.binlog_row_image",&rows)&&rows.rows==std::vector<std::vector<std::string>>{{"1","1","FULL"}},"native user references require strong SQL durability");
        Require(owner->Query("SELECT TABLE_NAME,ENGINE FROM information_schema.tables WHERE TABLE_SCHEMA=DATABASE()AND TABLE_NAME IN('im_native_private_owner','im_native_private_actor_refs')ORDER BY TABLE_NAME",&rows)&&
            rows.rows==std::vector<std::vector<std::string>>{{"im_native_private_actor_refs","InnoDB"},{"im_native_private_owner","InnoDB"}},"explicit native ownership schema is not installed");
        Require(owner->Query("SELECT COUNT(*) FROM information_schema.referential_constraints WHERE CONSTRAINT_SCHEMA=DATABASE() AND TABLE_NAME IN('im_native_private_owner','im_native_private_actor_refs')",&rows)&&
            rows.rows==std::vector<std::vector<std::string>>{{"0"}},"native ownership requires the explicit FK-free migration");
        Require(owner->Query("SELECT COUNT(*)FROM im_private_messages",&rows)&&rows.rows==std::vector<std::vector<std::string>>{{"0"}},"SQL private history needs explicit migration before native ownership");
        Require(owner->Query("SELECT owner_slot,format_version,owner_fingerprint="+fingerprint+" FROM im_native_private_owner",&rows)&&rows.rows.size()<=1,"native owner metadata read failed");
        if(!rows.rows.empty()){
            Require(rows.rows==std::vector<std::vector<std::string>>{{"1","1","1"}},"native owner machine/path/configuration mismatch");registered=true;
            Require(std::filesystem::is_regular_file(path/".tinyimx-local-store.json")&&std::filesystem::is_regular_file(path/"CURRENT"),"registered native data missing; never create an empty replacement");
        }
        }catch(...){if(owner)owner->Close();throw;}
    }
    ~Impl(){
        if(GuardedReferenceInsertEnabled())LOG_INFO("native guarded reference inserts, attempts="<<guarded_insert_attempts.load()
            <<", fast_successes="<<guarded_insert_fast_successes.load()<<", readbacks="<<guarded_insert_readbacks.load());
        if(reference_batch){const auto s=reference_batch->Snapshot();LOG_WARN("native reference ready batch stopped, calls="<<s.calls<<", succeeded="<<s.succeeded<<", batches="<<s.batches<<", sql_calls="<<s.sql_calls<<", fallback_calls="<<s.fallback_calls<<", rejected="<<s.rejected<<", largest_batch="<<s.largest_batch<<", queued="<<s.queued<<", pending="<<s.pending<<", active_batches="<<s.active_batches<<", parallel_batches="<<reference_batch->MaxParallelSqlBatches());}
        {std::lock_guard lock(monitor_mutex);stopping=true;}monitor_cv.notify_all();if(monitor.joinable())monitor.join();
        if(owner){MySqlQueryResult ignored;(void)owner->Query(std::string("SELECT RELEASE_LOCK(")+kLockName+')',&ignored);owner.Reset();}
    }
    void Monitor(){
        std::unique_lock lock(monitor_mutex);
        while(!monitor_cv.wait_for(lock,std::chrono::milliseconds(200),[&]{return stopping;})){
            lock.unlock();MySqlQueryResult result;
            if(!owner->Query(std::string("SELECT IS_USED_LOCK(")+kLockName+")=CONNECTION_ID()",&result)||result.rows!=std::vector<std::vector<std::string>>{{"1"}}){
                if(ConcurrentPinsEnabled()){{std::lock_guard state(actor_mutex);healthy=false;}actor_cv.notify_all();}
                else healthy=false;
                return;
            }
            lock.lock();
        }
    }
    // SQL runs without touching either cache. A caller reserves the exact fresh
    // actors under actor_mutex before releasing it, then publishes only success.
    NativeReferenceReadyBatch::PinResult GuardedPinRows(MySqlConnection* connection,const std::set<std::uint64_t>& ids){
        using PinResult=NativeReferenceReadyBatch::PinResult;
        if(!connection||ids.empty()||ids.size()>256||ids.contains(0))return PinResult::Unavailable();
        std::string actors,incoming;
        for(auto id:ids){
            if(!actors.empty()){actors+=',';incoming+=" UNION ALL ";}
            actors+=std::to_string(id);incoming+="SELECT "+std::to_string(id)+" AS user_id";
        }
        // One statement checks the whole set before inserting any row. An
        // invalid pair must not leave a partial pin. These are unsigned server
        // IDs, never caller SQL; retained-parent lifecycle remains mandatory.
        const std::string sql="INSERT INTO im_native_private_actor_refs(owner_slot,user_id)"
            " SELECT 1,candidate.user_id FROM ("+incoming+") AS candidate"
            " WHERE EXISTS(SELECT 1 FROM im_native_private_owner WHERE owner_slot=1)"
            " AND (SELECT COUNT(*) FROM im_users WHERE user_id IN("+actors+"))="+std::to_string(ids.size())+
            " ON DUPLICATE KEY UPDATE user_id=VALUES(user_id)";
        ++guarded_insert_attempts;
        // An uncertain wire reply is never replayed or promoted into success.
        // Conservative orphan pins are reconciled before serving after restart.
        if(!connection->Execute(sql))return PinResult::Unavailable();
        const auto affected=connection->AffectedRows();
        if(affected==ids.size()){++guarded_insert_fast_successes;return true;}
        if(affected>ids.size())return PinResult::Unavailable();
        ++guarded_insert_readbacks;
        // Existing durable pins produce fewer affected rows, as does a failed
        // existence condition. Distinguish them with read-only exact checks.
        // Never execute the INSERT a second time on this path.
        std::vector<db::LogicalReference> references{db::LogicalReference::NativeOwner()};
        for(auto id:ids)references.push_back(db::LogicalReference::User(id));
        const auto authority=db::CheckReferences(connection,std::move(references));
        if(authority==db::LogicalReferenceStatus::Missing)return false;
        if(authority!=db::LogicalReferenceStatus::Exists)return PinResult::Unavailable();
        MySqlQueryResult result;
        if(!connection->Query("SELECT COUNT(*) FROM im_native_private_actor_refs WHERE owner_slot=1 AND user_id IN("+actors+')',&result))
            return PinResult::Unavailable();
        return result.rows==std::vector<std::vector<std::string>>{{std::to_string(ids.size())}}
            ?PinResult(true):PinResult::Unavailable();
    }
    NativeReferenceReadyBatch::PinResult PinRows(const std::set<std::uint64_t>& ids){
        using PinResult=NativeReferenceReadyBatch::PinResult;
        std::string sql="INSERT INTO im_native_private_actor_refs(owner_slot,user_id)VALUES";bool any=false;
        for(auto id:ids){if(any)sql+=',';sql+="(1,"+std::to_string(id)+')';any=true;}
        if(!any)return true;
        sql+=" ON DUPLICATE KEY UPDATE user_id=VALUES(user_id)";
        auto lease=NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::ReferenceAcquirePing,[&]{return pool->Acquire();});
        if(!lease)return PinResult::Unavailable();
        if(GuardedReferenceInsertEnabled())
            return NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::ReferenceSql,[&]{
                return GuardedPinRows(lease.operator->(),ids);
            });
        std::vector<db::LogicalReference> references{db::LogicalReference::NativeOwner()};
        for(auto id:ids)references.push_back(db::LogicalReference::User(id));
        return NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::ReferenceSql,[&]() -> PinResult {
            const auto authority=db::CheckReferences(lease.operator->(),std::move(references));
            if(authority==db::LogicalReferenceStatus::Missing)return false;
            if(authority!=db::LogicalReferenceStatus::Exists)return PinResult::Unavailable();
            return lease->Execute(sql)?PinResult(true):PinResult::Unavailable();
        });
    }
    bool Pin(const std::set<std::uint64_t>& ids){
        std::set<std::uint64_t> fresh;for(auto id:ids)if(!pinned.contains(id))fresh.insert(id);
        if(!PinRows(fresh))return false;
        pinned.insert(ids.begin(),ids.end());return true;
    }
    MessageRepositoryMutationResult EnsureConcurrent(std::uint64_t from,std::uint64_t to){
        MessageRepositoryMutationResult out;out.status=Status::kStorageError;
        const std::set<std::uint64_t> ids{from,to};
        const auto wait=NativePersistencePhaseTrace::StartCurrent();std::unique_lock lock(actor_mutex);
        NativePersistencePhaseTrace::RecordCurrent(NativePersistencePhaseTrace::ReferenceMutex,wait);
        std::set<std::uint64_t> fresh;
        for(;;){
            if(!ready||!healthy.load()||!from||!to||from==to){out.message="native user reference owner unavailable";return out;}
            fresh.clear();for(auto id:ids)if(!pinned.contains(id))fresh.insert(id);
            NativePersistencePhaseTrace::FreshActors(static_cast<unsigned>(fresh.size()));
            if(fresh.empty()){out.status=Status::kSucceeded;return out;}
            if(std::none_of(fresh.begin(),fresh.end(),[&](auto id){return pinning.contains(id);}))break;
            // Only overlapping actors wait for their own durable registration.
            // Different cold pairs can use the existing bounded SQL pool.
            NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::ReferenceInFlightWait,[&]{
                actor_cv.wait(lock,[&]{return !healthy.load()||std::none_of(fresh.begin(),fresh.end(),[&](auto id){return pinning.contains(id);});});
            });
        }
        if(pinned.size()+pinning.size()+fresh.size()>100000){out.message="native user reference pin capacity exceeded";return out;}
        struct Reservation{
            Impl& owner;std::unique_lock<std::mutex>& lock;const std::set<std::uint64_t>& ids;bool active{false};
            ~Reservation(){if(!active)return;if(!lock.owns_lock())lock.lock();for(auto id:ids)owner.pinning.erase(id);lock.unlock();owner.actor_cv.notify_all();}
        } reservation{*this,lock,fresh};
        reservation.active=true; // Covers allocation/SQL errors and uncertain replies.
        pinning.insert(fresh.begin(),fresh.end());lock.unlock();
        const bool stored=reference_batch?reference_batch->Run(fresh):static_cast<bool>(PinRows(fresh));
        const auto relock=NativePersistencePhaseTrace::StartCurrent();lock.lock();
        NativePersistencePhaseTrace::RecordCurrent(NativePersistencePhaseTrace::ReferenceMutex,relock);
        if(!stored){out.message="native user reference pin failed";return out;}
        pinned.insert(fresh.begin(),fresh.end());
        if(!healthy.load()){out.message="native user reference owner unavailable";return out;}
        out.status=Status::kSucceeded;return out;
    }
};
NativeActorReferences::NativeActorReferences(MySqlConnectionPool*p,const LocalPrivateStoreOptions&o):impl_(std::make_unique<Impl>(p,o)){}
NativeActorReferences::~NativeActorReferences()=default;
void NativeActorReferences::ReconcileBeforeServing(LocalPrivateStore& store){
    auto&s=*impl_;std::lock_guard hold(s.actor_mutex);Require(!s.ready,"native reference owner already reconciled");
    const auto actors=store.ListMessageActors();Require(actors.Succeeded(),"native actor index read failed");
    if(!s.registered){
        const auto failed=store.ListFailedEvents(1);
        Require(!actors.has_messages&&store.Stats().pending_events==0&&failed.Succeeded()&&failed.events.empty(),"unregistered native history requires explicit ownership migration");
        Require(s.owner->Execute("INSERT INTO im_native_private_owner(owner_slot,format_version,owner_fingerprint)VALUES(1,1,"+s.fingerprint+')'),"native owner registration failed");s.registered=true;
    }
    // Pins precede new local commits. A crash before the local commit can leave
    // conservative orphan pins; remove only those absent from this sole owner's
    // durable actor index, before any RPC or publication is admitted.
    for(std::size_t i=0;i<actors.actors.size();i+=256){
        const auto end=std::min(i+256,actors.actors.size());
        Require(s.Pin(std::set<std::uint64_t>(actors.actors.begin()+i,actors.actors.begin()+end)),"native historical actor reference cannot be restored");
    }
    MySqlQueryResult rows;Require(s.owner->Query("SELECT user_id FROM im_native_private_actor_refs WHERE owner_slot=1 ORDER BY user_id LIMIT 100001",&rows)&&rows.rows.size()<=100000,"native reference registry exceeds bound");
    std::vector<std::uint64_t> orphan;
    for(const auto& row:rows.rows){Require(row.size()==1,"invalid native reference row");const auto id=std::stoull(row[0]);if(!s.pinned.contains(id))orphan.push_back(id);}
    for(std::size_t i=0;i<orphan.size();i+=256){std::string sql="DELETE FROM im_native_private_actor_refs WHERE owner_slot=1 AND user_id IN(";
        for(std::size_t k=i;k<std::min(i+256,orphan.size());++k){if(k!=i)sql+=',';sql+=std::to_string(orphan[k]);}sql+=')';Require(s.owner->Execute(sql),"native orphan-reference reconciliation failed");}
    s.monitor=std::thread([&s]{s.Monitor();});s.ready=true;
}
MessageRepositoryMutationResult NativeActorReferences::EnsureActors(std::uint64_t from,std::uint64_t to){
    MessageRepositoryMutationResult out;out.status=Status::kStorageError;auto&s=*impl_;
    if(ConcurrentPinsEnabled())return s.EnsureConcurrent(from,to);
    const auto wait=NativePersistencePhaseTrace::StartCurrent();std::lock_guard hold(s.actor_mutex);
    NativePersistencePhaseTrace::RecordCurrent(NativePersistencePhaseTrace::ReferenceMutex,wait);
    if(!s.ready||!s.healthy.load()||!from||!to||from==to){out.message="native user reference owner unavailable";return out;}
    const std::set<std::uint64_t> ids{from,to};std::size_t fresh=0;for(auto id:ids)fresh+=!s.pinned.contains(id);
    NativePersistencePhaseTrace::FreshActors(static_cast<unsigned>(fresh));
    if(s.pinned.size()+fresh>100000||!s.Pin(ids)){out.message="native user reference pin failed";return out;}
    out.status=Status::kSucceeded;return out;
}
bool NativeActorReferences::Healthy()const noexcept{return impl_->ready&&impl_->healthy.load();}
std::uint64_t NativeActorReferences::OwnerSqlSessionId()const noexcept{return impl_->session;}
NativeActorReferences::ReferenceSqlStats NativeActorReferences::SqlStats()const noexcept{
    return {impl_->guarded_insert_attempts.load(),impl_->guarded_insert_fast_successes.load(),impl_->guarded_insert_readbacks.load()};
}
}
