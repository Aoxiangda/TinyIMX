#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/outbox/OutboxRepository.h"
#include "services/repository/MessageRepository.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures=0;
void Check(bool ok,const char* label){std::cout<<(ok?"[PASS] ":"[FAIL] ")<<label<<'\n';failures+=!ok;}
std::string Unique(const char* prefix){return std::string(prefix)+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());}
std::string Scalar(tinyimx::MySqlConnectionPool& pool,const std::string& sql){
    auto c=pool.Acquire();tinyimx::MySqlQueryResult rows;
    if(!c||!c->Query(sql,&rows)||rows.rows.size()!=1||rows.rows[0].size()!=1)return "ERROR";
    return rows.rows[0][0];
}
}
int main(int argc,char** argv){
    if(argc!=2&&argc!=3)return 2;
    tinyimx::Config config;
    if(!config.LoadFromFile(argv[1])||!config.MySql().enable)return 2;
    // All persistent test writes must be in a separately audited owned schema.
    if(config.MySql().database.rfind("codex_private_batch_20261005_",0)!=0||config.MySql().pool_size!=1)return 2;
    if(!tinyimx::Logger::Instance().Init(config.Logger()))return 2;
    tinyimx::MySqlConnectionPool pool;if(!pool.Initialize(config.MySql()))return 2;
    tinyimx::MessageRepository repository(&pool);
    tinyimx::outbox::OutboxRepository outbox(&pool);
    tinyimx::message::MessageRepositoryAdapter adapter(&repository,&pool,&outbox);
    using Status=tinyimx::message::MessageApplicationStatus;
    using Outcome=tinyimx::message::PersistPrivateMessageOutcome;
    if(argc==3){
        if(std::string(argv[2])!="commit-drop")return 2;
        const auto cid=Unique("batch-commit-drop-");
        const auto recovered=adapter.PersistPrivateMessage(10001,10002,cid,1,"owned response-loss fixture");
        Check(recovered.Completed()&&recovered.outcome==Outcome::kReused&&recovered.message_id>0,
              "Lost COMMIT response recovers durable message by CID");
        Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE event_id='message.created.v1:"+std::to_string(recovered.message_id)+"'")=="1",
              "Lost COMMIT response leaves exactly one durable domain outbox");
        const auto retry=adapter.PersistPrivateMessage(10001,10002,cid,1,"owned response-loss fixture");
        Check(retry.Completed()&&retry.outcome==Outcome::kReused&&retry.message_id==recovered.message_id,
              "Retry after uncertain COMMIT converges to same durable MID");
        Check(pool.AvailableCount()==pool.Size(),"Uncertain COMMIT recovery returns pool1 lease");
        pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return failures?1:0;
    }
    {
        auto c=pool.Acquire();
        Check(c&&c->Execute("CREATE TEMPORARY TABLE codex_private_batch_driver_rows(id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,cid VARCHAR(64) NOT NULL UNIQUE,content TEXT NOT NULL)"),
              "Create only own connection temporary driver fixture");
        if(!c)return 2;
        tinyimx::MySqlBeginInsertQueryResult result;
        Check(!c->BeginInsertAndQuery("INSERT INTO codex_private_batch_driver_rows(cid,content) VALUES ('null','x')","SELECT 1",nullptr)&&!c->InTransaction(),
              "Null output rejects before transaction");
        const std::string content="quoted ' ; COMMIT; \\ newline\n UTF8 中文";
        const auto sql="INSERT INTO codex_private_batch_driver_rows(cid,content) VALUES ('first','"+c->EscapeString(content)+"')";
        Check(c->BeginInsertAndQuery(sql,"SELECT id,cid,content FROM codex_private_batch_driver_rows WHERE id=LAST_INSERT_ID() LIMIT 1",&result)&&
              result.begin_succeeded&&result.insert_succeeded&&result.query_succeeded&&result.affected_rows==1&&result.insert_id>0&&
              result.query_result.rows.size()==1&&result.query_result.rows[0][2]==content&&c->InTransaction(),
              "Three-result batch preserves escaped content and active transaction");
        tinyimx::MySqlBeginInsertQueryResult nested;
        Check(!c->BeginInsertAndQuery(sql,"SELECT 1",&nested)&&!nested.begin_succeeded&&c->InTransaction(),
              "Active transaction rejected without nested BEGIN or mutation");
        Check(c->Commit()&&!c->InTransaction(),"Caller commits only after identity validation");
        Check(!c->BeginInsertAndQuery(sql,"SELECT 1",&result)&&result.begin_succeeded&&!result.insert_succeeded&&!result.query_succeeded&&c->InTransaction(),
              "Duplicate INSERT preserves acknowledged BEGIN and fails without query");
        Check(c->Rollback(),"Duplicate INSERT transaction can roll back");
        Check(!c->BeginInsertAndQuery("INSERT INTO codex_private_batch_driver_rows(cid,content) VALUES ('read-error','x')",
              "SELECT codex_absent_column FROM codex_private_batch_driver_rows WHERE id=LAST_INSERT_ID()",&result)&&
              result.begin_succeeded&&result.insert_succeeded&&!result.query_succeeded&&c->InTransaction(),
              "Read SQL error preserves acknowledged INSERT and uncommitted transaction");
        Check(c->Rollback(),"Read error rolls back before outbox or commit");
        tinyimx::MySqlQueryResult rows;
        Check(c->Query("SELECT COUNT(*) FROM codex_private_batch_driver_rows WHERE cid='read-error'",&rows)&&rows.rows[0][0]=="0"&&c->Ping(),
              "Rollback removes own failed row and leaves protocol healthy");
        Check(!c->BeginInsertAndQuery("INSERT INTO codex_private_batch_driver_rows(cid,content) VALUES ('extra','x')","SELECT 1;SELECT 2",&result)&&
              !result.query_succeeded&&!c->IsConnected()&&!c->InTransaction(),
              "Unexpected fourth result retires session without accepting identity");
    }
    Check(Scalar(pool,"SELECT 1")=="1"&&pool.AvailableCount()==pool.Size(),"Retired session reconnects on ordinary healthy checkout");
    const auto cid=Unique("batch-escaping-");
    const std::string content="{\"text\":\"quote ' ; COMMIT; \\ 中文\"}";
    const auto created=adapter.PersistPrivateMessage(10001,10002,cid,1,content);
    Check(created.Completed()&&created.outcome==Outcome::kCreated&&created.record.content==content,
          "Actual adapter keeps quoted semicolon UTF8 identity intact");
    const auto repeated=adapter.PersistPrivateMessage(10001,10002,cid,1,content);
    Check(repeated.Completed()&&repeated.outcome==Outcome::kReused&&repeated.message_id==created.message_id,
          "Escaped identity repeat reuses durable MID");
    const auto events=Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox");
    {
        auto c=pool.Acquire();
        Check(c&&c->Execute("CREATE TEMPORARY TABLE im_private_messages LIKE im_private_messages"),
              "Create own session shadow for before-outbox validation fault");
        Check(c&&c->Execute("ALTER TABLE im_private_messages MODIFY message_type ENUM('2','1') NOT NULL"),
              "Only temporary shadow has numeric INSERT reinterpretation fault");
    }
    const auto wrong=adapter.PersistPrivateMessage(10001,10002,Unique("batch-wrong-type-"),1,"owned shadow fixture");
    Check(!wrong.Completed()&&wrong.status==Status::kInvalidRecord,
          "Valid parsed different identity rejects before outbox or COMMIT");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="0"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")==events,
          "Identity mismatch leaves neither shadow message nor outbox event");
    {
        auto c=pool.Acquire();
        Check(c&&c->Execute("ALTER TABLE im_private_messages MODIFY message_type TINYINT UNSIGNED NOT NULL, MODIFY created_at DATETIME NULL DEFAULT NULL"),
              "Only temporary shadow supplies malformed creation time");
    }
    const auto malformed=adapter.PersistPrivateMessage(10001,10002,Unique("batch-malformed-"),1,"owned malformed fixture");
    Check(!malformed.Completed()&&malformed.status==Status::kInvalidRecord,
          "Original full-record parser rejects malformed creation time before outbox");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="0"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")==events,
          "Invalid record rolls back atomic message and leaves outbox unchanged");
    Check(pool.AvailableCount()==pool.Size(),"Every failed path releases the pool1 lease");
    pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return failures?1:0;
}
