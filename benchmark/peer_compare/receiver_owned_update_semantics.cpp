// Real adapter/application semantics. Only generated schema and synthetic rows.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/repository/MessageRepository.h"
#include <nlohmann/json.hpp>
#include <barrier>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using Json=nlohmann::json;
using namespace tinyimx::message;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::string Scalar(tinyimx::MySqlConnectionPool& pool,const std::string& sql){
    auto c=pool.Acquire();tinyimx::MySqlQueryResult r;
    Require(c&&c->Query(sql,&r)&&r.rows.size()==1&&r.rows[0].size()==1,"Owned scalar failed");
    return r.rows[0][0];
}
void Execute(tinyimx::MySqlConnectionPool& pool,const std::string& sql){auto c=pool.Acquire();Require(c&&c->Execute(sql),"Owned fixture mutation failed");}
int main(int argc,char** argv){Json result={{"issue_id","PERF-039"},{"status","FAIL"},{"checks",Json::array()}};
try{
    Require(argc==4,"config owned-schema enabled");std::string schema=argv[2];bool enabled=std::string(argv[3])=="1";
    Require(schema.starts_with("codex_perf039_20261017_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"Unowned schema refused");
    tinyimx::Config config;Require(config.LoadFromFile(argv[1]),"Config failed");auto settings=config.MySql();settings.database=schema;settings.pool_size=8;
    tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(settings),"Pool failed");
    Require(Scalar(pool,"SELECT DATABASE()")==schema&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="0","Fresh owned fixture required");
    tinyimx::MessageRepository repository(&pool);MessageRepositoryAdapter adapter(&repository,&pool,nullptr);MessageApplicationService app(&adapter);
    auto check=[&](bool ok,const char* label){Require(ok,label);result["checks"].push_back(label);};
    auto insert=[&](unsigned id,unsigned from,unsigned to,unsigned type,unsigned status){
        Execute(pool,"INSERT INTO im_private_messages(message_id,client_message_id,from_user_id,to_user_id,message_type,content,delivery_status,created_at) VALUES("+
          std::to_string(id)+",'fixture-"+std::to_string(id)+"',"+std::to_string(from)+","+std::to_string(to)+","+std::to_string(type)+",'owned',"+std::to_string(status)+",NOW())");};
    for(unsigned id=1;id<=4;++id)insert(id,11,92,1,id-1);
    insert(5,11,92,1,99);insert(6,0,92,1,0);insert(7,11,92,0,0);insert(8,11,92,4,0);insert(9,11,0,1,0);
    insert(10,11,92,1,0);insert(11,11,92,1,0);insert(12,11,92,1,0);
    auto direct=adapter.TryConfirmReceiverOwnedIfPending(12,92);
    check(enabled?(direct&&direct->Succeeded()&&direct->affected_rows==1):!direct,
          "actual_opt_in_switch_verified");
    check(Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=12")==(enabled?"1":"0"),
          "switch_controls_actual_mutation");
    check(app.ConfirmReceiver(0,92).status==MessageApplicationStatus::kInvalidArgument&&app.ConfirmReceiver(1,0).status==MessageApplicationStatus::kInvalidArgument,"invalid_identity");
    check(app.ConfirmReceiver(1,93).status==MessageApplicationStatus::kPermissionDenied&&Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=1")=="0","wrong_owner_never_mutates");
    auto first=app.ConfirmReceiver(1,92);check(first.Succeeded()&&first.affected_rows==1&&Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=1")=="1","pending_durable_confirmation");
    check(Scalar(pool,"SELECT delivered_at IS NOT NULL FROM im_private_messages WHERE message_id=1")=="1","confirmation_timestamp_durable");
    check(app.ConfirmReceiver(1,92).Succeeded()&&app.ConfirmReceiver(1,92).affected_rows==0,"duplicate_idempotence");
    for(unsigned id:{2,3}){auto r=app.ConfirmReceiver(id,92);check(r.Succeeded()&&r.affected_rows==0,id==2?"already_confirmed":"read_not_regressed");}
    check(Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=3")=="2","read_state_unchanged");
    check(app.ConfirmReceiver(4,92).status==MessageApplicationStatus::kFailedPrecondition&&Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=4")=="3","failed_state_rejected");
    check(app.ConfirmReceiver(99,92).status==MessageApplicationStatus::kNotFound,"missing_not_success");
    for(unsigned id:{5,6,7,8,9})check(app.ConfirmReceiver(id,92).status==MessageApplicationStatus::kInvalidRecord,"invalid_record_fails_closed");
    check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE message_id BETWEEN 6 AND 9 AND delivery_status=0")=="4","invalid_rows_never_mutate");
    check(app.ConfirmReceiver(3,93).status==MessageApplicationStatus::kPermissionDenied,"read_still_checks_owner");
    check(app.ConfirmReceiver(4,93).status==MessageApplicationStatus::kPermissionDenied,"failed_still_checks_owner_first");
    std::barrier start(17);std::mutex mutex;unsigned affected=0,errors=0;std::vector<std::jthread> threads;
    for(unsigned i=0;i<16;++i)threads.emplace_back([&]{start.arrive_and_wait();auto r=app.ConfirmReceiver(10,92);std::lock_guard lock(mutex);affected+=r.affected_rows;errors+=!r.Succeeded();});
    start.arrive_and_wait();for(auto& t:threads)t.join();threads.clear();
    check(errors==0&&affected==1&&Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=10")=="1","concurrent_duplicates_exactly_one_transition");
    std::barrier race(3);MessageMutationApplicationResult confirmation;
    std::jthread confirm([&]{race.arrive_and_wait();confirmation=app.ConfirmReceiver(11,92);});
    std::jthread read([&]{race.arrive_and_wait();Execute(pool,"UPDATE im_private_messages SET delivery_status=2,read_at=NOW() WHERE message_id=11 AND to_user_id=92 AND delivery_status IN(0,1)");});
    race.arrive_and_wait();confirm.join();read.join();
    check(confirmation.Succeeded()&&Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=11")=="2","concurrent_read_never_regresses");
    auto optional=adapter.TryConfirmReceiverOwnedIfPending(99,92);check(!optional,"no_change_classification_fallback");
    Execute(pool,"RENAME TABLE im_private_messages TO owned_messages_preserved");
    const auto failed=app.ConfirmReceiver(1,92);
    Execute(pool,"RENAME TABLE owned_messages_preserved TO im_private_messages");
    check(failed.status==MessageApplicationStatus::kStorageError,"storage_failure_not_hidden");
    check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="12","all_rows_preserved");
    result["enabled"]=enabled;result["status"]="PASS";
}catch(const std::exception& e){result["failure"]=e.what();}
std::cout<<result.dump()<<'\n';return result["status"]=="PASS"?0:2;
}
