#include "services/message/local/LocalPrivateStore.h"
#include "services/message/local/NativePersistencePhaseTrace.h"
#include "services/message/local/NativeRocksWriteTrace.h"
#include "services/message/local/NativeSstWritebackPolicy.h"
#include "services/message/local/NativePersistUnreadReplyPolicy.h"
#include "services/message/local/NativePublicationPhaseTrace.h"
#include "services/message/application/MessageEventFactory.h"
#include "services/message/application/ReceiverConfirmationDecision.h"
#include "services/eventing/EventCodec.h"
#include "common/runtime/ReadyPhysicalWriteGroup.h"
#include <rocksdb/cache.h>
#include <rocksdb/db.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/statistics.h>
#include <rocksdb/table.h>
#include <rocksdb/write_batch.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace tinyimx::message::local {
namespace {
using Json=nlohmann::json;
using Status=MessageApplicationStatus;
bool EmptyEventScanGuardEnabled() noexcept {
    // Experimental ablation; default remains OFF until full-stack A/B passes.
    static const bool enabled=[] {
        const char* value=std::getenv("TINYIMX_NATIVE_EMPTY_EVENT_SCAN_GUARD_ENABLE");
        return value&&value[0]=='1'&&value[1]=='\0';
    }();
    return enabled;
}
bool PendingEventIndexEnabled() noexcept {
    // Independent opt-in ablation. E/ and the WAL remain authoritative.
    static const bool enabled=[] {
        const char* value=std::getenv("TINYIMX_NATIVE_PENDING_EVENT_INDEX_ENABLE");
        return value&&value[0]=='1'&&value[1]=='\0';
    }();
    return enabled;
}
bool ReadEventProgressShardsFlag() noexcept {
    // Keep the frozen global-lock path until independent full-stack A/B passes.
    static const bool enabled=[] {
        const char* value=std::getenv("TINYIMX_NATIVE_EVENT_PROGRESS_SHARDS_ENABLE");
        return value&&value[0]=='1'&&value[1]=='\0';
    }();
    return enabled;
}
struct Failure : std::runtime_error {
    Status status;
    Failure(Status s,const std::string& m):std::runtime_error(m),status(s){}
};
void Require(bool value,Status status,const char* text) { if(!value)throw Failure(status,text); }
std::string Hex(std::uint64_t n) {
    constexpr char digits[]="0123456789abcdef";
    std::string out(16,'0');for(int i=15;i>=0;--i){out[i]=digits[n&15];n>>=4;}return out;
}
std::uint64_t Unhex(const std::string& s) {
    Require(s.size()==16,Status::kInvalidRecord,"invalid local numeric key");
    std::uint64_t n=0;for(char c:s){Require((c>='0'&&c<='9')||(c>='a'&&c<='f'),Status::kInvalidRecord,"invalid local numeric key");n=(n<<4)|(c<='9'?c-'0':c-'a'+10);}return n;
}
std::string Number(std::uint64_t n) {
    std::string out(8,'\0');for(int i=7;i>=0;--i){out[i]=static_cast<char>(n&255);n>>=8;}return out;
}
std::uint64_t ParseNumber(const std::string& s) {
    Require(s.size()==8,Status::kInvalidRecord,"invalid local numeric value");
    std::uint64_t n=0;for(unsigned char c:s)n=(n<<8)|c;return n;
}
std::string MessageKey(std::uint64_t mid){return "M/"+Hex(mid);}
std::string CidKey(std::uint64_t from,const std::string& cid){return "I/"+Hex(from)+'/'+cid;}
std::string DialogPrefix(std::uint64_t a,std::uint64_t b){return "H/"+Hex(std::min(a,b))+'/'+Hex(std::max(a,b))+'/';}
std::string DialogUnreadKey(std::uint64_t receiver,std::uint64_t peer){return "D/"+Hex(receiver)+'/'+Hex(peer);}
std::string PendingPrefix(std::uint64_t receiver){return "P/"+Hex(receiver)+'/';}
std::string Now() {
    const auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};gmtime_r(&t,&utc);char text[32]{};
    Require(std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M:%S",&utc)>0,Status::kStorageError,"local timestamp failed");
    return text;
}
Json EncodeRecord(const MessageView& m) {
    return Json{{"message_id",m.message_id},{"client_message_id",m.client_message_id},
        {"from",m.from_user_id},{"to",m.to_user_id},{"type",m.message_type},{"content",m.content},
        {"state",static_cast<unsigned>(m.delivery_state)},{"created_at",m.created_at},
        {"confirmed_at",m.receiver_confirmed_at},{"read_at",m.read_at}};
}
MessageView DecodeRecord(const std::string& text,std::uint64_t expected) {
    const auto j=Json::parse(text);MessageView m;
    m.message_id=j.at("message_id");m.client_message_id=j.at("client_message_id");
    m.from_user_id=j.at("from");m.to_user_id=j.at("to");m.message_type=j.at("type");m.content=j.at("content");
    const unsigned state=j.at("state");m.delivery_state=static_cast<MessageDeliveryState>(state);
    m.created_at=j.at("created_at");m.receiver_confirmed_at=j.at("confirmed_at");m.read_at=j.at("read_at");
    Require(m.message_id==expected&&expected&&m.from_user_id&&m.to_user_id&&m.from_user_id!=m.to_user_id&&
        !m.client_message_id.empty()&&m.client_message_id.size()<=64&&m.message_type>=1&&m.message_type<=3&&
        !m.content.empty()&&state<=3&&m.created_at.size()==19,Status::kInvalidRecord,"invalid local message record");
    return m;
}
struct Snapshot {
    rocksdb::DB* db;rocksdb::ReadOptions read;
    explicit Snapshot(rocksdb::DB* d):db(d){read.snapshot=db->GetSnapshot();read.verify_checksums=true;}
    ~Snapshot(){db->ReleaseSnapshot(read.snapshot);}
};
PendingLocalEvent DecodeEvent(const std::string& text,const std::string& expected) {
    const auto j=Json::parse(text);PendingLocalEvent e;
    e.event_id=j.at("event_id");e.topic=j.at("topic");e.tag=j.at("tag");e.message_key=j.at("key");e.body=j.at("body");
    const auto decoded=eventing::EventCodec::Decode(e.body);
    Require(decoded.success&&decoded.event.event_id==e.event_id&&e.event_id==expected&&MessageEventFactory::IsValidTopic(e.topic)&&e.tag==decoded.event.event_type&&e.message_key==e.event_id,
        Status::kInvalidRecord,"invalid local event envelope");return e;
}
void EventId(const std::string& id){Require(!id.empty()&&id.size()<=191&&id.find('\0')==std::string::npos,Status::kInvalidArgument,"invalid local event identity");}
// A failed write has uncertain physical outcome. No operation replays it, and
// no further accepted result is issued from that owner until reopen/recovery.
} // namespace

class LocalPrivateStore::Impl {
public:
    struct BorrowedWrite {rocksdb::WriteBatch* batch;bool sync;};
    std::unique_ptr<tinyimx::ReadyPhysicalWriteGroup<BorrowedWrite>> ready_writes;
    std::atomic<std::uint64_t> engine_write_calls{0};
    LocalPrivateStoreOptions options;
    CommitObserver observer;
    std::unique_ptr<rocksdb::DB> db;
    std::shared_ptr<rocksdb::Statistics> statistics=rocksdb::CreateDBStatistics();
    mutable std::shared_mutex lifetime;
    std::array<std::mutex,64> stripes;
    std::mutex id_mutex,event_ack_mutex;
    std::mutex publication_sync_mutex;
    std::uint64_t synced_publication_generation{0};
    bool publication_sync_initialized{false};
    std::array<std::mutex,64> event_state_mutexes;
    const bool event_progress_shards_enabled{ReadEventProgressShardsFlag()};
    std::unordered_set<std::uint64_t> users;
    std::unordered_map<std::uint64_t,std::unique_ptr<std::mutex>> user_locks;
    std::mutex user_locks_mutex;
    std::atomic<bool> closing{false},poisoned{false};
    std::atomic<bool> relay_owned{false};
    std::atomic<std::uint64_t> pending_events{0},messages{0},confirmations{0},reads{0},reservations{0},publications{0};
    std::atomic<std::uint64_t> message_batches{0};
    std::atomic<std::uint64_t> event_scans_started{0},empty_event_scans_skipped{0};
    struct PendingIndexEntry {
        std::atomic<bool> ready;
        explicit PendingIndexEntry(bool committed=false):ready(committed){}
    };
    // Keys only, bounded by the existing event admission budget. A prepared
    // entry is allocated before its write and becomes visible without allocation
    // only after that write succeeds. No index lock spans an engine operation.
    bool pending_index_enabled{PendingEventIndexEnabled()};
    std::mutex pending_index_mutex;
    std::map<std::string,std::shared_ptr<PendingIndexEntry>,std::less<>> pending_index;
    std::atomic<std::uint64_t> pending_index_queries{0};
    std::atomic<std::uint64_t> event_decode_attempts{0}, held_events_skipped{0};
    std::uint64_t next_id{0},reserved_end{0};

