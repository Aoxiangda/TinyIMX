#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/db/MySqlConnection.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok,const char* text) {
    std::cout<<(ok?"[PASS] ":"[FAIL] ")<<text<<'\n';
    if(!ok)throw std::runtime_error(text);
}
std::string Scalar(tinyimx::MySqlConnectionPool& pool,const std::string& sql) {
    auto c=pool.Acquire();tinyimx::MySqlQueryResult r;
    Require(c&&c->Query(sql,&r)&&r.rows.size()==1&&r.rows[0].size()==1,"owned scalar query");
    return r.rows[0][0];
}
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    tinyimx::Config cfg;
    if(!cfg.LoadFromFile(argv[1])||!cfg.MySql().enable||cfg.MySql().database!="codex_event_topic_20261019_r1"||cfg.RocketMQ().message_topic!="codex_perf041_events_20261019_r1")return 2;
    if(!tinyimx::Logger::Instance().Init(cfg.Logger()))return 2;
    try {
        tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(cfg.MySql()),"owned SQL initialized");
        Require(Scalar(pool,"SELECT COUNT(*)FROM im_event_outbox") == "0","fresh empty isolated Outbox");
        Require(Scalar(pool,"SELECT COUNT(*)FROM im_users WHERE user_id IN(500001,500002)")=="2","exact owned user fixtures present");
        tinyimx::MessageRepository repo(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);
        tinyimx::message::MessageRepositoryAdapter adapter(&repo,&pool,&outbox,cfg.RocketMQ().message_topic);
        const auto p=adapter.PersistPrivateMessage(500001,500002,"topic-private-r1",1,"topic isolated body");
        Require(p.Accepted()&&p.message_id!=0,"nondefault private message transaction commits");
        const auto duplicate=adapter.PersistPrivateMessage(500001,500002,"topic-private-r1",1,"topic isolated body");
        Require(duplicate.Accepted()&&duplicate.message_id==p.message_id,"same CID retry retains one durable identity");
        Require(repo.MarkReceiverConfirmed(p.message_id).Succeeded(),"receiver confirmation retained");
        const auto read=adapter.MarkDialogRead(500002,500001);Require(read.Succeeded()&&read.affected_rows==1,"nondefault read transition commits");
        std::uint64_t group=0;
        {
            auto c=pool.Acquire();Require(c&&c->Execute("INSERT INTO im_groups(name,description,avatar_url,owner_user_id,status,join_policy,max_members,version,member_version)VALUES('topic-owned-r1','','',500001,1,1,20,1,1)"),"owned group fixture created");group=c->LastInsertId();
        }
        const auto g=adapter.PersistAuthorizedGroupMessage(500001,group,"topic-group-r1",1,"group topic body",1,1,1,{500002});
        Require(g.Accepted()&&g.message_id!=0,"nondefault group transaction and recipient snapshot commit");
        Require(Scalar(pool,"SELECT COUNT(*)FROM im_event_outbox WHERE topic='codex_perf041_events_20261019_r1'")=="3","all three durable events use configured topic");
        for(const auto type:{"message.created.v1","group_message.created.v1","dialog.read_advanced.v1"}) {
            Require(Scalar(pool,std::string("SELECT COUNT(*)FROM im_event_outbox WHERE event_type='")+type+"'AND topic='codex_perf041_events_20261019_r1'")=="1","event type and topic preserved exactly");
        }
        outbox.SetForceInsertFailureForTest(true);
        Require(!adapter.PersistPrivateMessage(500001,500002,"topic-rollback-r1",1,"must rollback").Accepted(),"custom topic Outbox failure rejects write");
        outbox.SetForceInsertFailureForTest(false);
        Require(Scalar(pool,"SELECT COUNT(*)FROM im_private_messages WHERE client_message_id='topic-rollback-r1'")=="0","failed Outbox leaves no partial private message");
        tinyimx::message::MessageRepositoryAdapter legacy(&repo,&pool,&outbox);
        Require(legacy.PersistPrivateMessage(500001,500002,"topic-default-r1",1,"legacy topic").Accepted(),"default adapter still commits");
        Require(Scalar(pool,"SELECT COUNT(*)FROM im_event_outbox WHERE topic='tinyimx-message-events'")=="1","default topic unchanged for legacy callers");
        Require(Scalar(pool,"SELECT COUNT(*)FROM im_event_outbox")=="4","no duplicate or rollback event introduced");
        std::cout<<"[PASS] CONFIGURED_TOPIC_ALL_THREE_REAL_SQL_TRANSACTIONS_AND_ROLLBACK\n";
    }catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';tinyimx::Logger::Instance().Shutdown();return 1;}
    tinyimx::Logger::Instance().Shutdown();return 0;
}
