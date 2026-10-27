#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tinyimx;
using namespace tinyimx::message;
void Check(bool ok,const char* label) {
    if (!ok) throw std::runtime_error(label);
    std::cout << "PASS " << label << '\n';
}
std::uint64_t Scalar(MySqlConnection& c,const std::string& sql) {
    MySqlQueryResult rows;
    if (!c.Query(sql,&rows)||rows.rows.size()!=1||rows.rows[0].size()!=1)
        throw std::runtime_error("owned observer query failed");
    return std::stoull(rows.rows[0][0]);
}
}

int main(int argc,char** argv) {
    if (argc!=2&&argc!=3) return 2;
    const bool wire_loss=argc==3&&std::string(argv[2])=="wire-loss";
    if (argc==3&&!wire_loss) return 2;
    try {
        Config config;
        Check(config.LoadFromFile(argv[1])&&config.MySql().database=="codex_perf102_20261020_r1"&&config.MySql().pool_size==1,
              "explicit owned resource database and pool1");
        Check(Logger::Instance().Init(config.Logger()),"owned logger initialized");
        MySqlConnectionPool pool;
        Check(pool.Initialize(config.MySql()),"pool initialized");
        auto direct=config.MySql();direct.unix_socket.clear();
        MySqlConnection observer;
        Check(observer.Connect(direct),"independent direct observer connected");
        MessageRepository messages(&pool);
        outbox::OutboxRepository events(&pool);
        MessageRepositoryAdapter adapter(&messages,&pool,&events,"codex_perf102_events_20261020_r1");
        const auto prefix="p102-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+'-';
        const auto before_messages=Scalar(observer,"SELECT COUNT(*)FROM im_private_messages");
        const auto before_events=Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox");
        if (wire_loss) {
            Check(!config.MySql().unix_socket.empty(),"wire-loss mode uses owned Unix proxy");
            const auto result=adapter.PersistPrivateMessage(500001,500002,prefix+"lost-ok",1,"owned real wire COMMIT OK loss");
            Check(result.Accepted()&&result.outcome==PersistPrivateMessageOutcome::kReused&&
                  result.message.find("combined commit outcome recovered")!=std::string::npos,
                  "real lost COMMIT response resolved by read-only CID lookup");
            Check(Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+1&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+1,
                  "lost reply leaves exactly one durable message and Outbox event");
            const auto retry=adapter.PersistPrivateMessage(500001,500002,prefix+"lost-ok",1,"owned real wire COMMIT OK loss");
            Check(retry.Accepted()&&retry.message_id==result.message_id&&retry.outcome==PersistPrivateMessageOutcome::kReused,
                  "retry preserves MID without another message or event");
        } else {
            Check(observer.Execute("CREATE TABLE IF NOT EXISTS codex_packet_driver_rows(id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,cid VARCHAR(64)NOT NULL UNIQUE,value INT NOT NULL)ENGINE=InnoDB"),
                  "owned protocol table only");
            MySqlConnection c;
            Check(c.Connect(direct),"protocol connection");
            MySqlInsertCommitResult result;
            Check(!c.InsertAndCommit("INSERT INTO codex_packet_driver_rows(cid,value)VALUES('invalid',1)",&result)&&
                  !result.command_attempted&&c.InsertCommitCommandAttempts()==0,"no transaction rejected before send");
            Check(c.BeginTransaction(),"start protocol transaction");
            Check(!c.InsertAndCommit("SELECT 1",&result)&&!result.command_attempted&&c.InTransaction(),
                  "query rejected without disturbing active transaction");
            const std::string oversize="INSERT INTO codex_packet_driver_rows(cid,value)VALUES('"+std::string(1024*1024,'x')+"',1)";
            Check(!c.InsertAndCommit(oversize,&result)&&!result.command_attempted&&c.InTransaction(),
                  "oversize command rejected before send");
            Check(!c.InsertAndCommit("INSERT INTO codex_packet_driver_rows(cid,value)VALUES('x',1)",nullptr)&&c.InTransaction(),
                  "null output rejected before send");
            const auto insert="INSERT INTO codex_packet_driver_rows(cid,value)VALUES('"+prefix+"driver',1)";
            Check(c.InsertAndCommit(insert,&result)&&result.command_attempted&&result.insert_acknowledged&&result.commit_acknowledged&&
                  result.insert_id>0&&result.affected_rows==1&&!c.InTransaction()&&c.InsertCommitCommandAttempts()==1,
                  "exact INSERT and COMMIT replies before success");
            Check(Scalar(c,"SELECT 42")==42,"next query has no unread result boundary");
            Check(c.BeginTransaction()&&c.Execute("INSERT INTO codex_packet_driver_rows(cid,value)VALUES('"+prefix+"rollback-marker',1)"),
                  "uncommitted marker before server error");
            Check(!c.InsertAndCommit("INSERT INTO codex_packet_owned_missing_table(id)VALUES(1)",&result)&&
                  result.command_attempted&&!result.commit_acknowledged&&!c.IsConnected(),
                  "real INSERT server error retires uncertain session");
            Check(Scalar(observer,"SELECT COUNT(*)FROM codex_packet_driver_rows WHERE cid='"+prefix+"rollback-marker'")==0,
                  "no trailing COMMIT after rejected INSERT; earlier write rolled back");

            std::string body="quote';semicolon;utf8-蜜蜂";body+='\0';body+="tail";
            const auto created=adapter.PersistPrivateMessage(500001,500002,prefix+"message",1,body);
            Check(created.Accepted()&&created.outcome==PersistPrivateMessageOutcome::kCreated&&created.record.content==body,
                  "escaped content and full saved identity preserved");
            const auto reused=adapter.PersistPrivateMessage(500001,500002,prefix+"message",1,body);
            const auto conflict=adapter.PersistPrivateMessage(500001,500002,prefix+"message",1,"different");
            Check(reused.Accepted()&&reused.message_id==created.message_id&&reused.outcome==PersistPrivateMessageOutcome::kReused&&
                  conflict.outcome==PersistPrivateMessageOutcome::kIdempotencyConflict,
                  "CID reuse and conflict do not duplicate durable records");
            Check(Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+1&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+1,"one atomic message and event");
            events.SetForceInsertFailureForTest(true);
            const auto rejected=adapter.PersistPrivateMessage(500001,500002,prefix+"prepare-fail",1,"body");
            events.SetForceInsertFailureForTest(false);
            Check(!rejected.Accepted()&&Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+1&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+1,"pre-send event validation failure rolls back message");

            // The caller-owned sole session sees this temporary rejection shadow;
            // the independent observer continues seeing the physical Outbox.
            {auto lease=pool.Acquire();Check(lease&&lease->Execute("CREATE TEMPORARY TABLE im_event_outbox(outbox_id BIGINT PRIMARY KEY,owned_reject VARCHAR(32))"),
                                          "session-only real Outbox SQL failure armed");}
            const auto failed=adapter.PersistPrivateMessage(500001,500002,prefix+"server-fail",1,"body");
            Check(!failed.Accepted()&&Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+1&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+1,"real failed Outbox INSERT leaves no partial message/event");
            {auto lease=pool.Acquire();Check(lease&&lease->Execute("DROP TEMPORARY TABLE IF EXISTS im_event_outbox"),
                                          "clear only own session shadow if legacy path retained it");}
            const auto recovered=adapter.PersistPrivateMessage(500001,500002,prefix+"server-fail",1,"body");
            Check(recovered.Accepted()&&Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+2&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+2,"failed request retry commits once after recovery");
            adapter.SetPrivatePersistBeforeCommitHookForTest([](MySqlConnection* c){c->Close();});
            const auto before_commit=adapter.PersistPrivateMessage(500001,500002,prefix+"before-commit",1,"body");
            Check(!before_commit.Accepted()&&Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+2&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+2,
                  "legacy before-COMMIT seam still rolls back both writes");
            bool committed=false;
            adapter.SetPrivatePersistBeforeCommitHookForTest([&](MySqlConnection* c){committed=c->Commit();c->Close();});
            const auto legacy_recovery=adapter.PersistPrivateMessage(500001,500002,prefix+"legacy-uncertain",1,"body");
            adapter.SetPrivatePersistBeforeCommitHookForTest({});
            Check(committed&&legacy_recovery.Accepted()&&legacy_recovery.outcome==PersistPrivateMessageOutcome::kReused&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_private_messages")==before_messages+3&&
                  Scalar(observer,"SELECT COUNT(*)FROM im_event_outbox")==before_events+3,
                  "legacy post-COMMIT recovery preserved; distinct from real packet reply-loss test");
        }
        Check(pool.Size()==1&&pool.AvailableCount()==1,"sole lease returned across failure/recovery");
        observer.Close();pool.Shutdown();Logger::Instance().Shutdown();
        std::cout<<"PRIVATE_OUTBOX_COMMIT_PACKET_CONTRACTS_PASS\n";return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';Logger::Instance().Shutdown();return 1;
    }
}