    explicit Impl(LocalPrivateStoreOptions opt,CommitObserver hook):options(std::move(opt)),observer(std::move(hook)) {
        const char* physical_group=std::getenv("TINYIMX_NATIVE_READY_PHYSICAL_WRITE_GROUP_ENABLE");
        if(physical_group&&physical_group[0]=='1'&&physical_group[1]=='\0')
            ready_writes=std::make_unique<tinyimx::ReadyPhysicalWriteGroup<BorrowedWrite>>();
        if(!options.event_signal)options.event_signal=std::make_shared<LocalEventSignal>();
        Require(!options.storage_identity.empty()&&options.storage_identity.size()<=256&&
            MessageEventFactory::IsValidTopic(options.message_events_topic)&&options.first_message_id>0&&
            options.first_message_id<std::numeric_limits<std::int64_t>::max()-4096&&
            options.block_cache_bytes>=1024*1024&&options.block_cache_bytes<=1024ULL*1024*1024&&
            options.max_pending_events>0&&options.max_pending_events<=1000000&&
            !options.initial_user_ids.empty()&&options.initial_user_ids.size()<=100000,
            Status::kInvalidArgument,"invalid local store configuration");
        Require(options.identity_policy.size()<=256&&
            (options.resolve_identity?options.identity_policy=="mysql-8.0.40-unicode-ci-padspace-v1":options.identity_policy=="fixture-raw-bytes-v1"),
            Status::kInvalidArgument,"explicit identity policy/resolver mismatch");
        std::sort(options.initial_user_ids.begin(),options.initial_user_ids.end());
        Require(options.initial_user_ids.front()>0&&std::adjacent_find(options.initial_user_ids.begin(),options.initial_user_ids.end())==options.initial_user_ids.end(),Status::kInvalidArgument,"invalid local identity registry");
        users.insert(options.initial_user_ids.begin(),options.initial_user_ids.end());
        if(options.exact_user_locking)
            for(auto id:options.initial_user_ids)user_locks.emplace(id,std::make_unique<std::mutex>());
        const std::filesystem::path path(options.directory);
        Require(path.is_absolute()&&path.lexically_normal()==path&&std::filesystem::weakly_canonical(path.parent_path())==path.parent_path(),Status::kInvalidArgument,"local store requires canonical absolute directory");
        const Json identity{{"format",3},{"storage_identity",options.storage_identity},{"topic",options.message_events_topic},
            {"first_message_id",options.first_message_id},{"initial_user_ids",options.initial_user_ids},
            {"identity_policy",options.identity_policy},{"content_bytes",65535}};
        const auto marker=path/".tinyimx-local-store.json";
        if(!std::filesystem::exists(path)) {
            Require(std::filesystem::create_directory(path),Status::kStorageError,"local directory creation failed");
            Require(::chmod(path.c_str(),0700)==0,Status::kStorageError,"local directory protection failed");
            const auto text=identity.dump();int fd=::open(marker.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
            Require(fd>=0,Status::kStorageError,"local owner marker creation failed");
            std::size_t pos=0;bool good=true;
            while(pos<text.size()){const auto n=::write(fd,text.data()+pos,text.size()-pos);if(n<=0){good=false;break;}pos+=n;}
            good=good&&::fsync(fd)==0;::close(fd);
            int dir=::open(path.c_str(),O_RDONLY|O_DIRECTORY);good=good&&dir>=0&&::fsync(dir)==0;if(dir>=0)::close(dir);
            int parent=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);good=good&&parent>=0&&::fsync(parent)==0;if(parent>=0)::close(parent);
            Require(good,Status::kStorageError,"local owner marker sync failed");
        } else {
            struct stat st{};
            Require(std::filesystem::is_directory(path)&&!std::filesystem::is_symlink(path)&&::lstat(marker.c_str(),&st)==0&&S_ISREG(st.st_mode)&&st.st_uid==::getuid()&&st.st_nlink==1&&st.st_size<4*1024*1024,Status::kFailedPrecondition,"unowned local store directory");
            std::ifstream f(marker);Json actual;f>>actual;Require(actual==identity,Status::kFailedPrecondition,"local store identity/format mismatch");
        }
        rocksdb::Options config;config.create_if_missing=true;config.paranoid_checks=true;
        config.max_background_jobs=2;config.max_open_files=128;config.write_buffer_size=16*1024*1024;config.max_write_buffer_number=2;
        config.compression=rocksdb::kNoCompression;config.enable_pipelined_write=true;
        ApplyNativeSstWritebackPolicy(config);
        // Experimental CPU/scheduler ablation. Keep the WAL, sync writes and
        // pipelined commit boundaries intact; only stop actively yielding while
        // another writer owns the engine. OFF preserves the current policy.
        const char* blocking_wait=std::getenv("TINYIMX_NATIVE_BLOCKING_WRITE_WAIT_ENABLE");
        if(blocking_wait&&blocking_wait[0]=='1'&&blocking_wait[1]=='\0') {
            config.enable_write_thread_adaptive_yield=false;
            config.write_thread_max_yield_usec=0;
        }
        config.wal_recovery_mode=rocksdb::WALRecoveryMode::kTolerateCorruptedTailRecords;
        config.statistics=statistics;rocksdb::BlockBasedTableOptions table;
        table.block_cache=rocksdb::NewLRUCache(options.block_cache_bytes);table.cache_index_and_filter_blocks=true;
        table.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10));config.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table));
        Check(rocksdb::DB::Open(config,options.directory,&db));
        std::string high;
        if(!Get("meta/high",high)) {
            rocksdb::WriteBatch init;init.Put("meta/high",Number(options.first_message_id-1));Write(init,true);
            high=Number(options.first_message_id-1);
        }
        reserved_end=ParseNumber(high);
        Require(reserved_end>=options.first_message_id-1&&reserved_end<std::numeric_limits<std::int64_t>::max()-4096,Status::kInvalidRecord,"invalid local MID high-water mark");
        next_id=reserved_end+1;
        Snapshot snapshot(db.get());std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(snapshot.read));
        for(it->Seek("E/");it->Valid()&&it->key().starts_with("E/");it->Next()){
            ++pending_events;
            if(pending_index_enabled){
                // A lower admission budget must not prevent an old backlog
                // from draining. Fall back to the original scan, without
                // allocating beyond the configured index bound.
                if(pending_events.load()>options.max_pending_events){
                    pending_index_enabled=false;pending_index.clear();
                }else pending_index.emplace(it->key().ToString().substr(2),std::make_shared<PendingIndexEntry>(true));
            }
        }
        Check(it->status());
        // Recovering a valid tail must not let an uncertain result be accepted
        // from an un-synced recovery buffer. Fail closed if recovery sync fails.
        Check(db->FlushWAL(true));
    }
    void Check(const rocksdb::Status& s) const {if(!s.ok())throw Failure(Status::kStorageError,"local engine: "+s.ToString());}
    void Open() const {Require(db&&!closing.load()&&!poisoned.load(),Status::kStorageError,"local store closed or requires recovery");}
    void Actor(std::uint64_t id) const {
        Require(id>0,Status::kInvalidArgument,"zero local actor");
        if(!options.resolve_identity)Require(users.contains(id),Status::kPermissionDenied,"actor absent from explicit local identity registry");
    }
    void Dialog(std::uint64_t a,std::uint64_t b) const {Actor(a);Actor(b);Require(a!=b,Status::kInvalidArgument,"self dialog is invalid");}
    void Limit(std::size_t n) const {Require(n>0&&n<=1001,Status::kInvalidArgument,"invalid local query limit");}
    template<class Out,class Fn>Out Call(Fn&& fn) {
        auto hold=NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::LifetimeLock,[&]{return std::shared_lock(lifetime);});Out out;
        try {Open();out.status=Status::kSucceeded;fn(out);}
        catch(const Failure& e){out={};out.status=e.status;out.message=e.what();}
        catch(const Json::exception&){out={};out.status=Status::kInvalidRecord;out.message="invalid local serialized record";}
        catch(const std::exception& e){out={};out.status=Status::kStorageError;out.message=e.what();}
        return out;
    }
    std::vector<std::unique_lock<std::mutex>> Lock(std::initializer_list<std::string> keys) {
        std::set<std::size_t> slots;for(const auto& key:keys)slots.insert(std::hash<std::string>{}(key)%stripes.size());
        std::vector<std::unique_lock<std::mutex>> locks;for(auto slot:slots)locks.emplace_back(stripes[slot]);return locks;
    }
    std::vector<std::unique_lock<std::mutex>> LockActors(std::initializer_list<std::uint64_t> actors) {
        // Sender locking is also the CID arbiter across different recipients.
        // Both actors protect a bidirectional conversation's latest MID, while
        // the recipient lock protects total/dialog/pending counters and reads.
        return LockActors(std::set<std::uint64_t>(actors));
    }
    std::vector<std::unique_lock<std::mutex>> LockActors(const std::set<std::uint64_t>& ordered) {
        std::vector<std::mutex*> owned;
        {
            std::lock_guard hold(user_locks_mutex);
            for(auto actor:ordered) {
                Require(actor>0,Status::kInvalidArgument,"zero local lock actor");
                auto found=user_locks.find(actor);
                if(found==user_locks.end()) {
                    Require(user_locks.size()<100000,Status::kFailedPrecondition,"bounded local actor lock capacity reached");
                    found=user_locks.emplace(actor,std::make_unique<std::mutex>()).first;
                }
                owned.push_back(found->second.get());
            }
        }
        std::vector<std::unique_lock<std::mutex>> locks;
        for(auto mutex:owned)locks.emplace_back(*mutex);return locks;
    }
    std::vector<std::unique_lock<std::mutex>> LockReceivers(const std::set<std::uint64_t>& actors) {
        if(options.exact_user_locking)return LockActors(actors);
        std::set<std::size_t> slots;
        for(auto actor:actors)slots.insert(std::hash<std::string>{}("receiver/"+Hex(actor))%stripes.size());
        std::vector<std::unique_lock<std::mutex>> locks;
        for(auto slot:slots)locks.emplace_back(stripes[slot]);return locks;
    }
    template<class Range>std::vector<std::unique_lock<std::mutex>> LockEventState(const Range& ids) {
        std::vector<std::unique_lock<std::mutex>> locks;
        if(!event_progress_shards_enabled){locks.reserve(1);locks.emplace_back(event_ack_mutex);return locks;}
        std::array<bool,64> needed{};std::size_t count=0;
        for(const auto& id:ids){
            const auto slot=std::hash<std::string_view>{}(std::string_view(id))%needed.size();
            if(!needed[slot]){needed[slot]=true;++count;}
        }
        locks.reserve(count);
        // Every multi-event operation takes the same ascending stripe order.
        // Overlapping IDs serialize across publish/fail/requeue; unrelated IDs
        // need not hold a global mutex while waiting in the engine write queue.
        for(std::size_t slot=0;slot<needed.size();++slot)if(needed[slot])locks.emplace_back(event_state_mutexes[slot]);
        return locks;
    }
    std::string Identity(std::uint64_t from,std::uint64_t to,const std::string& cid) {
        if(!options.resolve_identity)return cid;
        auto result=options.resolve_identity(from,to,cid);
        if(result.status!=Status::kSucceeded)throw Failure(result.status,result.message);
        Require(result.canonical_cid.size()<=1024,Status::kInvalidRecord,"invalid canonical local CID length");
        return std::move(result.canonical_cid);
    }
    bool Get(const std::string& key,std::string& value,const rocksdb::ReadOptions& ro={}) const {
        auto status=db->Get(ro,key,&value);if(status.IsNotFound())return false;Check(status);return true;
    }
    MessageRepositoryGetResult Record(std::uint64_t mid,const rocksdb::ReadOptions& ro={}) const {
        MessageRepositoryGetResult out;out.status=Status::kSucceeded;std::string value;
        if(Get(MessageKey(mid),value,ro)){out.record=DecodeRecord(value,mid);out.found=true;}return out;
    }
    std::uint64_t Count(const std::string& key,const rocksdb::ReadOptions& ro={}) const {
        std::string value;return Get(key,value,ro)?ParseNumber(value):0;
    }
    void WriteDirect(rocksdb::WriteBatch& batch,bool sync) {
        Require(!poisoned.load(),Status::kStorageError,"local owner requires recovery after write failure");
        rocksdb::WriteOptions write;write.sync=sync;write.disableWAL=false;
        try{
            if(observer)observer(CommitPoint::kBeforeEngineWrite,batch.Count());
            ++engine_write_calls;
            {
                NativeRocksWriteTrace trace;
                Check(db->Write(write,&batch));
            }
            if(observer)observer(CommitPoint::kAfterEngineWriteBeforeWake,batch.Count());
        }catch(...){poisoned.store(true);throw;}
    }
    void Write(rocksdb::WriteBatch& batch,bool sync) {
        if(!ready_writes){WriteDirect(batch,sync);return;}
        try{
            ready_writes->Run(BorrowedWrite{&batch,sync},batch.GetDataSize(),
                [this](std::span<const BorrowedWrite> inputs){
                    Require(!inputs.empty(),Status::kStorageError,"empty physical write group");
                    if(inputs.size()==1){WriteDirect(*inputs.front().batch,inputs.front().sync);return;}
                    std::size_t reserved=0;bool synchronous=false;
                    for(const auto& input:inputs){reserved+=input.batch->GetDataSize();synchronous=synchronous||input.sync;}
                    // Group admission bounds cumulative serialized input bytes.
                    // Reserve once; do not grow a second unbounded merge buffer.
                    rocksdb::WriteBatch merged(reserved);
                    class Copy final:public rocksdb::WriteBatch::Handler {
                    public:
                        explicit Copy(rocksdb::WriteBatch& value):target(value){}
                        rocksdb::Status PutCF(std::uint32_t cf,const rocksdb::Slice& key,const rocksdb::Slice& value)override{
                            if(cf)return rocksdb::Status::InvalidArgument("non-default native column family");
                            return target.Put(key,value);
                        }
                        rocksdb::Status DeleteCF(std::uint32_t cf,const rocksdb::Slice& key)override{
                            if(cf)return rocksdb::Status::InvalidArgument("non-default native column family");
                            return target.Delete(key);
                        }
                        void LogData(const rocksdb::Slice&)override{unsupported=true;}
                        rocksdb::WriteBatch& target;bool unsupported{false};
                    } copy(merged);
                    for(const auto& input:inputs){
                        const auto before=merged.Count();
                        Check(input.batch->Iterate(&copy));
                        Require(!copy.unsupported&&merged.Count()-before==input.batch->Count(),
                            Status::kStorageError,"unsupported native physical group operation");
                    }
                    // Domain/CID locks are still held by their original callers.
                    // Only the storage entrance is shared. Any required sync
                    // applies to the whole group; no caller is completed early.
                    WriteDirect(merged,synchronous);
                });
        }catch(...){poisoned.store(true);throw;}
    }
    std::uint64_t NextId() {
        std::lock_guard hold(id_mutex);
        if(next_id>reserved_end) {
            Require(reserved_end<std::numeric_limits<std::int64_t>::max()-4096,Status::kFailedPrecondition,"local MID space exhausted");
            const auto end=reserved_end+4096;rocksdb::WriteBatch batch;batch.Put("meta/high",Number(end));Write(batch,true);reserved_end=end;++reservations;
        }
        return next_id++;
    }
    struct EventSlot {
        Impl& owner;bool committed{false};
        std::string indexed_id;
        std::shared_ptr<PendingIndexEntry> index_entry;
        explicit EventSlot(Impl& i):owner(i) {
            auto n=owner.pending_events.load();
            do{Require(n<owner.options.max_pending_events,Status::kFailedPrecondition,"local pending event capacity reached");}
            while(!owner.pending_events.compare_exchange_weak(n,n+1));
        }
        void Prepare(const std::string& id){
            if(!owner.pending_index_enabled)return;
            indexed_id=id;index_entry=std::make_shared<PendingIndexEntry>();
            std::lock_guard hold(owner.pending_index_mutex);
            Require(owner.pending_index.emplace(indexed_id,index_entry).second,
                Status::kInvalidRecord,"duplicate prepared local event identity");
        }
        void Commit()noexcept{
            committed=true;
            if(index_entry)index_entry->ready.store(true,std::memory_order_release);
        }
        ~EventSlot(){
            if(committed)return;
            if(index_entry){
                std::lock_guard hold(owner.pending_index_mutex);
                const auto found=owner.pending_index.find(indexed_id);
                if(found!=owner.pending_index.end()&&found->second==index_entry)owner.pending_index.erase(found);
            }
            --owner.pending_events;
        }
    };
    void Event(rocksdb::WriteBatch& batch,const OutboxEventSpec& event,EventSlot& slot) {
        const auto encoded=eventing::EventCodec::Encode(event.event);
        Require(encoded.success,Status::kInvalidRecord,"local domain event encoding failed");
        Json value{{"event_id",event.event.event_id},{"topic",event.topic},{"tag",event.tag},{"key",event.message_key},{"body",encoded.encoded}};
        static const bool reuse_encoding=[] {
            const char* v=std::getenv("TINYIMX_NATIVE_EVENT_ENCODING_REUSE_ENABLE");
            return v&&v[0]=='1'&&v[1]=='\0';
        }();
        if(reuse_encoding) {
            // WriteBatch::Put copies both values. Keep the exact durable bytes
            // and atomic E/A pair while avoiding a second JSON serialization.
            const auto bytes=value.dump();
            batch.Put("E/"+event.event.event_id,bytes);batch.Put("A/"+event.event.event_id,bytes);
        }else {
            batch.Put("E/"+event.event.event_id,value.dump());batch.Put("A/"+event.event.event_id,value.dump());
        }
        slot.Prepare(event.event.event_id);
    }
    bool Shutdown() noexcept {
        closing.store(true);std::unique_lock hold(lifetime);
        if(!db)return !poisoned.load();
        bool good=true;try{good=db->FlushWAL(true).ok();good=db->Close().ok()&&good;}catch(...){good=false;}
        if(ready_writes)try{
            const auto group=ready_writes->Snapshot();
            std::clog<<"NATIVE_READY_PHYSICAL_WRITE_GROUP calls="<<group.calls<<" admitted="<<group.admitted
                <<" execute_calls="<<group.execute_calls<<" engine_write_calls="<<engine_write_calls.load()
                <<" grouped_inputs="<<group.grouped_inputs<<" fallbacks="<<group.fallbacks
                <<" largest_group="<<group.largest_group<<" held_input_bytes="<<group.held_input_bytes
                <<" queued="<<group.queued<<std::endl;
        }catch(...){/* Telemetry cannot change the durable close outcome. */}
        db.reset();return good&&!poisoned.load();
    }
};

