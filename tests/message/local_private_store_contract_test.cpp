#include "services/message/local/LocalPrivateStore.h"
#include "services/eventing/EventCodec.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& name) {
    LocalPrivateStoreOptions out;out.directory=(root/name).string();out.storage_identity="owned-perf104-"+root.filename().string()+':'+name;
    out.message_events_topic="codex_perf104_events_20261020_r3";out.first_message_id=7070000000000000ULL;
    out.initial_user_ids={500001,500002,500003,500004};out.block_cache_bytes=8*1024*1024;
    const auto exact=std::getenv("TINYIMX_LOCAL_STORE_EXACT_USERS_ENABLE");
    out.exact_user_locking=exact&&std::string(exact)=="1";return out;
}
void Contracts(const std::filesystem::path& root) {
    const auto options=Options(root,"single");std::uint64_t first=0;
    {
        LocalPrivateStore s(options);
        Check(!s.PersistPrivateMessage(0,500002,"a",1,"body").Accepted(),"zero sender rejected");
        Check(!s.PersistPrivateMessage(500001,500001,"a",1,"body").Accepted(),"self dialog rejected");
        Check(!s.PersistPrivateMessage(500001,999999,"a",1,"body").Accepted(),"unknown identity rejected before native write");
        Check(!s.PersistPrivateMessage(500001,500002,std::string(65,'x'),1,"body").Accepted(),"oversize CID rejected");
        Check(!s.PersistPrivateMessage(500001,500002,"a",0,"body").Accepted(),"invalid type rejected");
        Check(!s.PersistPrivateMessage(500001,500002,"a",1,"").Accepted(),"empty body rejected");
        Check(s.Stats().pending_events==0&&s.CountPending(500002).count==0,"rejected writes leave no state or events");
        std::string body="quote';蜜蜂";body+='\0';body+="tail";
        const auto one=s.PersistPrivateMessage(500001,500002,"same-cid",1,body);first=one.message_id;
        Check(one.Accepted()&&one.outcome==PersistPrivateMessageOutcome::kCreated&&first>=options.first_message_id&&one.record.content==body,"full message committed with embedded NUL and Unicode");
        auto events=s.ListPendingEvents(256);
        Check(events.Succeeded()&&events.events.size()==1&&events.events[0].event_id=="message.created.v1:"+std::to_string(first)&&tinyimx::eventing::EventCodec::Decode(events.events[0].body).success,"same atomic commit exposes valid original event envelope");
        Check(s.PersistPrivateMessage(500001,500002,"same-cid",1,body).outcome==PersistPrivateMessageOutcome::kReused,"CID retry reuses durable record");
        Check(s.PersistPrivateMessage(500001,500003,"same-cid",1,body).outcome==PersistPrivateMessageOutcome::kIdempotencyConflict,"CID destination change conflicts");
        Check(s.PersistPrivateMessage(500001,500002,"same-cid",1,"different").outcome==PersistPrivateMessageOutcome::kIdempotencyConflict&&s.Stats().pending_events==1,"CID content conflict creates no second event");
        const auto two=s.PersistPrivateMessage(500003,500002,"same-cid",1,"second peer");
        const auto reverse=s.PersistPrivateMessage(500002,500001,"reverse",1,"reverse");
        Check(two.Accepted()&&reverse.Accepted()&&two.message_id>first&&reverse.message_id>two.message_id,"CID sender namespace and MID ordering preserved");
        const auto history=s.ListHistory(500002,500001,0,100);
        Check(history.Succeeded()&&history.records.size()==2&&history.records[0].message_id==first&&history.records[1].message_id==reverse.message_id,"newest dialog window returns both directions in ascending RPC MID order");
        const auto newest=s.ListHistory(500002,500001,0,1);
        Check(newest.Succeeded()&&newest.records.size()==1&&newest.records[0].message_id==reverse.message_id,"bounded history selects newest message before ascending presentation");
        Check(s.ListHistory(500003,500001,0,100).records.empty(),"other dialog cannot read this private history");
        Check(s.ListHistory(500001,500002,reverse.message_id,100).records.size()==1,"history cursor is exclusive");
        auto unread=s.ReadUnreadSnapshot(500002,500001);
        Check(unread.Succeeded()&&unread.private_unread==1&&unread.total_unread==2,"dialog and total unread share one snapshot");
        Check(s.ConfirmReceiver(first,500003).status==MessageApplicationStatus::kPermissionDenied,"foreign receiver confirmation rejected");
        Check(s.ConfirmReceiver(1,500002).status==MessageApplicationStatus::kNotFound,"missing confirmation remains not found");
        Check(s.ConfirmReceiver(first,500002).affected_rows==1&&s.ConfirmReceiver(first,500002).affected_rows==0,"receiver transition is durable and idempotent");
        Check(s.GetPrivateMessage(first).record.delivery_state==MessageDeliveryState::kReceiverConfirmed&&s.ReadUnreadSnapshot(500002,500001).total_unread==2,"receiver ACK does not mean message read");
        Check(s.CountPending(500002).count==1&&s.ListPendingAfter(500002,0,100).records[0].message_id==two.message_id,"confirmed message removed from pending index atomically");
        const auto read=s.MarkDialogRead(500002,500001);
        Check(read.Succeeded()&&read.affected_rows==1&&s.GetPrivateMessage(first).record.delivery_state==MessageDeliveryState::kRead,"dialog read changes only incoming peer messages");
        Check(s.ConfirmReceiver(first,500002).Succeeded()&&s.ConfirmReceiver(first,500002).affected_rows==0&&s.GetPrivateMessage(first).record.delivery_state==MessageDeliveryState::kRead,"late receiver ACK cannot downgrade read state");
        unread=s.ReadUnreadSnapshot(500002,500001);
        Check(unread.private_unread==0&&unread.total_unread==1,"read subtracts exact dialog count and preserves other peer");
        Check(s.MarkDialogRead(500002,500001).affected_rows==0&&s.Stats().pending_events==4,"repeated read creates no extra event");
        const auto later=s.PersistPrivateMessage(500001,500002,"after-read",1,"later");
        unread=s.ReadUnreadSnapshot(500002,500001);
        Check(later.Accepted()&&unread.private_unread==1&&unread.total_unread==2,"new message after read has fresh unread state");
        const auto conversations=s.ListConversations(500002,100);
        Check(conversations.Succeeded()&&conversations.records.size()==2&&conversations.records[0].last_message_id==later.message_id,"conversation last-message index follows committed MID");
        const auto recipients=s.ListPendingRecipientsAfter(0,1);
        Check(recipients.Succeeded()&&recipients.recipient_user_ids.size()==1&&recipients.has_more,"bounded pending recipient discovery preserves continuation");
        events=s.ListPendingEvents(256);std::vector<std::string> ids;
        for(const auto&e:events.events)ids.push_back(e.event_id);
        Check(ids.size()==5&&s.MarkEventsPublished({ids[0],ids[1],ids[0]}).affected_rows==2&&s.MarkEventsPublished({ids[0]}).affected_rows==0,"publication progress idempotently deletes only unique pending events");
        Check(s.SyncPublicationProgress().Succeeded()&&s.Stats().pending_events==3,"publication progress explicitly synced");
        const auto stats=s.Stats();Check(stats.engine_wal_syncs>=stats.message_commits+stats.confirmation_commits+stats.read_commits,"critical commits execute real engine WAL synchronization");
        bool second_owner=false;try{LocalPrivateStore conflict(options);}catch(const std::exception&){second_owner=true;}
        Check(second_owner&&s.GetPrivateMessage(first).Found(),"second storage owner rejected without mutating existing owner");
        Check(s.Shutdown()&&s.GetPrivateMessage(first).status==MessageApplicationStatus::kStorageError,"shutdown syncs and rejects new operations");
    }
    {
        LocalPrivateStore s(options);const auto found=s.FindPrivateMessageByClientMessageId(500001,"same-cid");
        Check(found.Found()&&found.record.message_id==first&&found.record.delivery_state==MessageDeliveryState::kRead&&s.Stats().pending_events==3,"reopen retains MID, state, indexes and publication progress");
        auto wrong=options;wrong.storage_identity+="foreign";bool rejected=false;
        try{LocalPrivateStore foreign(wrong);}catch(const std::exception&){rejected=true;}
        Check(rejected&&s.GetPrivateMessage(first).Found(),"namespace mismatch fails before touching native files");
    }
    {
        auto quota=Options(root,"quota");quota.max_pending_events=1;LocalPrivateStore s(quota);
        auto result=s.PersistPrivateMessage(500001,500002,"q1",1,"body");
        Check(result.Accepted()&&s.PersistPrivateMessage(500001,500002,"q1",1,"body").Accepted(),"CID retry remains available when pending-event quota is full");
        const auto rejected=s.PersistPrivateMessage(500001,500002,"q2",1,"body");
        Check(!rejected.Accepted()&&rejected.status==MessageApplicationStatus::kFailedPrecondition&&
              rejected.message=="local pending event capacity reached"&&!s.FindPrivateMessageByClientMessageId(500001,"q2").found,
              "pending-event capacity returns precise failed-precondition and creates no partial message");
        Check(!s.MarkDialogRead(500002,500001).Succeeded()&&s.GetPrivateMessage(result.message_id).record.delivery_state==MessageDeliveryState::kPending&&s.ReadUnreadSnapshot(500002,500001).private_unread==1,"read-event quota failure leaves message and unread unchanged");
    }
    {
        auto opt=Options(root,"concurrent");LocalPrivateStore s(opt);std::atomic<unsigned> bad{0};std::atomic<bool> finished{false};
        std::vector<std::thread> writers;
        for(unsigned t=0;t<4;++t)writers.emplace_back([&,t]{for(unsigned i=0;i<100;++i)if(!s.PersistPrivateMessage(500001,500002,"c-"+std::to_string(t)+'-'+std::to_string(i),1,"body").Accepted())++bad;});
        std::thread reader([&]{while(!finished.load())if(!s.MarkDialogRead(500002,500001).Succeeded())++bad;});
        for(auto&t:writers)t.join();finished=true;reader.join();
        auto all=s.ListHistory(500002,500001,0,1000);std::uint64_t unread=0,pending=0;
        for(const auto&m:all.records){unread+=m.delivery_state==MessageDeliveryState::kPending||m.delivery_state==MessageDeliveryState::kReceiverConfirmed;pending+=m.delivery_state==MessageDeliveryState::kPending;}
        const auto count=s.ReadUnreadSnapshot(500002,500001);
        Check(bad==0&&all.records.size()==400&&count.private_unread==unread&&count.total_unread==unread&&s.CountPending(500002).count==pending,"concurrent persist/read linearizes records, pending and exact counters");
        std::vector<std::thread> retries;std::atomic<unsigned> created{0},reused{0};
        for(unsigned i=0;i<8;++i)retries.emplace_back([&]{auto x=s.PersistPrivateMessage(500003,500002,"one-cid",1,"once");if(x.outcome==PersistPrivateMessageOutcome::kCreated&&x.Accepted())++created;else if(x.outcome==PersistPrivateMessageOutcome::kReused&&x.Accepted())++reused;else++bad;});
        for(auto&t:retries)t.join();Check(bad==0&&created==1&&reused==7,"eight concurrent CID retries create one durable logical message");
    }
    {
        auto opt=Options(root,"drain");std::promise<void> entered,release;auto gate=release.get_future().share();std::atomic<bool> once{false};
        LocalPrivateStore s(opt,[&](auto point,std::uint64_t){if(point==LocalPrivateStore::CommitPoint::kBeforeWrite&&!once.exchange(true)){entered.set_value();gate.wait();}});
        auto write=std::async(std::launch::async,[&]{return s.PersistPrivateMessage(500001,500002,"drain",1,"body");});
        entered.get_future().wait();auto close=std::async(std::launch::async,[&]{return s.Shutdown();});
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!s.Stats().closing&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
        const bool closing=s.Stats().closing;release.set_value();const auto result=write.get();const auto closed=close.get();
        Check(closing&&result.Accepted()&&closed,"shutdown waits for admitted synchronous write before closing engine");
        LocalPrivateStore reopened(opt);Check(reopened.FindPrivateMessageByClientMessageId(500001,"drain").Found(),"admitted write survives graceful drain and reopen");
    }
}
}
int main(int argc,char**argv) {
    try {
        if(argc!=3)return 2;const std::filesystem::path root=argv[1];const std::string mode=argv[2];
        const auto path=root.string();
        Check(root.is_absolute()&&(path.starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf104-")||path.starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf105-"))&&std::filesystem::is_directory(root)&&std::filesystem::canonical(root)==root,"explicit owned physical-disk fixture root");
        if(mode=="contracts")Contracts(root);
        else {
            const bool before=mode=="crash-before"||mode=="verify-before";const bool verify=mode=="verify-before"||mode=="verify-after";
            if(!before&&mode!="crash-after"&&mode!="verify-after")return 2;
            auto options=Options(root,before?"crash-before":"crash-after");
            if(!verify) {
                LocalPrivateStore s(options,[&](auto point,std::uint64_t mid){if(point==(before?LocalPrivateStore::CommitPoint::kBeforeWrite:LocalPrivateStore::CommitPoint::kAfterSyncBeforeReply)){
                    std::cout<<"OWNED_LOCAL_STORE_CRASH_READY "<<mode<<' '<<mid<<std::endl;std::raise(SIGSTOP);
                }});
                (void)s.PersistPrivateMessage(500001,500002,"crash-cid",1,"owned crash record");return 3;
            }
            LocalPrivateStore s(options);auto found=s.FindPrivateMessageByClientMessageId(500001,"crash-cid");auto events=s.ListPendingEvents(256);
            if(before)Check(found.Succeeded()&&!found.found&&events.events.empty()&&s.CountPending(500002).count==0,"SIGKILL before atomic write leaves no message/event/counter");
            else {
                Check(found.Found()&&events.events.size()==1&&s.CountPending(500002).count==1,"SIGKILL after sync before reply recovers exact message/event/pending state");
                auto retry=s.PersistPrivateMessage(500001,500002,"crash-cid",1,"owned crash record");
                Check(retry.Accepted()&&retry.outcome==PersistPrivateMessageOutcome::kReused&&retry.message_id==found.record.message_id&&s.ListPendingEvents(256).events.size()==1,"lost-reply retry preserves original MID and one event after process death");
            }
        }
        std::cout<<"LOCAL_PRIVATE_STORE_CONTRACTS_PASS "<<mode<<'\n';return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
