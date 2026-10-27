#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace tinyimx::message;
void Require(bool yes,const char* what){if(!yes)throw std::runtime_error(what);std::cout<<"PASS "<<what<<'\n';}
std::uint64_t Scalar(tinyimx::MySqlConnection& connection,const std::string& query){
    tinyimx::MySqlQueryResult out;Require(connection.Query(query,&out)&&out.rows.size()==1&&out.rows[0].size()==1,"owned verification query");
    return std::stoull(out.rows[0][0]);
}
}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;
    const std::uint64_t retained=argc==3&&std::string(argv[2])=="resume6"?6:0;
    if(argc==3&&!retained)return 2;
    try{
        tinyimx::Config config;Require(config.LoadFromFile(argv[1])&&config.MySql().database=="codex_perf041_20261018_r77"&&config.MySql().pool_size==1,"exact resource fixture and pool1");
        Require(tinyimx::Logger::Instance().Init(config.Logger()),"logger initialized");
        tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(config.MySql()),"SQL pool initialized");
        tinyimx::MySqlConnection observer;Require(observer.Connect(config.MySql()),"independent observer connection");
        tinyimx::MessageRepository messages(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);
        MessageRepositoryAdapter adapter(&messages,&pool,&outbox,config.RocketMQ().message_topic);
        const auto initial_messages=Scalar(observer,"SELECT COUNT(*)FROM im_private_messages");
        const auto initial_events=Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox");
        Require(initial_messages==retained&&initial_events==retained,"exact expected resource baseline, prior rows retained");
        const auto prefix="p094-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+'-';
        const auto input=[&](const std::string& suffix,const std::string& body="body",std::uint64_t from=500001,std::uint64_t to=500002){return PrivateMessagePersistInput{from,to,prefix+suffix,1,body};};
        auto batch=adapter.PersistPrivateMessages({input("a"),input("b","body",500003,500004),input("c")});
        Require(batch.size()==3&&batch[0].Accepted()&&batch[1].Accepted()&&batch[2].Accepted(),"grouped three messages accepted after atomic commit");
        std::set<std::uint64_t> ids;for(const auto& row:batch){Require(row.outcome==PersistPrivateMessageOutcome::kCreated,"grouped result created");ids.insert(row.message_id);}
        Require(ids.size()==3&&Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==retained+3&&Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==retained+3,"one durable message and event per input");
        auto duplicate=adapter.PersistPrivateMessages({input("same"),input("same"),input("same","changed"),input("other")});
        Require(duplicate.size()==4&&duplicate[0].outcome==PersistPrivateMessageOutcome::kCreated&&duplicate[1].outcome==PersistPrivateMessageOutcome::kReused&&
            duplicate[2].outcome==PersistPrivateMessageOutcome::kIdempotencyConflict&&duplicate[3].outcome==PersistPrivateMessageOutcome::kCreated,"same-batch CID create reuse conflict remains FIFO");
        Require(duplicate[0].message_id==duplicate[1].message_id&&duplicate[0].message_id==duplicate[2].message_id&&
            Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==retained+5&&Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==retained+5,"duplicates and conflict add no extra message/event");
        const auto lower=prefix+"case";auto upper=lower;
        for(auto& c:upper)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
        const auto direct_lower=adapter.PersistPrivateMessage(500001,500002,lower,1,"body");
        Require(direct_lower.Accepted(),"reference lower-case CID committed");
        (void)adapter.PersistPrivateMessage(500001,500002,upper,1,"body");
        const auto reference=adapter.PersistPrivateMessage(500001,500002,upper,1,"body");
        const auto collation=adapter.PersistPrivateMessages({{500001,500002,upper,1,"body"},{500001,500002,lower,1,"body"}});
        Require(collation.size()==2&&collation[0].Completed()&&collation[0].outcome==reference.outcome&&
            collation[0].message_id==reference.message_id&&collation[1].message_id==direct_lower.message_id,
            "batch CID case/collation outcome equals original single-call repository");
        const auto stable=Scalar(observer,"SELECT COUNT(*)FROM im_private_messages");
        Require(Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==stable,"collation reference remains one event per message");
        // Force only this connection's Outbox INSERT to fail. The second
        // connection still sees the original physical table and no partial rows.
        {auto c=pool.Acquire();Require(c&&c->Execute("CREATE TEMPORARY TABLE im_event_outbox(outbox_id BIGINT UNSIGNED PRIMARY KEY, codex_owned_rejected_event_id VARCHAR(128))"),"owned session-only Outbox rejection shadow");}
        auto failed=adapter.PersistPrivateMessages({input("rollback-a"),input("rollback-b")});
        Require(failed.size()==2&&!failed[0].Accepted()&&!failed[1].Accepted()&&
            Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==stable&&Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==stable,"failed Outbox rolls back entire grouped transaction");
        {auto c=pool.Acquire();Require(c&&c->Execute("DROP TEMPORARY TABLE IF EXISTS im_event_outbox"),"remove only owned temporary shadow");}
        adapter.SetPrivatePersistBeforeCommitHookForTest([](tinyimx::MySqlConnection* c){c->Close();});
        failed=adapter.PersistPrivateMessages({input("disconnect-a"),input("disconnect-b")});
        Require(failed.size()==2&&!failed[0].Accepted()&&!failed[1].Accepted()&&
            Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==stable&&Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==stable,"connection loss before commit leaves no durable partial batch");
        bool committed=false;
        adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection* c){committed=c->Commit();c->Close();});
        auto uncertain=adapter.PersistPrivateMessages({input("uncertain-a"),input("uncertain-b")});
        Require(committed&&uncertain.size()==2&&uncertain[0].Accepted()&&uncertain[1].Accepted()&&
            uncertain[0].outcome==PersistPrivateMessageOutcome::kReused&&uncertain[1].outcome==PersistPrivateMessageOutcome::kReused,
            "server commit then closed connection recovers both outcomes by read-only CID");
        adapter.SetPrivatePersistBeforeCommitHookForTest({});
        const auto retry=adapter.PersistPrivateMessages({input("uncertain-a"),input("uncertain-b")});
        Require(retry[0].message_id==uncertain[0].message_id&&retry[1].message_id==uncertain[1].message_id&&
            Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==stable+2&&Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==stable+2,"uncertain batch retry preserves MIDs and one event per durable message");
        Require(pool.AvailableCount()==1&&pool.Size()==1,"all failure and recovery paths return the sole lease");
        std::cout<<"FINAL_OWNED_ROWS messages="<<stable+2<<" events="<<stable+2<<'\n';
        observer.Close();pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';tinyimx::Logger::Instance().Shutdown();return 1;}
}