LocalPrivateStore::LocalPrivateStore(LocalPrivateStoreOptions options,CommitObserver observer)
    :impl_(std::make_unique<Impl>(std::move(options),std::move(observer))){}
LocalPrivateStore::~LocalPrivateStore(){Shutdown();}
bool LocalPrivateStore::Shutdown() noexcept{return impl_->Shutdown();}

MessageRepositoryPersistResult LocalPrivateStore::PersistPrivateMessage(
    std::uint64_t from,std::uint64_t to,const std::string& cid,std::uint32_t type,const std::string& content) {
    NativePersistencePhaseTrace trace(from,to);
    using Phase=NativePersistencePhaseTrace;
    auto result=impl_->Call<MessageRepositoryPersistResult>([&](auto& out){
        auto& s=*impl_;s.Dialog(from,to);
        Require(!cid.empty()&&cid.size()<=64&&type>=1&&type<=3&&!content.empty(),Status::kInvalidArgument,"invalid local private message input");
        const auto canonical=trace.Measure(Phase::Identity,[&]{return s.Identity(from,to,cid);});
        if(s.observer)s.observer(CommitPoint::kBeforeLocks,0);
        auto locks=trace.Measure(Phase::LocalLock,[&]{return s.options.exact_user_locking?s.LockActors({from,to}):s.Lock({CidKey(from,canonical),"receiver/"+Hex(to),DialogPrefix(from,to)});});
        if(s.observer)s.observer(CommitPoint::kAfterLocks,0);
        std::string existing;
        if(trace.Measure(Phase::CidLookup,[&]{return s.Get(CidKey(from,canonical),existing);})) {
            auto found=s.Record(ParseNumber(existing));Require(found.Found(),Status::kInvalidRecord,"orphan local CID index");
            std::string stored_key;
            Require(found.record.from_user_id==from&&s.Get("K/"+Hex(found.record.message_id),stored_key)&&stored_key==canonical,Status::kInvalidRecord,"mismatched local canonical identity");
            out.message_id=found.record.message_id;out.record=std::move(found.record);
            out.outcome=out.record.from_user_id==from&&out.record.to_user_id==to&&out.record.client_message_id==cid&&out.record.message_type==type&&out.record.content==content?PersistPrivateMessageOutcome::kReused:PersistPrivateMessageOutcome::kIdempotencyConflict;
            return;
        }
        trace.Measure(Phase::BodyValidation,[&]{
            Require(content.size()<=65535,Status::kStorageError,"message exceeds legacy MySQL TEXT byte capacity");
            try{(void)Json(content).dump();}catch(const Json::exception&){throw Failure(Status::kStorageError,"invalid legacy utf8mb4 message bytes");}
        });
        trace.Measure(Phase::EventAdmission,[&]{Require(!s.options.admit_events||s.options.admit_events(),Status::kStorageError,"local event admission fenced");});
        const auto reserve=Phase::StartCurrent();Impl::EventSlot slot(s);Phase::RecordCurrent(Phase::EventSlotReserve,reserve);
        if(s.options.before_new_message_commit){auto pinned=trace.Measure(Phase::References,[&]{return s.options.before_new_message_commit(from,to);});if(!pinned.Succeeded())throw Failure(pinned.status,pinned.message);}
        MessageView record;record.message_id=trace.Measure(Phase::MidAllocate,[&]{return s.NextId();});trace.Allocated(record.message_id);
        trace.Measure(Phase::RecordBuild,[&]{record.from_user_id=from;record.to_user_id=to;record.client_message_id=cid;record.message_type=type;record.content=content;record.created_at=Now();});
        const auto [total,dialog,pending]=trace.Measure(Phase::StateRead,[&]{return std::array<std::uint64_t,3>{s.Count("N/"+Hex(to)),s.Count(DialogUnreadKey(to,from)),s.Count("Q/"+Hex(to))};});
        Require(total<std::numeric_limits<std::uint64_t>::max()&&dialog<=total&&pending<=total,Status::kInvalidRecord,"invalid local unread counters");
        rocksdb::WriteBatch batch;
        trace.Measure(Phase::BatchBuild,[&]{
            batch.Put(MessageKey(record.message_id),EncodeRecord(record).dump());batch.Put(CidKey(from,canonical),Number(record.message_id));
            batch.Put("K/"+Hex(record.message_id),canonical);
            batch.Put("U/"+Hex(from),"");batch.Put("U/"+Hex(to),"");
            batch.Put(DialogPrefix(from,to)+Hex(record.message_id),"");batch.Put(PendingPrefix(to)+Hex(record.message_id),"");
            batch.Put("C/"+Hex(from)+'/'+Hex(to),Number(record.message_id));batch.Put("C/"+Hex(to)+'/'+Hex(from),Number(record.message_id));
            batch.Put("N/"+Hex(to),Number(total+1));batch.Put(DialogUnreadKey(to,from),Number(dialog+1));batch.Put("Q/"+Hex(to),Number(pending+1));
            batch.Put("R/"+Hex(to)+'/'+Hex(from),"");
        });
        trace.Measure(Phase::EventEncode,[&]{s.Event(batch,MessageEventFactory::MessageCreated(record,s.options.message_events_topic),slot);});
        if(s.observer)s.observer(CommitPoint::kBeforeWrite,record.message_id);
        trace.Measure(Phase::EngineSync,[&]{s.Write(batch,true);});slot.Commit();++s.messages;++s.message_batches;
        trace.Measure(Phase::PostWriteSignal,[&]{s.options.event_signal->Notify();});
        if(s.observer)s.observer(CommitPoint::kAfterSyncBeforeReply,record.message_id);
        out.message_id=record.message_id;out.record=std::move(record);out.outcome=PersistPrivateMessageOutcome::kCreated;
        out.committed_unread=NativeCreatedUnreadReply(out.message_id,to,from,dialog+1,total+1);
    });
    trace.Finish(static_cast<int>(result.status),static_cast<int>(result.outcome),result.message_id);
    return result;
}

