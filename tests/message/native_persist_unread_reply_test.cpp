#include "services/message/local/LocalPrivateStore.h"
#include "services/message/local/NativePersistUnreadReplyPolicy.h"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
unsigned checks=0;
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);++checks;std::cout<<"PASS "<<why<<'\n';}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="ack-snapshot:"+root.filename().string()+":"+name;
    o.message_events_topic="codex_perf1598_events";o.first_message_id=7100000000000000ULL;
    o.initial_user_ids={500001,500002,500003};o.block_cache_bytes=1024*1024;
    o.exact_user_locking=true;o.persist_batch_enable=true;return o;
}
PrivateMessagePersistInput Input(std::uint64_t from,std::string cid){
    PrivateMessagePersistInput r;r.from_user_id=from;r.to_user_id=500002;
    r.client_message_id=std::move(cid);r.message_type=1;r.content="body";return r;
}
void Snapshot(const MessageRepositoryPersistResult& r,std::uint64_t dialog,std::uint64_t total,bool enabled){
    Check(r.Accepted()&&r.outcome==PersistPrivateMessageOutcome::kCreated,"snapshot result is a successful Created commit");
    Check(r.committed_unread.has_value()==enabled,"feature OFF carries no new metadata");
    if(enabled){const auto& s=*r.committed_unread;Check(s.ValidFor(r.message_id,r.record.to_user_id,r.record.from_user_id)&&s.private_unread==dialog&&s.total_unread==total,
        "metadata identifies exact durable MID/receiver/peer and committed counters");}
}
void Policy(){
    using tinyimx::CommittedUnreadSnapshot;using tinyimx::CanUseCommittedUnreadSnapshot;
    std::optional<CommittedUnreadSnapshot> s=CommittedUnreadSnapshot{7,500002,500001,1,2};
    Check(CanUseCommittedUnreadSnapshot(false,true,s,7,500002,500001),"Created native reply can avoid a redundant read");
    Check(!CanUseCommittedUnreadSnapshot(true,true,s,7,500002,500001),"writer-enabled Gateway retains idempotent projection repair");
    Check(!CanUseCommittedUnreadSnapshot(false,false,s,7,500002,500001),"reused CID never bypasses uncertainty recovery");
    Check(!CanUseCommittedUnreadSnapshot(false,true,std::nullopt,7,500002,500001),"SQL and older peer replies retain legacy path");
    Check(!CanUseCommittedUnreadSnapshot(false,true,s,8,500002,500001)&&!s->ValidFor(7,500003,500001)&&!s->ValidFor(7,500002,500003),"MID and actor mismatches cannot reuse metadata");
    s->private_unread=0;Check(!s->ValidFor(7,500002,500001),"a Created unread increment cannot be zero");
    s->private_unread=3;Check(!s->ValidFor(7,500002,500001),"private unread may not exceed total");
    s->private_unread=1;s->total_unread=std::numeric_limits<std::uint64_t>::max();
    Check(!s->ValidFor(7,500002,500001),"desktop signed counter overflow retains legacy path");
}
void Contracts(const std::filesystem::path& root,bool enabled){
    Policy();
    {
        LocalPrivateStore s(Options(root,"scalar"));auto a=s.PersistPrivateMessage(500001,500002,"one",1,"body");Snapshot(a,1,1,enabled);
        auto reused=s.PersistPrivateMessage(500001,500002,"one",1,"body");
        auto conflict=s.PersistPrivateMessage(500001,500002,"one",1,"changed");
        Check(reused.outcome==PersistPrivateMessageOutcome::kReused&&!reused.committed_unread&&conflict.outcome==PersistPrivateMessageOutcome::kIdempotencyConflict&&!conflict.committed_unread,
            "reused and conflicting CIDs do not acquire Created snapshots");
        Check(s.MarkDialogRead(500002,500001).Succeeded(),"authorized read commits independently");
        Check(s.ReadUnreadSnapshot(500002,500001).total_unread==0,"snapshot delivery never rewrites current durable read state");
        Snapshot(s.PersistPrivateMessage(500003,500002,"other",1,"body"),1,1,enabled);
        Snapshot(s.PersistPrivateMessage(500001,500002,"after-read",1,"body"),1,2,enabled);
        Check(s.Shutdown(),"scalar store drains after committed metadata");
    }
    {
        LocalPrivateStore s(Options(root,"batch"));
        auto r=s.PersistPrivateMessages({Input(500001,"a"),Input(500003,"b"),Input(500001,"c"),Input(500001,"a")});
        Snapshot(r[0],2,3,enabled);Snapshot(r[1],1,3,enabled);Snapshot(r[2],2,3,enabled);
        Check(r[3].outcome==PersistPrivateMessageOutcome::kReused&&!r[3].committed_unread,"in-batch CID reuse retains recovery path");
        Check(s.Stats().message_commits==3&&s.MessageCommitBatches()==1,"metadata adds no message or WAL commit");
        Check(s.ConfirmReceiver(r[0].message_id,500003).status==MessageApplicationStatus::kPermissionDenied,"receiver confirmation remains owner checked");
        Check(s.Shutdown(),"batch store drains before reopen");
    }
    {
        LocalPrivateStore s(Options(root,"batch"));
        Check(s.ReadUnreadSnapshot(500002,500001).private_unread==2&&s.ReadUserUnreadSnapshot(500002).total_unread==3,"all unread counters survive reopen unchanged");
        auto r=s.PersistPrivateMessages({Input(500001,"a"),Input(500003,"b")});
        Check(r[0].outcome==PersistPrivateMessageOutcome::kReused&&r[1].outcome==PersistPrivateMessageOutcome::kReused&&!r[0].committed_unread&&!r[1].committed_unread,
            "reopen retries resolve original durable CIDs without fabricating new metadata");
    }
    for(bool after:{false,true}){
        auto opt=Options(root,after?"fault-after":"fault-before");
        LocalPrivateStore s(opt,[&](auto point,auto){if(point==(after?LocalPrivateStore::CommitPoint::kAfterPrivateBatchSyncBeforeReply:LocalPrivateStore::CommitPoint::kBeforePrivateBatchWrite))throw std::runtime_error("owned lost completion");});
        auto r=s.PersistPrivateMessages({Input(500001,"f1"),Input(500003,"f2")});
        Check(!r[0].Accepted()&&!r[1].Accepted()&&!r[0].committed_unread&&!r[1].committed_unread,"uncertain completion never leaks provisional success metadata");
        Check(s.FindPrivateMessageByClientMessageId(500001,"f1").found==after,"actual sync boundary determines CID recovery");
    }
}
}
int main(int argc,char**argv){try{
    Check(argc==4,"owned test root, feature0/1 and mode are explicit");
    const std::filesystem::path root(argv[1]);const std::string flag(argv[2]),mode(argv[3]);
    Check((flag=="0"||flag=="1")&&root.is_absolute()&&root.parent_path()=="/mnt/tinyimx-benchmark"&&root.filename().string().starts_with("perf1598-unread-")&&std::filesystem::canonical(root)==root,
        "isolated canonical fixture scope only");
    Check(::setenv("TINYIMX_NATIVE_PERSIST_UNREAD_REPLY_ENABLE",flag.c_str(),1)==0,"select exact default-OFF candidate flag");
    const bool enabled=flag=="1";
    if(mode=="contracts"){Contracts(root,enabled);std::cout<<"NATIVE_COMMITTED_UNREAD_CONTRACTS_PASS checks="<<checks<<'\n';return 0;}
    const bool before=mode=="crash-before"||mode=="verify-before";
    Check(before||mode=="crash-after"||mode=="verify-after","exact real process boundary");
    auto opt=Options(root,before?"process-before":"process-after");
    if(mode.starts_with("crash-")){
        LocalPrivateStore s(opt,[&](auto point,auto mid){if(point==(before?LocalPrivateStore::CommitPoint::kBeforePrivateBatchWrite:LocalPrivateStore::CommitPoint::kAfterPrivateBatchSyncBeforeReply)){
            std::cout<<"UNREAD_REAL_CRASH_READY "<<mid<<std::endl;std::raise(SIGSTOP);}});
        (void)s.PersistPrivateMessages({Input(500001,"death1"),Input(500003,"death2")});return 3;
    }
    LocalPrivateStore s(opt);
    const auto recovered=s.FindPrivateMessageByClientMessageId(500001,"death1");
    Check(recovered.found==!before&&s.ReadUserUnreadSnapshot(500002).total_unread==(before?0:2),"actual killed process reopens with atomic message/unread state");
    auto r=s.PersistPrivateMessages({Input(500001,"death1"),Input(500003,"death2")});
    if(before){Snapshot(r[0],1,2,enabled);Snapshot(r[1],1,2,enabled);}
    else Check(r[0].outcome==PersistPrivateMessageOutcome::kReused&&r[1].outcome==PersistPrivateMessageOutcome::kReused&&!r[0].committed_unread&&!r[1].committed_unread,"post-sync death resolves as Reused with legacy repair path");
    std::cout<<"NATIVE_COMMITTED_UNREAD_PROCESS_RECOVERY_PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
