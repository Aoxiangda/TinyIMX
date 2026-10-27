// Real adapter/application semantics. Only generated schema and synthetic rows.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/repository/MessageRepository.h"
#include <nlohmann/json.hpp>
#include <barrier>
#include <atomic>
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
int main(int argc,char** argv){Json result={{"issue_id","PERF-054"},{"status","FAIL"},{"checks",Json::array()}};
try{
    Require(argc==4,"config owned-schema enabled");std::string schema=argv[2];bool enabled=std::string(argv[3])=="1";
    Require(schema.starts_with("codex_perf054_20261018_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"Unowned schema refused");
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
    auto optional=adapter.TryConfirmReceiverOwnedIfPending(99,92);check(enabled?(optional&&optional->status==MessageApplicationStatus::kNotFound):!optional,"no_change_classification_preserved");
    Execute(pool,"RENAME TABLE im_private_messages TO owned_messages_preserved");
    const auto failed=app.ConfirmReceiver(1,92);
    Execute(pool,"RENAME TABLE owned_messages_preserved TO im_private_messages");
    check(failed.status==MessageApplicationStatus::kStorageError,"storage_failure_not_hidden");
    check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="12","all_rows_preserved");
// Included inside the fresh-schema probe after the original receiver checks.
if (enabled) {
    insert(20,11,92,1,0);
    insert(300,11,92,1,0);
    insert(400,11,92,1,0);
    for (unsigned id=200;id<250;++id) insert(id,11,92,1,0);
    std::atomic<unsigned> commits_seen{0}, replies_returned{0};
    std::atomic<bool> commit_held{false}, release_commit{false};
    adapter.SetReceiverConfirmationBeforeCommitHookForTest([&](tinyimx::MySqlConnection*){
        if (++commits_seen != 2) return;
        commit_held=true;
        auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(!release_commit&&std::chrono::steady_clock::now()<limit)std::this_thread::yield();
        Require(release_commit,"test commit gate timeout");
    });
    auto blocker=pool.Acquire();
    Require(blocker&&blocker->BeginTransaction()&&blocker->Execute(
        "UPDATE im_private_messages SET content='held' WHERE message_id=20"),"blocker setup");
    MessageMutationApplicationResult leader_result;
    std::jthread leader([&]{leader_result=app.ConfirmReceiver(20,92);});
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(pool.AvailableCount()!=6&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    Require(pool.AvailableCount()==6,"leader entered locked SQL");
    std::vector<std::pair<unsigned,unsigned>> requests;
    for(unsigned id=200;id<250;++id)requests.emplace_back(id,92);
    for(unsigned i=0;i<7;++i)requests.emplace_back(300,92);
    for(auto pair:std::vector<std::pair<unsigned,unsigned>>{{1,93},{3,92},{4,92},{9999,92},{7,92},{3,93},{4,93}})requests.push_back(pair);
    Require(requests.size()==64,"batch input size");
    std::vector<MessageMutationApplicationResult> replies(64);
    std::vector<std::jthread> waiters;
    for(std::size_t i=0;i<64;++i)waiters.emplace_back([&,i]{replies[i]=app.ConfirmReceiver(requests[i].first,requests[i].second);++replies_returned;});
    until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(adapter.ReceiverConfirmationQueuedForTest()!=64&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    // Always release blockers before asserting, so a failed check cannot hang join.
    bool queued=adapter.ReceiverConfirmationQueuedForTest()==64;
    MessageMutationApplicationResult overflow;
    if (queued) overflow=app.ConfirmReceiver(400,92);
    Require(blocker->Rollback(),"release blocker");blocker.Reset();leader.join();
    until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!commit_held&&std::chrono::steady_clock::now()<until)std::this_thread::yield();
    bool no_early_reply=commit_held&&replies_returned==0&&Scalar(pool,
        "SELECT COUNT(*) FROM im_private_messages WHERE (message_id BETWEEN 200 AND 249 OR message_id=300) AND delivery_status=0")=="51";
    release_commit=true;
    for(auto& t:waiters)t.join();
    adapter.SetReceiverConfirmationBeforeCommitHookForTest({});
    check(no_early_reply,"all_batch_results_wait_for_commit_and_pending_rows_not_visible_as_confirmed");
    check(queued,"already_arrived_64_requests_coalesced");
    check(overflow.status==MessageApplicationStatus::kStorageError&&Scalar(pool,
        "SELECT delivery_status FROM im_private_messages WHERE message_id=400")=="0","bounded_overflow_rejected_without_mutation");
    unsigned transitions=0;bool valid=leader_result.Succeeded();
    for(unsigned i=0;i<57;++i){valid=valid&&replies[i].Succeeded();transitions+=replies[i].affected_rows;}
    check(valid&&transitions==51&&Scalar(pool,
        "SELECT COUNT(*) FROM im_private_messages WHERE (message_id BETWEEN 200 AND 249 OR message_id=300) AND delivery_status=1 AND delivered_at IS NOT NULL")=="51","batch_51_unique_rows_commit_once_duplicates_idempotent");
    check(replies[57].status==MessageApplicationStatus::kPermissionDenied&&
          replies[58].Succeeded()&&replies[58].affected_rows==0&&
          replies[59].status==MessageApplicationStatus::kFailedPrecondition&&
          replies[60].status==MessageApplicationStatus::kNotFound&&
          replies[61].status==MessageApplicationStatus::kInvalidRecord&&
          replies[62].status==MessageApplicationStatus::kPermissionDenied&&
          replies[63].status==MessageApplicationStatus::kPermissionDenied,
          "mixed_batch_retains_per_request_owner_state_and_record_decisions");
    check(Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=3")=="2"&&
          Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=4")=="3",
          "mixed_batch_never_regresses_read_or_failed_state");
    insert(500,11,92,1,0);
    unsigned hooks=0;
    adapter.SetReceiverConfirmationBeforeCommitHookForTest([&](tinyimx::MySqlConnection* c){
        ++hooks;tinyimx::MySqlQueryResult cid;
        Require(c->Query("SELECT CONNECTION_ID()",&cid)&&cid.rows.size()==1,"owned connection id");
        Execute(pool,"KILL "+cid.rows[0][0]);
    });
    auto killed=app.ConfirmReceiver(500,92);
    adapter.SetReceiverConfirmationBeforeCommitHookForTest({});
    check(hooks==1&&killed.status==MessageApplicationStatus::kStorageError&&killed.affected_rows==0&&
          Scalar(pool,"SELECT delivery_status FROM im_private_messages WHERE message_id=500")=="0",
          "real_connection_loss_before_commit_fails_without_replay_or_early_ack");
    check(app.ConfirmReceiver(500,92).Succeeded(),"caller_retry_after_failed_transaction_recovers");
    result["fixture_rows"]=66;
}

    result["enabled"]=enabled;result["status"]="PASS";
}catch(const std::exception& e){result["failure"]=e.what();}
std::cout<<result.dump()<<'\n';return result["status"]=="PASS"?0:2;
}