std::vector<MessageRepositoryPersistResult> LocalPrivateStore::PersistPrivateMessages(
    const std::vector<PrivateMessagePersistInput>& inputs) {
    std::vector<MessageRepositoryPersistResult> out(inputs.size());
    if(inputs.size()>64) {
        for(auto& result:out){result.status=Status::kInvalidArgument;result.message="local private persistence batch exceeds64";}
        return out;
    }
    if(inputs.empty())return out;
    if(!impl_->options.persist_batch_enable||inputs.size()==1) {
        for(std::size_t i=0;i<inputs.size();++i) {
            const auto& in=inputs[i];out[i]=PersistPrivateMessage(in.from_user_id,in.to_user_id,
                in.client_message_id,in.message_type,in.content);
        }
        return out;
    }

    auto& s=*impl_;
    NativePersistencePhaseTrace trace(inputs.front().from_user_id,inputs.front().to_user_id);
    using Phase=NativePersistencePhaseTrace;
    trace.BatchInputs(static_cast<unsigned>(inputs.size()));
    auto lifetime=trace.Measure(Phase::LifetimeLock,[&]{return std::shared_lock(s.lifetime);});
    std::vector<bool> eligible(inputs.size()),provisional(inputs.size());
    std::vector<std::string> canonical(inputs.size());
    // These slots and all temporary state remain owned until the single write
    // succeeds or fails. No result depending on a staged CID escapes early.
    struct Create {
        std::size_t result_index;
        std::unique_ptr<Impl::EventSlot> slot;
        std::vector<std::pair<std::string,std::string>> writes;
    };
    std::vector<Create> creates;creates.reserve(inputs.size());
    auto fail_uncommitted=[&](Status status,const std::string& message) {
        for(std::size_t i=0;i<out.size();++i) {
            if(provisional[i]||(out[i].status==Status::kStorageError&&out[i].message.empty())) {
                out[i]={};out[i].status=status;out[i].message=message;
            }
        }
    };
    try {
        s.Open();
        std::set<std::uint64_t> actors;
        std::set<std::size_t> stripes;
        for(std::size_t i=0;i<inputs.size();++i) {
            const auto& in=inputs[i];
            try {
                s.Dialog(in.from_user_id,in.to_user_id);
                Require(!in.client_message_id.empty()&&in.client_message_id.size()<=64&&
                    in.message_type>=1&&in.message_type<=3&&!in.content.empty(),
                    Status::kInvalidArgument,"invalid local private message input");
                canonical[i]=trace.Measure(Phase::Identity,[&]{return s.Identity(in.from_user_id,in.to_user_id,in.client_message_id);});
                actors.insert(in.from_user_id);actors.insert(in.to_user_id);
                if(!s.options.exact_user_locking) {
                    for(const auto& key:{CidKey(in.from_user_id,canonical[i]),
                        "receiver/"+Hex(in.to_user_id),DialogPrefix(in.from_user_id,in.to_user_id)})
                        stripes.insert(std::hash<std::string>{}(key)%s.stripes.size());
                }
                eligible[i]=true;
            }catch(const Failure& e){out[i].status=e.status;out[i].message=e.what();}
            catch(const Json::exception&){out[i].status=Status::kInvalidRecord;out[i].message="invalid local serialized record";}
        }
        if(s.observer)s.observer(CommitPoint::kBeforeLocks,0);
        std::vector<std::unique_lock<std::mutex>> locks;
        if(s.options.exact_user_locking)locks=trace.Measure(Phase::LocalLock,[&]{return s.LockActors(actors);});
        else trace.Measure(Phase::LocalLock,[&]{for(auto index:stripes)locks.emplace_back(s.stripes[index]);});
        if(s.observer)s.observer(CommitPoint::kAfterLocks,0);

        std::map<std::string,std::size_t> staged_cids;
        std::map<std::string,std::uint64_t> counters;
        auto count=[&](const std::string& key) {
            auto found=counters.find(key);
            return found==counters.end()?s.Count(key):found->second;
        };
        for(std::size_t i=0;i<inputs.size();++i) {
            if(!eligible[i])continue;
            const auto& in=inputs[i];
            const auto from=in.from_user_id,to=in.to_user_id;
            const auto cid_key=CidKey(from,canonical[i]);
            try {
                MessageView existing;
                bool found=false;
                if(const auto staged=staged_cids.find(cid_key);staged!=staged_cids.end()) {
                    existing=out[staged->second].record;found=true;provisional[i]=true;
                }else {
                    std::string value;
                    if(s.Get(cid_key,value)) {
                        auto record=s.Record(ParseNumber(value));
                        Require(record.Found(),Status::kInvalidRecord,"orphan local CID index");
                        std::string stored_key;
                        Require(record.record.from_user_id==from&&s.Get("K/"+Hex(record.record.message_id),stored_key)&&
                            stored_key==canonical[i],Status::kInvalidRecord,"mismatched local canonical identity");
                        existing=std::move(record.record);found=true;
                    }
                }
                if(found) {
                    auto& result=out[i];result.status=Status::kSucceeded;result.message_id=existing.message_id;
                    result.outcome=existing.from_user_id==from&&existing.to_user_id==to&&
                        existing.client_message_id==in.client_message_id&&existing.message_type==in.message_type&&
                        existing.content==in.content?PersistPrivateMessageOutcome::kReused:PersistPrivateMessageOutcome::kIdempotencyConflict;
                    result.record=std::move(existing);continue;
                }
                Require(in.content.size()<=65535,Status::kStorageError,"message exceeds legacy MySQL TEXT byte capacity");
                try{(void)Json(in.content).dump();}
                catch(const Json::exception&){throw Failure(Status::kStorageError,"invalid legacy utf8mb4 message bytes");}
                Require(!s.options.admit_events||s.options.admit_events(),Status::kStorageError,"local event admission fenced");
                auto slot=std::make_unique<Impl::EventSlot>(s);
                if(s.options.before_new_message_commit) {
                    auto pinned=trace.Measure(Phase::References,[&]{return s.options.before_new_message_commit(from,to);});
                    if(!pinned.Succeeded())throw Failure(pinned.status,pinned.message);
                }
                MessageView record;record.message_id=s.NextId();record.from_user_id=from;record.to_user_id=to;
                record.client_message_id=in.client_message_id;record.message_type=in.message_type;
                record.content=in.content;record.created_at=Now();
                const auto total_key="N/"+Hex(to),dialog_key=DialogUnreadKey(to,from),pending_key="Q/"+Hex(to);
                const auto total=count(total_key),dialog=count(dialog_key),pending=count(pending_key);
                Require(total<std::numeric_limits<std::uint64_t>::max()&&dialog<=total&&pending<=total,
                    Status::kInvalidRecord,"invalid local unread counters");
                const auto event=MessageEventFactory::MessageCreated(record,s.options.message_events_topic);
                const auto encoded=eventing::EventCodec::Encode(event.event);
                Require(encoded.success,Status::kInvalidRecord,"local domain event encoding failed");
                const auto event_value=Json{{"event_id",event.event.event_id},{"topic",event.topic},
                    {"tag",event.tag},{"key",event.message_key},{"body",encoded.encoded}}.dump();
                Create create{i,std::move(slot),{
                    {MessageKey(record.message_id),EncodeRecord(record).dump()},
                    {cid_key,Number(record.message_id)},{"K/"+Hex(record.message_id),canonical[i]},
                    {"U/"+Hex(from),""},{"U/"+Hex(to),""},
                    {DialogPrefix(from,to)+Hex(record.message_id),""},{PendingPrefix(to)+Hex(record.message_id),""},
                    {"C/"+Hex(from)+'/'+Hex(to),Number(record.message_id)},
                    {"C/"+Hex(to)+'/'+Hex(from),Number(record.message_id)},
                    {total_key,Number(total+1)},{dialog_key,Number(dialog+1)},{pending_key,Number(pending+1)},
                    {"R/"+Hex(to)+'/'+Hex(from),""},
                    {"E/"+event.event.event_id,event_value},{"A/"+event.event.event_id,event_value}}};
                create.slot->Prepare(event.event.event_id);
                // All domain checks precede staging. Allocation failures abort
                // the entire uncommitted group, rather than leaving a partial
                // write batch or stale in-memory counter overlay for its peers.
                creates.push_back(std::move(create));
                provisional[i]=true;
                auto& result=out[i];result.status=Status::kSucceeded;result.message_id=record.message_id;
                result.record=std::move(record);result.outcome=PersistPrivateMessageOutcome::kCreated;
                staged_cids.emplace(cid_key,i);
                counters[total_key]=total+1;counters[dialog_key]=dialog+1;counters[pending_key]=pending+1;
            }catch(const Failure& e){out[i]={};out[i].status=e.status;out[i].message=e.what();}
            catch(const Json::exception&){out[i]={};out[i].status=Status::kInvalidRecord;out[i].message="invalid local serialized record";}
        }
        if(creates.empty()){trace.FinishBatch(out);return out;}
        rocksdb::WriteBatch batch;
        for(const auto& create:creates)
            for(const auto&[key,value]:create.writes)s.Check(batch.Put(key,value));
        const auto first=out[creates.front().result_index].message_id;
        trace.Allocated(first);
        if(s.observer)s.observer(CommitPoint::kBeforePrivateBatchWrite,first);
        trace.Measure(Phase::EngineSync,[&]{s.Write(batch,true);});
        for(auto& create:creates)create.slot->Commit();
        s.messages.fetch_add(creates.size());++s.message_batches;
        trace.Measure(Phase::PostWriteSignal,[&]{s.options.event_signal->Notify();});
        // Every snapshot describes the final counters of this atomic group.
        // Reused/conflicting rows keep their legacy recovery/read-model path.
        // The counters are already staged; do not perform another DB read.
        for(const auto& create:creates) {
            auto& result=out[create.result_index];const auto& record=result.record;
            const auto total=counters.find("N/"+Hex(record.to_user_id));
            const auto dialog=counters.find(DialogUnreadKey(record.to_user_id,record.from_user_id));
            if(total!=counters.end()&&dialog!=counters.end())
                result.committed_unread=NativeCreatedUnreadReply(result.message_id,record.to_user_id,
                    record.from_user_id,dialog->second,total->second);
        }
        if(s.observer)s.observer(CommitPoint::kAfterPrivateBatchSyncBeforeReply,first);
    }catch(const Failure& e){fail_uncommitted(e.status,e.what());}
    catch(const Json::exception&){fail_uncommitted(Status::kInvalidRecord,"invalid local serialized record");}
    catch(const std::exception& e){fail_uncommitted(Status::kStorageError,e.what());}
    trace.FinishBatch(out);return out;
}

