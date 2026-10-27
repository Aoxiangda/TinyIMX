#include "services/message/local/LocalMessageRepositoryAdapter.h"
#include "services/message/local/LocalUnreadProjectionSource.h"
#include "services/message/local/MySqlLegacyIdentity.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/repository/MessageRepository.h"
#include "services/outbox/OutboxRepository.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool value,const char* name){if(!value)throw std::runtime_error(name);std::cout<<"PASS "<<name<<'\n';}
template<class A,class B>void Same(const A&a,const B&b,const char* name){Check(a.status==b.status&&a.message==b.message,name);}
}
int main(int argc,char**argv) {
    try {
        if(argc!=3&&argc!=4)return 2;
        const std::string mode=argc==4?argv[3]:"contracts";
        if(mode!="contracts"&&mode!="crash-batch-before"&&mode!="crash-batch-after"&&mode!="verify-batch-before"&&mode!="verify-batch-after")return 2;
        const std::filesystem::path root=argv[2];
        Check(root.is_absolute()&&root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf105-")&&std::filesystem::canonical(root)==root,"owned application fixture root");
        Config cfg;Check(cfg.LoadFromFile(argv[1])&&cfg.MySql().database=="codex_perf104_20261020_r1","owned existing SQL resource schema");
        Check(Logger::Instance().Init(cfg.Logger()),"test logger initialization");
        MySqlConnectionPool pool;auto sql_cfg=cfg.MySql();sql_cfg.pool_size=4;Check(pool.Initialize(sql_cfg),"bounded real SQL pool");
        MessageRepository messages(&pool);outbox::OutboxRepository events(&pool);
        MessageRepositoryAdapter sql(&messages,&pool,&events,"codex_perf105_events_20261020_r1");MessageApplicationService old(&sql);
        auto oracle=std::make_shared<MySqlLegacyIdentity>(&pool);
        LocalPrivateStoreOptions options;options.directory=(root/"application").string();options.storage_identity="perf105-app-"+root.filename().string();
        options.message_events_topic="codex_perf105_events_20261020_r1";options.first_message_id=7080000000000000ULL;
        options.initial_user_ids={500001,500002,500003,500004};options.block_cache_bytes=8*1024*1024;options.exact_user_locking=true;
        options.identity_policy=oracle->Policy();options.resolve_identity=[oracle](auto from,auto to,const auto& cid){return oracle->Resolve(from,to,cid);};
        LocalPrivateStore::CommitObserver observer;
        if(mode.starts_with("crash-"))observer=[&](auto point,std::uint64_t mid){
            const auto target=mode=="crash-batch-before"?LocalPrivateStore::CommitPoint::kBeforeConfirmationBatchWrite:LocalPrivateStore::CommitPoint::kAfterConfirmationBatchSyncBeforeReply;
            if(point==target){std::cout<<"OWNED_LOCAL_BATCH_CRASH_READY "<<mode<<' '<<mid<<std::endl;std::raise(SIGSTOP);}
        };
        LocalPrivateStore store(options,observer);LocalMessageRepositoryAdapter adapter(&store,&sql);MessageApplicationService app(&adapter);LocalUnreadProjectionSource reader(&store);
        if(mode!="contracts") {
            std::vector<std::uint64_t> mids;
            for(unsigned i=0;i<3;++i) {
                const auto cid="p105-atomic-batch-"+std::to_string(i);
                if(mode.starts_with("crash-")){
                    const auto result=app.PersistPrivateMessage(500001,500002,cid,1,"batch crash record");Check(result.Created(),"crash fixture persisted");mids.push_back(result.message_id);
                }else {
                    const auto found=store.FindPrivateMessageByClientMessageId(500001,cid);Check(found.Found(),"recovered batch message identity");mids.push_back(found.record.message_id);
                    const auto expected=mode=="verify-batch-before"?MessageDeliveryState::kPending:MessageDeliveryState::kReceiverConfirmed;
                    Check(found.record.delivery_state==expected,"batch recovery is wholly before or wholly after synchronized state");
                    const auto retry=app.PersistPrivateMessage(500001,500002,cid,1,"batch crash record");Check(retry.Reused()&&retry.message_id==found.record.message_id,"batch crash retry preserves original message");
                }
            }
            if(mode.starts_with("crash-")){(void)app.ConfirmReceiverBatch(500002,mids);return 3;}
            Check(store.CountPending(500002).count==(mode=="verify-batch-before"?3:0)&&store.Stats().pending_events==3&&reader.LoadDialogSnapshot(500002,500001).value.private_unread==3,"batch recovery preserves pending indexes, unread and original events");
            if(mode=="verify-batch-after")Check(app.ConfirmReceiverBatch(500002,mids).Succeeded()&&app.ConfirmReceiverBatch(500002,mids).affected_rows==0,"lost batch reply retry is idempotent");
            Check(store.Shutdown(),"recovered batch owner shuts down");pool.Shutdown();Logger::Instance().Shutdown();
            std::cout<<"LOCAL_MESSAGE_BATCH_RECOVERY_PASS "<<mode<<'\n';return 0;
        }
        const std::string tag="p105-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-";
        std::cout<<"OWNED_LOCAL_PARITY_TAG "<<tag<<'\n';
        const std::vector<std::pair<std::string,std::string>> cases={
            {"Case","case"},{"é","e"},{"é","e"},{"space","space "},{"nbsp","nbsp "},{"ß","ss"},
            {std::string("ab\0",3),"ab"},{std::string("a\0b",3),"ab"},{"ﬁ","fi"}};
        unsigned number=0;
        for(const auto&[a,b]:cases) {
            const auto prefix=tag+std::to_string(++number)+'-';const auto first=prefix+a,changed=prefix+b;
            const auto s0=old.PersistPrivateMessage(500001,500002,first,1,"same immutable body");
            const auto n0=app.PersistPrivateMessage(500001,500002,first,1,"same immutable body");
            Check(s0.status==n0.status&&s0.outcome==n0.outcome&&n0.Created(),"SQL/native first durable identity matches");
            const auto s1=old.PersistPrivateMessage(500001,500002,changed,1,"same immutable body");
            const auto n1=app.PersistPrivateMessage(500001,500002,changed,1,"same immutable body");
            Check(s1.status==n1.status&&s1.outcome==n1.outcome&&n1.Conflict()&&n1.message_id==n0.message_id,"real UCA case/accent/PAD SPACE/expansion/NUL conflict matches");
            const auto s2=old.PersistPrivateMessage(500001,500002,first,1,"same immutable body");
            const auto n2=app.PersistPrivateMessage(500001,500002,first,1,"same immutable body");
            Check(s2.status==n2.status&&s2.outcome==n2.outcome&&n2.Reused()&&n2.message_id==n0.message_id,"exact CID retry keeps one durable message");
            Check(old.ResolvePrivateMessage(500001,500002,changed,1,"same immutable body").status==app.ResolvePrivateMessage(500001,500002,changed,1,"same immutable body").status,"legacy nonexact CID resolution status preserved");
            Check(app.ResolvePrivateMessage(500001,500002,first,1,"same immutable body").outcome==ResolvePrivateMessageOutcome::kMatchedDurable,"exact native resolve uses full durable record");
        }
        auto compare_reject=[&](std::uint64_t from,std::uint64_t to,const std::string&cid,const std::string&body){
            const auto before=store.Stats().pending_events;
            const auto s=old.PersistPrivateMessage(from,to,cid,1,body),n=app.PersistPrivateMessage(from,to,cid,1,body);
            Check(s.status==n.status&&s.status!=MessageApplicationStatus::kSucceeded&&store.Stats().pending_events==before,"SQL/native rejection status and no-side-effect match");
        };
        compare_reject(500001,999999999,tag+"unknown","body");
        compare_reject(500001,500002,std::string(65,'x'),"body");
        compare_reject(500001,500002,tag+"too-large",std::string(65536,'x'));
        compare_reject(500001,500002,tag+"bad-utf8",std::string(1,char(0xff)));
        compare_reject(500001,500002,std::string(1,char(0xff)),"body");
        const auto wide=app.PersistPrivateMessage(500001,500002,tag+"text-limit",1,std::string(65535,'x'));
        const auto wide_sql=old.PersistPrivateMessage(500001,500002,tag+"text-limit",1,std::string(65535,'x'));
        Check(wide.Created()&&wide_sql.Created(),"exact TEXT byte boundary accepted by both");
        const auto conflict=app.PersistPrivateMessage(500001,500002,tag+"text-limit",1,std::string(65536,'x'));
        Check(conflict.outcome==old.PersistPrivateMessage(500001,500002,tag+"text-limit",1,std::string(65536,'x')).outcome&&conflict.Conflict(),"existing CID conflict precedes oversize new-body failure");
        Check(app.PersistPrivateMessage(500005,500002,tag+"new-authoritative-actor",1,"registered outside bootstrap").Created(),"current SQL identity can introduce actor outside bootstrap lock list");
        Check(app.CountPending(900000001).Succeeded()&&app.CountPending(900000001).count==0,"unknown query actor returns empty like SQL");
        const auto full_history=app.ListHistory(500002,500001,0,50);
        const auto latest_history=app.ListHistory(500002,500001,0,3);
        Check(full_history.Succeeded()&&full_history.messages.size()==10&&latest_history.Succeeded()&&latest_history.has_more&&latest_history.messages.size()==3,"history application preserves bounded latest window and lookahead");
        bool same_window=true;
        for(std::size_t i=0;i<3;++i)same_window=same_window&&latest_history.messages[i].message_id==full_history.messages[7+i].message_id;
        Check(same_window,"history lookahead removes oldest extra row rather than newest message");
        const auto previous_history=app.ListHistory(500002,500001,latest_history.messages.front().message_id,3);
        same_window=previous_history.Succeeded()&&previous_history.has_more&&previous_history.messages.size()==3;
        for(std::size_t i=0;same_window&&i<3;++i)same_window=previous_history.messages[i].message_id==full_history.messages[4+i].message_id;
        Check(same_window,"exclusive history cursor returns previous contiguous window without gap or overlap");

        auto add=[&](const std::string&cid,std::uint64_t to=500002){auto x=app.PersistPrivateMessage(500003,to,tag+cid,1,"batch body");Check(x.Created(),"batch fixture persisted");return x.message_id;};
        const auto one=add("b1"),two=add("b2"),three=add("b3"),foreign=add("foreign",500004);
        Check(app.ConfirmReceiverBatch(500002,{one,foreign}).status==MessageApplicationStatus::kPermissionDenied&&store.GetPrivateMessage(one).record.delivery_state==MessageDeliveryState::kPending,"batch rejects foreign owner before any mutation");
        Check(app.ConfirmReceiverBatch(500002,{one,1}).status==MessageApplicationStatus::kNotFound&&store.GetPrivateMessage(one).record.delivery_state==MessageDeliveryState::kPending,"batch rejects missing MID before any mutation");
        Check(app.ConfirmReceiverBatch(500002,{one,one}).status==MessageApplicationStatus::kInvalidArgument&&store.GetPrivateMessage(one).record.delivery_state==MessageDeliveryState::kPending,"batch rejects duplicate MID before any mutation");
        const auto before=store.Stats();const auto done=app.ConfirmReceiverBatch(500002,{one,two,three});
        Check(done.Succeeded()&&done.affected_rows==3&&store.Stats().confirmation_commits==before.confirmation_commits+1,"batch commits all three receiver states in one synchronous write");
        Check(app.ConfirmReceiverBatch(500002,{one,two,three}).Succeeded()&&app.ConfirmReceiverBatch(500002,{one,two,three}).affected_rows==0,"confirmation batch repeat is idempotent");
        Check(reader.LoadDialogSnapshot(500002,500003).value.private_unread==3,"confirmation does not clear unread");
        Check(app.MarkDialogRead(500002,500003).Succeeded()&&app.ConfirmReceiver(one,500002).affected_rows==0&&store.GetPrivateMessage(one).record.delivery_state==MessageDeliveryState::kRead,"late confirmation cannot downgrade concurrent read");
        const auto user=reader.LoadUserSnapshot(500002);
        Check(user.success&&std::find(user.value.peer_counts.begin(),user.value.peer_counts.end(),std::pair<std::uint64_t,std::int64_t>{500003,0})!=user.value.peer_counts.end(),"complete user snapshot retains zero-count historical incoming peers");
        const auto outgoing=reader.LoadUserSnapshot(500003);
        Check(outgoing.success&&outgoing.value.peer_counts.empty()&&outgoing.value.total_unread==0,"outgoing-only peers are not invented as incoming history");
        Check(app.ListHistory(500002,500001,0,50).Succeeded()&&app.ListConversations(500002,50).Succeeded()&&app.ListPendingAfter(500002,0,100).Succeeded()&&app.ListPendingRecipientsAfter(0,100).Succeeded(),"all private read application paths use local owner");
        Same(adapter.FindGroupMessageByClientMessageId(0,"g"),sql.FindGroupMessageByClientMessageId(0,"g"),"group CID delegates unchanged");
        Same(adapter.PersistAuthorizedGroupMessage(0,0,"g",1,"b",0,0,0,{}),sql.PersistAuthorizedGroupMessage(0,0,"g",1,"b",0,0,0,{}),"group persist delegates unchanged");
        Same(adapter.ListGroupHistory(0,0,0,1),sql.ListGroupHistory(0,0,0,1),"group history delegates unchanged");
        Same(adapter.GetGroupMessageDelivery(0,0),sql.GetGroupMessageDelivery(0,0),"group delivery lookup delegates unchanged");
        Same(adapter.ClaimGroupMessageDeliveries("","",0,0,0),sql.ClaimGroupMessageDeliveries("","",0,0,0),"group lease claim delegates unchanged");
        Same(adapter.ClaimGroupMessageDeliveriesForRecipient(0,"","",0,0),sql.ClaimGroupMessageDeliveriesForRecipient(0,"","",0,0),"group recipient claim delegates unchanged");
        Same(adapter.CompleteGroupMessageDeliveryAttempts({}),sql.CompleteGroupMessageDeliveryAttempts({}),"group batch completion delegates unchanged");
        Same(adapter.ConfirmGroupMessageDelivery(0,0),sql.ConfirmGroupMessageDelivery(0,0),"group confirmation delegates unchanged");
        // Existing batches validate all100 before mutation. New compatibility
        // normalization remains active while several private writers/readers run.
        std::atomic<unsigned> bad{0};std::vector<std::thread> writers;
        for(unsigned t=0;t<4;++t)writers.emplace_back([&,t]{for(unsigned i=0;i<25;++i){auto x=app.PersistPrivateMessage(500003,500002,tag+"parallel-"+std::to_string(t)+'-'+std::to_string(i),1,"body");if(!x.Created())++bad;}});
        for(auto&t:writers)t.join();Check(bad==0&&reader.LoadDialogSnapshot(500002,500003).value.private_unread==100,"parallel authoritative CID writes preserve exact local counters");
        const auto first=app.PersistPrivateMessage(500001,500002,tag+"reopen",1,"recover");
        Check(first.Created()&&store.Shutdown(),"application owner graceful sync");
        Check(!app.CountPending(500002).Succeeded()&&!reader.LoadUserSnapshot(500002).success,"closed owner never falls back to SQL data");
        {
            LocalPrivateStore reopened(options);LocalMessageRepositoryAdapter again(&reopened,&sql);MessageApplicationService recovered(&again);
            Check(recovered.PersistPrivateMessage(500001,500002,tag+"reopen",1,"recover").Reused(),"compatibility policy and full application CID survive reopen");
            auto wrong=options;wrong.identity_policy="fixture-raw-bytes-v1";wrong.resolve_identity={};bool denied=false;
            try{LocalPrivateStore incompatible(wrong);}catch(const std::exception&){denied=true;}
            Check(denied,"different CID key policy cannot open and reinterpret existing store");
            const auto n=reopened.Stats().pending_events;pool.Shutdown();
            Check(!recovered.PersistPrivateMessage(500001,500002,tag+"authority-down",1,"body").Accepted()&&reopened.Stats().pending_events==n,"authority failure rejects new writes without local side effects");
        }
        Logger::Instance().Shutdown();std::cout<<"LOCAL_MESSAGE_REPOSITORY_APPLICATION_SQL_PARITY_PASS\n";return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';Logger::Instance().Shutdown();return 1;}
}
