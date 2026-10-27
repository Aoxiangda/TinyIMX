#include "services/message/local/LocalMessageRepositoryAdapter.h"
#include "tests/message/ReadyBatchNoopRepository.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace std::chrono_literals;
namespace {
unsigned checks=0;
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);++checks;std::cout<<"PASS "<<text<<'\n';}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& name,bool batch=true,bool exact=true){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="native-batch:"+root.filename().string()+':'+name;
    o.message_events_topic="codex_perf588_events";o.first_message_id=7100000000000000ULL;
    o.initial_user_ids={500001,500002,500003,500004};o.block_cache_bytes=1024*1024;
    o.exact_user_locking=exact;o.persist_batch_enable=batch;return o;
}
PrivateMessagePersistInput Input(std::uint64_t from,std::uint64_t to,std::string cid,std::string body="body",std::uint32_t type=1){
    PrivateMessagePersistInput in;in.from_user_id=from;in.to_user_id=to;in.client_message_id=std::move(cid);in.content=std::move(body);in.message_type=type;return in;
}
std::vector<PrivateMessagePersistInput> Fill(unsigned n,const std::string& prefix="item-"){
    std::vector<PrivateMessagePersistInput> out;
    for(unsigned i=0;i<n;++i)out.push_back(Input(500001,500002,prefix+std::to_string(i),"body:"+std::to_string(i)));
    return out;
}
void Compare(const MessageRepositoryPersistResult& a,const MessageRepositoryPersistResult& b){
    Check(a.status==b.status&&a.outcome==b.outcome&&a.message_id==b.message_id,"batch keeps sequential per-item status, outcome and MID");
    if(a.Accepted()||a.outcome==PersistPrivateMessageOutcome::kIdempotencyConflict)
        Check(a.record.from_user_id==b.record.from_user_id&&a.record.to_user_id==b.record.to_user_id&&
            a.record.client_message_id==b.record.client_message_id&&a.record.message_type==b.record.message_type&&
            a.record.content==b.record.content&&a.record.delivery_state==b.record.delivery_state,"batch preserves immutable result and initial delivery state");
}
void Oracle(const std::filesystem::path& root,bool exact){
    auto aopt=Options(root,exact?"oracle-single-exact":"oracle-single-striped",false,exact);
    auto bopt=Options(root,exact?"oracle-batch-exact":"oracle-batch-striped",true,exact);
    LocalPrivateStore a(aopt),b(bopt);
    auto in=std::vector<PrivateMessagePersistInput>{
        Input(500001,500002,"one"),Input(500003,500002,"two"),Input(500002,500001,"reverse"),
        Input(500001,500002,"one"),Input(500001,500002,"one","changed"),Input(500001,500004,"one"),
        Input(500001,500002,""),Input(500001,500001,"self"),Input(500099,500002,"foreign"),
        Input(500001,500002,"bad-type","body",4),Input(500001,500002,"empty",""),
        Input(500001,500002,"oversize",std::string(65536,'x')),Input(500001,500002,"utf8",std::string("\xff",1)),
        Input(500001,500002,"one",std::string(65536,'x')),Input(500004,500002,"last")};
    const auto x=a.PersistPrivateMessages(in),y=b.PersistPrivateMessages(in);
    Check(x.size()==in.size()&&y.size()==in.size(),"every input has one result without dropping rejected items");
    for(std::size_t i=0;i<x.size();++i)Compare(x[i],y[i]);
    Check(a.Stats().message_commits==4&&b.Stats().message_commits==4&&a.MessageCommitBatches()==4&&b.MessageCommitBatches()==1,
        "four genuine messages use four baseline commits and exactly one native batch commit");
    Check(a.ReadUserUnreadSnapshot(500002).total_unread==3&&b.ReadUserUnreadSnapshot(500002).total_unread==3&&
        a.CountPending(500002).count==3&&b.CountPending(500002).count==3,"shared recipient counter overlay prevents lost increments");
    const auto pending=b.ListPendingEvents(64);
    Check(pending.events.size()==4&&b.ListHistory(500001,500002,0,64).records.size()==2,"one event per new message and bidirectional history index retained");
    Check(b.ListConversations(500002,64).records.front().last_message_id==y.back().message_id,"conversation latest MID includes later entries in the same commit");
    const auto before=b.Stats();const auto retry=b.PersistPrivateMessages(in);
    Check(b.Stats().message_commits==before.message_commits&&b.MessageCommitBatches()==1&&b.Stats().pending_events==4,
        "retrying a committed mixed batch adds no message, event, counters or commit");
    Check(retry[0].outcome==PersistPrivateMessageOutcome::kReused&&retry[4].outcome==PersistPrivateMessageOutcome::kIdempotencyConflict,
        "post-commit exact retry and conflict remain distinct");
    Check(b.PersistPrivateMessages({}).empty(),"empty batch is a no-op");
    auto too_many=Fill(65,"too-many-");const auto rejected=b.PersistPrivateMessages(too_many);
    Check(rejected.size()==65&&rejected.front().status==MessageApplicationStatus::kInvalidArgument&&b.Stats().message_commits==4,"batch bound rejects65 without partial writes");
    Check(b.ConfirmReceiver(y[0].message_id,500003).status==MessageApplicationStatus::kPermissionDenied,"batch-created message retains owner-checked receiver permission");
    Check(b.ConfirmReceiver(y[0].message_id,500002).Succeeded()&&b.MarkDialogRead(500002,500001).Succeeded(),"receiver ACK and dialog read operate on batch-created indexes");
    Check(b.ReadUnreadSnapshot(500002,500001).private_unread==0&&b.ReadUserUnreadSnapshot(500002).total_unread==2,"read changes only the authorized peer counters");
}
void Canonical(const std::filesystem::path& root){
    auto opt=Options(root,"canonical");opt.identity_policy="mysql-8.0.40-unicode-ci-padspace-v1";
    // Controlled authority double exercises non-byte canonical identity only;
    // it does not replace the later real MySQL collation integration gate.
    opt.resolve_identity=[](auto from,auto to,const std::string& cid){
        LocalIdentityResult r;r.status=MessageApplicationStatus::kSucceeded;r.canonical_cid=cid;
        for(char& c:r.canonical_cid)if(c>='A'&&c<='Z')c+=32;
        while(!r.canonical_cid.empty()&&r.canonical_cid.back()==' ')r.canonical_cid.pop_back();
        if(to&&(from==500099||to==500099)){r.status=MessageApplicationStatus::kStorageError;r.message="authority actor missing";}return r;
    };
    LocalPrivateStore s(opt);
    const auto out=s.PersistPrivateMessages({Input(500001,500002,"Case "),Input(500001,500002,"case"),Input(500001,500002,"Case "),Input(500099,500002,"absent"),Input(500003,500002,"ok")});
    Check(out[0].Accepted()&&out[1].outcome==PersistPrivateMessageOutcome::kIdempotencyConflict&&out[2].outcome==PersistPrivateMessageOutcome::kReused,
        "canonical aliases arbitrate within batch without changing raw CID conflict semantics");
    Check(!out[3].Accepted()&&out[4].Accepted()&&s.Stats().message_commits==2&&s.FindPrivateMessageByClientMessageId(500001,"case").record.client_message_id=="Case ",
        "authority rejection is per item and canonical lookup preserves original CID");
}
void QuotaAndFence(const std::filesystem::path& root){
    auto opt=Options(root,"quota");opt.max_pending_events=2;LocalPrivateStore s(opt);
    const auto in=std::vector{Input(500001,500002,"q1"),Input(500003,500002,"q2"),Input(500004,500002,"q3"),Input(500001,500002,"q1"),Input(500001,500002,"q1","conflict")};
    const auto out=s.PersistPrivateMessages(in);
    Check(out[0].Accepted()&&out[1].Accepted()&&!out[2].Accepted()&&out[3].outcome==PersistPrivateMessageOutcome::kReused&&out[4].outcome==PersistPrivateMessageOutcome::kIdempotencyConflict,
        "quota rejects only new capacity, preserves staged duplicate/conflict decisions");
    Check(s.Stats().pending_events==2&&s.Stats().message_commits==2&&s.MessageCommitBatches()==1&&!s.FindPrivateMessageByClientMessageId(500004,"q3").found,"quota rejected entry leaves no partial state");
    auto fence=Options(root,"fence");unsigned calls=0;fence.admit_events=[&]{return ++calls!=2;};LocalPrivateStore f(fence);
    auto fo=f.PersistPrivateMessages({Input(500001,500002,"f1"),Input(500003,500002,"f2"),Input(500004,500002,"f3"),Input(500001,500002,"f1")});
    Check(fo[0].Accepted()&&!fo[1].Accepted()&&fo[2].Accepted()&&fo[3].Accepted()&&calls==3&&f.Stats().pending_events==2,"event fencing is applied before every new creation, not before CID reuse");
    auto pins=Options(root,"pins");pins.before_new_message_commit=[](auto from,auto){MessageRepositoryMutationResult r;r.status=from==500003?MessageApplicationStatus::kPermissionDenied:MessageApplicationStatus::kSucceeded;return r;};
    LocalPrivateStore p(pins);auto po=p.PersistPrivateMessages({Input(500001,500002,"p1"),Input(500003,500002,"p2"),Input(500004,500002,"p3")});
    Check(po[0].Accepted()&&!po[1].Accepted()&&po[2].Accepted()&&p.Stats().pending_events==2,"logical actor reference rejection releases its event reservation");
}
void Faults(const std::filesystem::path& root){
    for(bool after:{false,true}){
        auto opt=Options(root,after?"after-write-failure":"before-write-failure");
        LocalPrivateStore s(opt,[&](auto point,auto){if(point==(after?LocalPrivateStore::CommitPoint::kAfterPrivateBatchSyncBeforeReply:LocalPrivateStore::CommitPoint::kBeforePrivateBatchWrite))throw std::runtime_error("injected lost completion");});
        auto old=s.PersistPrivateMessage(500003,500002,"old",1,"body");
        auto out=s.PersistPrivateMessages({Input(500003,500002,"old"),Input(500001,500002,"fresh"),Input(500001,500002,"fresh"),Input(500001,500002,"fresh","changed"),Input(500004,500002,"fresh2")});
        Check(out[0].Accepted()&&out[0].message_id==old.message_id,"existing durable reuse remains valid when another commit fails");
        Check(!out[1].Accepted()&&!out[2].Accepted()&&out[3].status==MessageApplicationStatus::kStorageError&&out[3].message_id==0&&!out[4].Accepted(),
            "uncertain new commit cannot leak created/reused/conflict answers from staged identities");
        Check(s.FindPrivateMessageByClientMessageId(500001,"fresh").found==after&&s.Stats().message_commits==(after?3:1)&&s.Stats().pending_events==(after?3:1),
            "before-write failure rolls back all prepared slots; lost completion retains durable group");
        if(after){auto retry=s.PersistPrivateMessages({Input(500001,500002,"fresh"),Input(500004,500002,"fresh2")});Check(retry[0].outcome==PersistPrivateMessageOutcome::kReused&&retry[1].outcome==PersistPrivateMessageOutcome::kReused,"lost group completion resolves by original CIDs without another write");}
    }
}
void Concurrent(const std::filesystem::path& root,bool exact){
    auto opt=Options(root,exact?"concurrent-exact":"concurrent-striped",true,exact);LocalPrivateStore s(opt);
    std::atomic<unsigned> bad{0};std::atomic<bool> finished{false};std::vector<std::thread> writers;
    for(unsigned t=0;t<4;++t)writers.emplace_back([&,t]{for(unsigned group=0;group<4;++group){
        std::vector<PrivateMessagePersistInput> in;for(unsigned i=0;i<16;++i)in.push_back(Input(t%2?500003:500001,500002,"t"+std::to_string(t)+'-'+std::to_string(group)+'-'+std::to_string(i)));
        auto out=s.PersistPrivateMessages(in);for(auto&r:out){if(!r.Accepted())++bad;else if(!s.ConfirmReceiver(r.message_id,500002).Succeeded())++bad;}
    }});
    std::thread reader([&]{while(!finished.load()){if(!s.MarkDialogRead(500002,500001).Succeeded())++bad;std::this_thread::yield();}});
    for(auto&t:writers)t.join();finished=true;reader.join();
    const auto a=s.ListHistory(500002,500001,0,1000),b=s.ListHistory(500002,500003,0,1000);
    std::uint64_t unread=0;for(const auto& rows:{a.records,b.records})for(const auto& r:rows)unread+=r.delivery_state!=MessageDeliveryState::kRead;
    Check(bad==0&&a.records.size()==128&&b.records.size()==128&&s.CountPending(500002).count==0&&s.ReadUserUnreadSnapshot(500002).total_unread==unread,
        "overlapping multi-writer groups, confirmation and read preserve every record and exact counters");
    Check(s.Stats().message_commits==256&&s.MessageCommitBatches()==16,"concurrent groups execute16 genuine commits for256 new messages");
}
void Drain(const std::filesystem::path& root){
    auto opt=Options(root,"drain");std::promise<void> entered,release;auto gate=release.get_future().share();
    LocalPrivateStore s(opt,[&](auto point,auto){if(point==LocalPrivateStore::CommitPoint::kBeforePrivateBatchWrite){entered.set_value();gate.wait();}});
    auto write=std::async(std::launch::async,[&]{return s.PersistPrivateMessages(Fill(16));});entered.get_future().wait();
    auto close=std::async(std::launch::async,[&]{return s.Shutdown();});
    const auto deadline=std::chrono::steady_clock::now()+5s;while(!s.Stats().closing&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    bool closing=s.Stats().closing;release.set_value();const auto out=write.get();const bool closed=close.get();
    Check(closing&&closed&&out.size()==16&&out.front().Accepted()&&out.back().Accepted(),"shutdown waits for an admitted atomic batch before closing the engine");
    LocalPrivateStore reopen(opt);Check(reopen.ListHistory(500001,500002,0,64).records.size()==16&&reopen.Stats().pending_events==16,"graceful reopen retains the complete committed group");
    auto rejected=s.PersistPrivateMessages(Fill(2,"closed-"));Check(!rejected[0].Accepted()&&!rejected[1].Accepted(),"closed store rejects every new batch item");
}
void Adapter(const std::filesystem::path& root){
    auto opt=Options(root,"adapter");LocalPrivateStore store(opt);tinyimx::tests::ReadyBatchNoopRepository groups;
    LocalMessageRepositoryAdapter adapter(&store,&groups);MessageRepositoryPort* port=&adapter;
    const auto out=port->PersistPrivateMessages(Fill(64));
    Check(out.size()==64&&out.front().Accepted()&&out.back().Accepted()&&store.Stats().message_commits==64&&store.MessageCommitBatches()==1,
        "virtual native adapter overrides compatibility loop and reaches one real64-message commit");
}
}
int main(int argc,char**argv){
    try{
        if(argc!=3)return 2;const std::filesystem::path root=argv[1];const std::string mode=argv[2];
        Check(root.is_absolute()&&std::filesystem::is_directory(root)&&std::filesystem::canonical(root)==root&&
            (root.string().starts_with("/dev/shm/perf588-")||root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf588-")||root.string().starts_with("/mnt/tinyimx-benchmark/perf943-contracts-")),"explicit new owned test root");
        if(mode=="contracts"){
            Check(!LocalPrivateStoreOptions{}.persist_batch_enable,"native batch default remains disabled");
            Oracle(root,true);Oracle(root,false);Canonical(root);QuotaAndFence(root);Faults(root);Concurrent(root,true);Concurrent(root,false);Drain(root);Adapter(root);
            std::cout<<"NATIVE_PRIVATE_BATCH_CONTRACTS_PASS checks="<<checks<<'\n';return 0;
        }
        const bool before=mode=="crash-before"||mode=="verify-before";
        const bool verify=mode=="verify-before"||mode=="verify-after";
        if(!before&&mode!="crash-after"&&mode!="verify-after")return 2;
        auto opt=Options(root,before?"crash-before":"crash-after");
        if(!verify){
            LocalPrivateStore s(opt,[&](auto point,auto mid){if(point==(before?LocalPrivateStore::CommitPoint::kBeforePrivateBatchWrite:LocalPrivateStore::CommitPoint::kAfterPrivateBatchSyncBeforeReply)){
                std::cout<<"NATIVE_BATCH_REAL_CRASH_READY "<<mode<<' '<<mid<<std::endl;std::raise(SIGSTOP);
            }});
            (void)s.PersistPrivateMessages(Fill(16));return 3;
        }
        LocalPrivateStore s(opt);const auto history=s.ListHistory(500001,500002,0,64);const auto events=s.ListPendingEvents(64);
        Check(history.records.size()==(before?0:16)&&events.events.size()==(before?0:16)&&s.CountPending(500002).count==(before?0:16),"SIGKILL boundary recovers none or the complete group with event and counter indexes");
        if(!before){auto retry=s.PersistPrivateMessages(Fill(16));for(const auto& r:retry)Check(r.Accepted()&&r.outcome==PersistPrivateMessageOutcome::kReused,"lost-reply batch retry retains original durable MID");Check(s.Stats().message_commits==0&&s.Stats().pending_events==16,"recovery retry emits no duplicate event or message");}
        std::cout<<"NATIVE_PRIVATE_BATCH_RECOVERY_PASS "<<mode<<'\n';return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