MessageRepositoryGetResult LocalPrivateStore::GetPrivateMessage(std::uint64_t mid) {
    return impl_->Call<MessageRepositoryGetResult>([&](auto& out){Require(mid>0,Status::kInvalidArgument,"zero local MID");out=impl_->Record(mid);});
}
MessageRepositoryGetResult LocalPrivateStore::FindPrivateMessageByClientMessageId(std::uint64_t from,const std::string& cid) {
    return impl_->Call<MessageRepositoryGetResult>([&](auto& out){impl_->Actor(from);Require(!cid.empty()&&cid.size()<=64,Status::kInvalidArgument,"invalid local CID");
        const auto canonical=impl_->Identity(from,0,cid);
        Snapshot snapshot(impl_->db.get());std::string value;if(impl_->Get(CidKey(from,canonical),value,snapshot.read)){
            out=impl_->Record(ParseNumber(value),snapshot.read);std::string stored_key;
            Require(out.Found()&&out.record.from_user_id==from&&impl_->Get("K/"+Hex(out.record.message_id),stored_key,snapshot.read)&&stored_key==canonical,
                Status::kInvalidRecord,"orphan/mismatched local CID");}
    });
}

MessageRepositoryHistoryResult LocalPrivateStore::ListHistory(std::uint64_t actor,std::uint64_t peer,std::uint64_t before,std::size_t limit) {
    return impl_->Call<MessageRepositoryHistoryResult>([&](auto& out){
        auto& s=*impl_;s.Dialog(actor,peer);s.Limit(limit);Snapshot snapshot(s.db.get());
        const auto prefix=DialogPrefix(actor,peer);const auto bound=before?prefix+Hex(before):prefix+'~';
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));it->SeekForPrev(bound);
        if(before&&it->Valid()&&it->key().ToString()==bound)it->Prev();
        for(;it->Valid()&&it->key().starts_with(prefix)&&out.records.size()<limit;it->Prev()) {
            auto found=s.Record(Unhex(it->key().ToString().substr(prefix.size())),snapshot.read);
            Require(found.Found()&&((found.record.from_user_id==actor&&found.record.to_user_id==peer)||(found.record.from_user_id==peer&&found.record.to_user_id==actor)),Status::kInvalidRecord,"orphan local dialog index");
            out.records.push_back(std::move(found.record));
        }
        s.Check(it->status());
        // Select the newest bounded window, then return it oldest-to-newest.
        // MessageApplicationService removes the first lookahead row and the
        // existing MessageRpcClient rejects any non-increasing MID sequence.
        std::reverse(out.records.begin(),out.records.end());
    });
}
MessageRepositoryConversationResult LocalPrivateStore::ListConversations(std::uint64_t actor,std::size_t limit) {
    return impl_->Call<MessageRepositoryConversationResult>([&](auto& out){
        auto& s=*impl_;s.Actor(actor);s.Limit(limit);Snapshot snapshot(s.db.get());const auto prefix="C/"+Hex(actor)+'/';
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));
        for(it->Seek(prefix);it->Valid()&&it->key().starts_with(prefix);it->Next()) {
            const auto peer=Unhex(it->key().ToString().substr(prefix.size()));auto found=s.Record(ParseNumber(it->value().ToString()),snapshot.read);
            Require(found.Found()&&((found.record.from_user_id==actor&&found.record.to_user_id==peer)||(found.record.from_user_id==peer&&found.record.to_user_id==actor)),Status::kInvalidRecord,"orphan local conversation index");
            const auto& m=found.record;ConversationView c;c.peer_user_id=peer;c.last_message_id=m.message_id;c.last_client_message_id=m.client_message_id;
            c.last_from_user_id=m.from_user_id;c.last_to_user_id=m.to_user_id;c.last_message_type=m.message_type;c.last_content=m.content;
            c.last_delivery_state=m.delivery_state;c.last_created_at=m.created_at;c.last_receiver_confirmed_at=m.receiver_confirmed_at;c.last_read_at=m.read_at;
            out.records.push_back(std::move(c));
        }
        s.Check(it->status());std::sort(out.records.begin(),out.records.end(),[](const auto&a,const auto&b){return a.last_message_id>b.last_message_id;});
        if(out.records.size()>limit)out.records.resize(limit);
    });
}
MessageRepositoryCountResult LocalPrivateStore::CountPending(std::uint64_t receiver) {
    return impl_->Call<MessageRepositoryCountResult>([&](auto& out){impl_->Actor(receiver);out.count=impl_->Count("Q/"+Hex(receiver));});
}
MessageRepositoryPendingResult LocalPrivateStore::ListPendingAfter(std::uint64_t receiver,std::uint64_t after,std::size_t limit) {
    return impl_->Call<MessageRepositoryPendingResult>([&](auto& out){
        auto& s=*impl_;s.Actor(receiver);s.Limit(limit);Snapshot snapshot(s.db.get());const auto prefix=PendingPrefix(receiver),start=prefix+Hex(after);
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));it->Seek(start);if(it->Valid()&&it->key().ToString()==start)it->Next();
        for(;it->Valid()&&it->key().starts_with(prefix)&&out.records.size()<limit;it->Next()) {
            auto found=s.Record(Unhex(it->key().ToString().substr(prefix.size())),snapshot.read);
            Require(found.Found()&&found.record.to_user_id==receiver&&found.record.delivery_state==MessageDeliveryState::kPending,Status::kInvalidRecord,"orphan local pending index");
            out.records.push_back(std::move(found.record));
        }
        s.Check(it->status());
    });
}
PendingRecipientsResult LocalPrivateStore::ListPendingRecipientsAfter(std::uint64_t after,std::size_t limit) {
    return impl_->Call<PendingRecipientsResult>([&](auto& out){
        auto& s=*impl_;s.Limit(limit);Snapshot snapshot(s.db.get());const auto start="Q/"+Hex(after);
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));it->Seek(start);if(it->Valid()&&it->key().ToString()==start)it->Next();
        for(;it->Valid()&&it->key().starts_with("Q/");it->Next()) {
            Require(ParseNumber(it->value().ToString())>0,Status::kInvalidRecord,"zero local pending-recipient index");
            if(out.recipient_user_ids.size()==limit){out.has_more=true;break;}
            out.recipient_user_ids.push_back(Unhex(it->key().ToString().substr(2)));
        }
        s.Check(it->status());
    });
}
LocalUnreadResult LocalPrivateStore::ReadUnreadSnapshot(std::uint64_t receiver,std::uint64_t peer) {
    return impl_->Call<LocalUnreadResult>([&](auto& out){
        auto& s=*impl_;s.Dialog(receiver,peer);Snapshot snapshot(s.db.get());out.total_unread=s.Count("N/"+Hex(receiver),snapshot.read);
        out.private_unread=s.Count(DialogUnreadKey(receiver,peer),snapshot.read);
        Require(out.private_unread<=out.total_unread,Status::kInvalidRecord,"local unread snapshot invariant failed");
    });
}
LocalUserUnreadResult LocalPrivateStore::ReadUserUnreadSnapshot(std::uint64_t receiver) {
    return impl_->Call<LocalUserUnreadResult>([&](auto& out){
        auto& s=*impl_;s.Actor(receiver);Snapshot snapshot(s.db.get());const auto prefix="R/"+Hex(receiver)+'/';
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));
        for(it->Seek(prefix);it->Valid()&&it->key().starts_with(prefix);it->Next()) {
            const auto peer=Unhex(it->key().ToString().substr(prefix.size()));
            const auto count=s.Count(DialogUnreadKey(receiver,peer),snapshot.read);
            Require(peer&&peer!=receiver&&out.peer_counts.size()<100000&&count<=std::numeric_limits<std::int64_t>::max()-out.total_unread,
                Status::kInvalidRecord,"invalid or over-budget local user unread snapshot");
            out.peer_counts.emplace_back(peer,count);out.total_unread+=count;
        }
        s.Check(it->status());Require(out.total_unread==s.Count("N/"+Hex(receiver),snapshot.read),Status::kInvalidRecord,"local user unread total disagrees");
    });
}
MessageRepositoryMutationResult LocalPrivateStore::ConfirmReceiver(std::uint64_t mid,std::uint64_t receiver) {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;s.Actor(receiver);Require(mid>0,Status::kInvalidArgument,"zero local MID");
        auto locks=s.options.exact_user_locking?s.LockActors({receiver}):s.Lock({"receiver/"+Hex(receiver)});
        auto found=s.Record(mid);if(auto decision=ReceiverConfirmationDecision(found,receiver)){out=std::move(*decision);return;}
        auto record=std::move(found.record);record.delivery_state=MessageDeliveryState::kReceiverConfirmed;record.receiver_confirmed_at=Now();
        const auto pending=s.Count("Q/"+Hex(receiver));Require(pending>0,Status::kInvalidRecord,"local pending counter underflow");
        rocksdb::WriteBatch batch;batch.Put(MessageKey(mid),EncodeRecord(record).dump());batch.Delete(PendingPrefix(receiver)+Hex(mid));
        if(pending==1)batch.Delete("Q/"+Hex(receiver));else batch.Put("Q/"+Hex(receiver),Number(pending-1));
        s.Write(batch,true);++s.confirmations;out.affected_rows=1;
    });
}
MessageRepositoryMutationResult LocalPrivateStore::ConfirmReceiverBatch(const std::vector<std::uint64_t>& mids) {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;Require(!mids.empty()&&mids.size()<=100,Status::kInvalidArgument,"invalid local receiver batch size");
        std::set<std::uint64_t> unique,actors;
        for(auto mid:mids) {
            Require(mid&&unique.insert(mid).second,Status::kInvalidArgument,"zero or duplicate local receiver MID");
            const auto found=s.Record(mid);Require(found.Found(),Status::kNotFound,"local receiver batch message missing");
            s.Actor(found.record.to_user_id);actors.insert(found.record.to_user_id);
        }
        auto locks=s.LockReceivers(actors);rocksdb::WriteBatch batch;
        std::map<std::uint64_t,std::uint64_t> removed;
        const auto now=Now();
        // Owners are immutable, but read/ACK state may have changed before the
        // locks. Re-read every record under all locks; one failure writes none.
        for(auto mid:mids) {
            auto found=s.Record(mid);Require(found.Found()&&actors.contains(found.record.to_user_id),Status::kInvalidRecord,"local receiver batch identity changed");
            const auto decision=ReceiverConfirmationDecision(found,found.record.to_user_id);
            if(decision){Require(decision->Succeeded(),decision->status,decision->message.c_str());continue;}
            auto record=std::move(found.record);record.delivery_state=MessageDeliveryState::kReceiverConfirmed;record.receiver_confirmed_at=now;
            batch.Put(MessageKey(mid),EncodeRecord(record).dump());batch.Delete(PendingPrefix(record.to_user_id)+Hex(mid));
            ++removed[record.to_user_id];++out.affected_rows;
        }
        if(!out.affected_rows)return;
        for(const auto&[receiver,count]:removed) {
            const auto pending=s.Count("Q/"+Hex(receiver));Require(pending>=count,Status::kInvalidRecord,"local receiver batch pending underflow");
            if(pending==count)batch.Delete("Q/"+Hex(receiver));else batch.Put("Q/"+Hex(receiver),Number(pending-count));
        }
        if(s.observer)s.observer(CommitPoint::kBeforeConfirmationBatchWrite,mids.front());
        s.Write(batch,true);++s.confirmations;
        if(s.observer)s.observer(CommitPoint::kAfterConfirmationBatchSyncBeforeReply,mids.front());
    });
}
std::vector<MessageRepositoryMutationResult> LocalPrivateStore::ConfirmReceiverRequests(
    const std::vector<OwnedReceiverConfirmation>& inputs) {
    std::vector<MessageRepositoryMutationResult> results(inputs.size());
    if(inputs.empty())return results;
    if(inputs.size()>64) {
        for(auto& result:results){result.status=Status::kInvalidArgument;result.message="owned receiver batch exceeds64";}
        return results;
    }
    auto& s=*impl_;
    std::shared_lock lifetime(s.lifetime);
    std::vector<bool> eligible(inputs.size()),provisional(inputs.size());
    const auto fail_provisional=[&](Status status,const std::string& message){
        for(std::size_t i=0;i<results.size();++i)if(provisional[i]) {
            results[i]={};results[i].status=status;results[i].message=message;
        }
    };
    try {
        s.Open();
        std::set<std::uint64_t> actors;
        for(std::size_t i=0;i<inputs.size();++i) {
            try {
                const auto& in=inputs[i];s.Actor(in.receiver_user_id);
                Require(in.message_id>0,Status::kInvalidArgument,"zero local MID");
                actors.insert(in.receiver_user_id);eligible[i]=true;
            }catch(const Failure& e){results[i].status=e.status;results[i].message=e.what();}
        }
        auto locks=s.LockReceivers(actors);
        std::map<std::uint64_t,MessageView> changed;
        std::map<std::uint64_t,std::uint64_t> removed;
        rocksdb::WriteBatch batch;
        const auto now=Now();
        for(std::size_t i=0;i<inputs.size();++i) {
            if(!eligible[i])continue;
            const auto& in=inputs[i];
            MessageRepositoryGetResult found;
            try {found=s.Record(in.message_id);}
            catch(const Failure& e){
                if(e.status!=Status::kInvalidRecord)throw;
                results[i].status=e.status;results[i].message=e.what();continue;
            }catch(const Json::exception&){
                results[i].status=Status::kInvalidRecord;
                results[i].message="invalid local serialized record";continue;
            }
            // Reflect an earlier request in this batch without decrementing
            // pending twice. A different owner must still receive permission denied.
            if(const auto it=changed.find(in.message_id);it!=changed.end())found.record=it->second;
            if(auto decision=ReceiverConfirmationDecision(found,in.receiver_user_id)) {
                results[i]=std::move(*decision);
                provisional[i]=results[i].Succeeded()&&changed.contains(in.message_id);
                continue;
            }
            auto record=std::move(found.record);
            record.delivery_state=MessageDeliveryState::kReceiverConfirmed;
            record.receiver_confirmed_at=now;
            changed.emplace(in.message_id,record);++removed[in.receiver_user_id];
            batch.Put(MessageKey(in.message_id),EncodeRecord(record).dump());
            batch.Delete(PendingPrefix(in.receiver_user_id)+Hex(in.message_id));
            provisional[i]=true;results[i].affected_rows=1;
        }
        if(changed.empty())return results;
        for(const auto&[receiver,count]:removed) {
            const auto pending=s.Count("Q/"+Hex(receiver));
            Require(pending>=count,Status::kInvalidRecord,"owned receiver batch pending underflow");
            if(pending==count)batch.Delete("Q/"+Hex(receiver));
            else batch.Put("Q/"+Hex(receiver),Number(pending-count));
        }
        const auto first=changed.begin()->first;
        if(s.observer)s.observer(CommitPoint::kBeforeConfirmationBatchWrite,first);
        s.Write(batch,true);++s.confirmations;
        if(s.observer)s.observer(CommitPoint::kAfterConfirmationBatchSyncBeforeReply,first);
        for(std::size_t i=0;i<results.size();++i)if(provisional[i]) {
            results[i].status=Status::kSucceeded;
            results[i].message=results[i].affected_rows?"private message receiver confirmation durable":"receiver confirmation already durable";
        }
    }catch(const Failure& e){fail_provisional(e.status,e.what());}
    catch(const Json::exception&){fail_provisional(Status::kInvalidRecord,"invalid local serialized record");}
    catch(const std::exception& e){fail_provisional(Status::kStorageError,e.what());}
    return results;
}
MessageRepositoryMutationResult LocalPrivateStore::MarkDialogRead(std::uint64_t reader,std::uint64_t peer) {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;s.Dialog(reader,peer);
        auto locks=s.options.exact_user_locking?s.LockActors({reader}):s.Lock({"receiver/"+Hex(reader)});Snapshot snapshot(s.db.get());
        const auto prefix=DialogPrefix(reader,peer);std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));
        rocksdb::WriteBatch batch;std::uint64_t changed=0,pending_removed=0;const auto when=Now();
        for(it->Seek(prefix);it->Valid()&&it->key().starts_with(prefix);it->Next()) {
            auto found=s.Record(Unhex(it->key().ToString().substr(prefix.size())),snapshot.read);Require(found.Found(),Status::kInvalidRecord,"orphan local history index");
            auto& m=found.record;
            Require((m.from_user_id==reader&&m.to_user_id==peer)||(m.from_user_id==peer&&m.to_user_id==reader),Status::kInvalidRecord,"local read index identity mismatch");
            if(m.to_user_id!=reader)continue;
            if(m.delivery_state!=MessageDeliveryState::kPending&&m.delivery_state!=MessageDeliveryState::kReceiverConfirmed)continue;
            if(m.delivery_state==MessageDeliveryState::kPending){batch.Delete(PendingPrefix(reader)+Hex(m.message_id));++pending_removed;}
            m.delivery_state=MessageDeliveryState::kRead;m.read_at=when;if(m.receiver_confirmed_at.empty())m.receiver_confirmed_at=when;
            batch.Put(MessageKey(m.message_id),EncodeRecord(m).dump());++changed;
            Require(batch.GetDataSize()<=64*1024*1024,Status::kFailedPrecondition,"local atomic read batch exceeds64MiB");
        }
        s.Check(it->status());if(!changed)return;
        const auto total=s.Count("N/"+Hex(reader)),dialog=s.Count(DialogUnreadKey(reader,peer)),pending=s.Count("Q/"+Hex(reader));
        Require(dialog==changed&&total>=changed&&pending>=pending_removed,Status::kInvalidRecord,"local dialog/read counters disagree");
        Require(!s.options.admit_events||s.options.admit_events(),Status::kStorageError,"local event admission fenced");
        Impl::EventSlot slot(s);batch.Put("N/"+Hex(reader),Number(total-changed));batch.Delete(DialogUnreadKey(reader,peer));
        if(pending==pending_removed)batch.Delete("Q/"+Hex(reader));else batch.Put("Q/"+Hex(reader),Number(pending-pending_removed));
        s.Event(batch,MessageEventFactory::DialogReadAdvanced(reader,peer,changed,s.options.message_events_topic),slot);
        s.Write(batch,true);slot.Commit();++s.reads;out.affected_rows=changed;
        s.options.event_signal->Notify();
    });
}
LocalEventsResult LocalPrivateStore::ListPendingEvents(std::size_t limit) {
    return ListPendingEventsAfter("",limit);
}
LocalEventsResult LocalPrivateStore::ListPendingEventsAfter(const std::string& after,std::size_t limit) {
    return ListPendingEventPage(after,limit,nullptr);
}
LocalEventsResult LocalPrivateStore::ListPendingEventsAfterExcluding(const std::string& after,
    std::size_t limit,const std::unordered_set<std::string>& held) {
    return ListPendingEventPage(after,limit,&held);
}
LocalEventsResult LocalPrivateStore::ListPendingEventPage(const std::string& after,std::size_t limit,
    const std::unordered_set<std::string>* held) {
    return impl_->Call<LocalEventsResult>([&](auto& out){
        auto& s=*impl_;Require(limit>0&&limit<=256,Status::kInvalidArgument,"invalid local event limit");if(!after.empty())EventId(after);
        Require(!held||held->size()<=256,Status::kInvalidArgument,"held event set exceeds relay bound");
        const auto skip=[&](const std::string& id){
            ++out.examined_events;out.last_examined_event_id=id;
            if(held&&held->contains(id)){++s.held_events_skipped;return true;}
            return false;
        };
        // EventSlot reserves before the durable write and releases only after
        // removal or aborted admission. Therefore zero is a safe empty result;
        // a positive count may still include an uncommitted reservation.
        // Startup reconstructs it from E/. Call() still enforces poison/close,
        // and the normal commit/requeue signal wakes a waiting relay.
        if(EmptyEventScanGuardEnabled()&&s.pending_events.load()==0){
            ++s.empty_event_scans_skipped;return;
        }
        if(s.pending_index_enabled){
            ++s.pending_index_queries;
            std::vector<std::string> ids;ids.reserve(limit);
            {
                std::lock_guard hold(s.pending_index_mutex);
                auto it=after.empty()?s.pending_index.begin():s.pending_index.upper_bound(after);
                for(;it!=s.pending_index.end()&&ids.size()<limit;++it)
                    if(it->second->ready.load(std::memory_order_acquire))ids.push_back(it->first);
            }
            if(ids.empty())return;
            // Snapshot follows observation of committed entries. Concurrent
            // publication can remove a selected E/ key before the snapshot;
            // that is a normal disappearance, never a fabricated event.
            Snapshot snapshot(s.db.get());
            for(const auto& id:ids){
                if(skip(id))continue;
                std::string value;const auto read=s.db->Get(snapshot.read,"E/"+id,&value);
                if(read.IsNotFound())continue;
                s.Check(read);++s.event_decode_attempts;out.events.push_back(DecodeEvent(value,id));
            }
            return;
        }
        ++s.event_scans_started;
        Snapshot snapshot(s.db.get());
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));
        it->Seek("E/"+after);if(!after.empty()&&it->Valid()&&it->key().ToString()=="E/"+after)it->Next();
        for(;it->Valid()&&it->key().starts_with("E/")&&out.examined_events<limit;it->Next()) {
            const auto id=it->key().ToString().substr(2);if(skip(id))continue;
            ++s.event_decode_attempts;out.events.push_back(DecodeEvent(it->value().ToString(),id));
        }
        s.Check(it->status());
    });
}
LocalFailedEventsResult LocalPrivateStore::ListFailedEvents(std::size_t limit) {
    return impl_->Call<LocalFailedEventsResult>([&](auto& out){
        auto& s=*impl_;Require(limit>0&&limit<=256,Status::kInvalidArgument,"invalid local failed event limit");Snapshot snapshot(s.db.get());
        std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));
        for(it->Seek("F/");it->Valid()&&it->key().starts_with("F/")&&out.events.size()<limit;it->Next()){
            const auto data=Json::parse(it->value().ToString());FailedLocalEvent failed;
            failed.event=DecodeEvent(data.at("event").get<std::string>(),it->key().ToString().substr(2));failed.reason=data.at("reason");failed.failed_at=data.at("failed_at");
            Require(!failed.reason.empty()&&failed.reason.size()<=128&&failed.failed_at.size()==19,Status::kInvalidRecord,"invalid failed event metadata");out.events.push_back(std::move(failed));
        }
        s.Check(it->status());
    });
}
MessageRepositoryMutationResult LocalPrivateStore::MarkEventFailed(const std::string& id,const std::string& reason) {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;EventId(id);Require(!reason.empty()&&reason.size()<=128&&reason.find('\0')==std::string::npos,Status::kInvalidArgument,"invalid event failure reason");
        const auto locks=s.LockEventState(std::array<std::string_view,1>{id});std::string value;if(!s.Get("E/"+id,value))return;
        (void)DecodeEvent(value,id);
        rocksdb::WriteBatch batch;batch.Put("F/"+id,Json{{"event",value},{"reason",reason},{"failed_at",Now()}}.dump());batch.Delete("E/"+id);
        s.Write(batch,true);
        if(s.pending_index_enabled){std::lock_guard index_hold(s.pending_index_mutex);s.pending_index.erase(id);}
        s.pending_events.fetch_sub(1);out.affected_rows=1;
    });
}
MessageRepositoryMutationResult LocalPrivateStore::RequeueFailedEvents(const std::vector<std::string>& ids) {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;Require(ids.size()<=256,Status::kInvalidArgument,"too many failed events to requeue");std::set<std::string> unique;
        for(const auto& id:ids){EventId(id);unique.insert(id);}
        const auto locks=s.LockEventState(unique);rocksdb::WriteBatch batch;std::vector<std::unique_ptr<Impl::EventSlot>> slots;
        for(const auto& id:unique){std::string value;if(!s.Get("F/"+id,value))continue;const auto data=Json::parse(value);const auto raw=data.at("event").get<std::string>();(void)DecodeEvent(raw,id);
            std::string pending;Require(!s.Get("E/"+id,pending),Status::kInvalidRecord,"event is both pending and failed");
            slots.push_back(std::make_unique<Impl::EventSlot>(s));slots.back()->Prepare(id);batch.Put("E/"+id,raw);batch.Delete("F/"+id);++out.affected_rows;
        }
        if(!out.affected_rows)return;s.Write(batch,true);for(auto& slot:slots)slot->Commit();s.options.event_signal->Notify();
    });
}
LocalActorIndexResult LocalPrivateStore::ListMessageActors() {
    return impl_->Call<LocalActorIndexResult>([&](auto& out){
        auto& s=*impl_;Snapshot snapshot(s.db.get());std::unique_ptr<rocksdb::Iterator> it(s.db->NewIterator(snapshot.read));
        it->Seek("M/");out.has_messages=it->Valid()&&it->key().starts_with("M/");s.Check(it->status());
        for(it->Seek("U/");it->Valid()&&it->key().starts_with("U/");it->Next()){
            const auto actor=Unhex(it->key().ToString().substr(2));Require(actor&&out.actors.size()<100000,Status::kInvalidRecord,"invalid message actor index");
            std::unique_ptr<rocksdb::Iterator> peers(s.db->NewIterator(snapshot.read));const auto prefix="C/"+Hex(actor)+'/';peers->Seek(prefix);
            s.Check(peers->status());Require(peers->Valid()&&peers->key().starts_with(prefix),Status::kInvalidRecord,"orphan message actor index");out.actors.push_back(actor);
        }
        s.Check(it->status());Require(out.has_messages==!out.actors.empty(),Status::kInvalidRecord,"message and actor index disagree");
    });
}
std::shared_ptr<LocalEventSignal> LocalPrivateStore::EventSignal()const{return impl_->options.event_signal;}
std::string LocalPrivateStore::MessageEventsTopic()const{return impl_->options.message_events_topic;}
bool LocalPrivateStore::AcquireRelayOwnership()noexcept{bool expected=false;return impl_->relay_owned.compare_exchange_strong(expected,true);}
void LocalPrivateStore::ReleaseRelayOwnership()noexcept{impl_->relay_owned.store(false);}
MessageRepositoryMutationResult LocalPrivateStore::MarkEventsPublished(const std::vector<std::string>& ids) {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;Require(ids.size()<=256,Status::kInvalidArgument,"too many local publication results");
        std::set<std::string> unique;
        for(const auto& id:ids){Require(!id.empty()&&id.size()<=191&&id.find('\0')==std::string::npos,Status::kInvalidArgument,"invalid local event identity");unique.insert(id);}
        const std::string_view first=ids.empty()?std::string_view{}:std::string_view(ids.front());
        auto engine_trace=NativePublicationPhaseTrace::Deferred(NativePublicationPhaseTrace::ProgressEngineWrite,first,unique.size());
        NativePublicationPhaseTrace lock_trace(NativePublicationPhaseTrace::ProgressLockAcquire,first,unique.size());
        const auto locks=s.LockEventState(unique);lock_trace.Stop();lock_trace.Result(0,unique.size());
        rocksdb::WriteBatch batch;std::string value;std::vector<std::string> removed;
        if(s.pending_index_enabled)removed.reserve(unique.size());
        for(const auto& id:unique)if(s.Get("E/"+id,value)){
            batch.Delete("E/"+id);++out.affected_rows;
            if(s.pending_index_enabled)removed.push_back(id);
        }
        if(!out.affected_rows)return;
        engine_trace.Start();s.Write(batch,false);engine_trace.Stop();engine_trace.Result(0,out.affected_rows);
        if(s.pending_index_enabled){std::lock_guard index_hold(s.pending_index_mutex);for(const auto& id:removed)s.pending_index.erase(id);}
        s.pending_events.fetch_sub(out.affected_rows);++s.publications;
    });
}
MessageRepositoryMutationResult LocalPrivateStore::SyncPublicationProgress() {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto&){const auto result=impl_->db->FlushWAL(true);if(!result.ok())impl_->poisoned.store(true);impl_->Check(result);});
}
MessageRepositoryMutationResult LocalPrivateStore::SyncPublicationProgressIfDirty() {
    return impl_->Call<MessageRepositoryMutationResult>([&](auto& out){
        auto& s=*impl_;
        std::lock_guard hold(s.publication_sync_mutex);
        // Capture BEFORE flushing. A concurrent marker completed afterwards
        // must remain dirty, even if its WAL bytes happened to join this sync.
        const auto generation=s.publications.load();
        if(s.publication_sync_initialized&&generation==s.synced_publication_generation)return;
        const auto result=s.db->FlushWAL(true);
        if(!result.ok())s.poisoned.store(true);
        s.Check(result);
        if(s.observer)s.observer(CommitPoint::kAfterPublicationSyncBeforeCheckpoint,generation);
        s.synced_publication_generation=generation;
        s.publication_sync_initialized=true;
        out.affected_rows=1;
    });
}
bool LocalPrivateStore::EventProgressShardsEnabled()const noexcept{return impl_->event_progress_shards_enabled;}
LocalStoreStats LocalPrivateStore::Stats() const {
    const auto& s=*impl_;return {s.messages.load(),s.confirmations.load(),s.reads.load(),s.reservations.load(),s.publications.load(),s.statistics->getTickerCount(rocksdb::WAL_FILE_SYNCED),s.pending_events.load(),s.poisoned.load(),s.closing.load(),s.event_scans_started.load(),s.empty_event_scans_skipped.load(),s.pending_index_queries.load(),s.pending_index_enabled,s.event_decode_attempts.load(),s.held_events_skipped.load()};
}
std::uint64_t LocalPrivateStore::MessageCommitBatches()const noexcept{return impl_->message_batches.load();}
bool LocalPrivateStore::Healthy()const noexcept{return !impl_->closing.load()&&!impl_->poisoned.load();}
LocalPhysicalWriteGroupStats LocalPrivateStore::PhysicalWriteGroupStats()const{
    const auto& s=*impl_;
    if(!s.ready_writes)return {false,0,0,s.engine_write_calls.load()};
    const auto group=s.ready_writes->Snapshot();
    return {true,group.calls,group.execute_calls,s.engine_write_calls.load(),group.grouped_inputs,group.fallbacks,
        group.largest_group,group.held_input_bytes,group.queued};
}
} // namespace tinyimx::message::local
