// Actual application + adapter + MySQL. Only a fresh generated two-table schema.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/repository/MessageRepository.h"
#include "services/outbox/OutboxRepository.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace tinyimx::message;
using Json=nlohmann::json;
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::string Scalar(tinyimx::MySqlConnectionPool& p,const std::string& sql){auto c=p.Acquire();tinyimx::MySqlQueryResult r;Require(c&&c->Query(sql,&r)&&r.rows.size()==1&&r.rows[0].size()==1,"owned scalar failed");return r.rows[0][0];}
void Execute(tinyimx::MySqlConnectionPool& p,const std::string& sql){auto c=p.Acquire();Require(c&&c->Execute(sql),"owned mutation failed");}
template<class Predicate>bool Wait(Predicate predicate){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));return predicate();}
int main(int argc,char** argv){Json result={{"issue_id","PERF-045"},{"status","FAIL"},{"checks",Json::array()}};
try{
    Require(argc==4,"config schema enabled");const std::string schema=argv[2];const bool enabled=std::string(argv[3])=="1";
    Require(schema.starts_with("codex_perf045_20261017_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"unowned schema");
    tinyimx::Config config;Require(config.LoadFromFile(argv[1]),"config failed");auto settings=config.MySql();settings.database=schema;settings.pool_size=8;
    tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(settings),"pool failed");
    Require(Scalar(pool,"SELECT DATABASE()")==schema&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="0"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="0","fresh owned fixture required");
    tinyimx::MessageRepository repo(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);
    MessageRepositoryAdapter adapter(&repo,&pool,&outbox);MessageApplicationService app(&adapter);
    auto check=[&](bool ok,const char* why){Require(ok,why);result["checks"].push_back(why);};
    auto send=[&](const std::string& cid,const std::string& body="payload",unsigned from=11,unsigned to=92,unsigned type=1){return app.PersistPrivateMessage(from,to,cid,type,body);};
    auto first=send("one");check(first.Created()&&first.message_id>0,"single_message_accepted_only_with_durable_row");
    check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="1"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="1","single_atomic_message_and_outbox");
    auto replay=send("one");check(replay.Reused()&&replay.message_id==first.message_id,"durable_cid_replay_reuses_mid");
    auto conflict=send("one","changed");check(conflict.Conflict()&&conflict.message_id==first.message_id,"durable_cid_conflict_no_mutation");
    check(!send("").Accepted()&&!send("empty","").Accepted()&&!send("type","payload",11,92,0).Accepted()&&!send("self","payload",11,11).Accepted(),"invalid_input_rejected");
    check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="1","invalid_replay_conflict_no_extra_outbox");
    if(enabled){
        std::atomic<unsigned> hook_count{0},returned{0};std::atomic<bool> first_held{false},second_held{false},release_first{false},release_second{false};
        adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection*){
            auto n=++hook_count;
            if(n==1){first_held=true;Require(Wait([&]{return release_first.load();}),"leader commit gate timeout");}
            if(n==2){second_held=true;Require(Wait([&]{return release_second.load();}),"batch commit gate timeout");}
        });
        PersistPrivateMessageApplicationResult leader_result;
        std::jthread leader([&]{leader_result=send("batch-leader");});
        bool held=Wait([&]{return first_held.load();});
        struct Request{std::string cid,body;unsigned from=11,to=92,type=1;};std::vector<Request> requests;
        for(unsigned i=0;i<48;++i)requests.push_back({i==47?"quote'key":"q"+std::to_string(i),i==47?"quoted 'body\nline":"payload"});
        for(unsigned i=0;i<8;++i)requests.push_back({"shared","payload"});
        for(auto r:std::vector<Request>{{"q0","payload"},{"conflict-shared","A"},{"conflict-shared","B"},{"one","payload"},{"one","changed"},{"one","payload",12},{"typed","payload",11,92,2},{"to93","payload",11,93}})requests.push_back(r);
        Require(requests.size()==64,"input size");std::vector<PersistPrivateMessageApplicationResult> replies(64);std::vector<std::jthread> waiters;
        for(std::size_t i=0;i<64;++i)waiters.emplace_back([&,i]{const auto& r=requests[i];replies[i]=send(r.cid,r.body,r.from,r.to,r.type);++returned;});
        bool queued=Wait([&]{return adapter.PrivatePersistQueuedForTest()==64;});
        PersistPrivateMessageApplicationResult overflow;if(queued)overflow=send("overload");
        bool early_leader_visible=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")!="1"||Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")!="1";
        release_first=true;leader.join();
        bool batch_held=Wait([&]{return second_held.load();});
        bool no_early_return=batch_held&&returned==0&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="2"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="2";
        release_second=true;for(auto& t:waiters)t.join();adapter.SetPrivatePersistBeforeCommitHookForTest({});
        check(held&&queued,"64_already_arrived_requests_use_one_bounded_queue");
        check(!early_leader_visible&&no_early_return,"no_message_or_outbox_visible_and_no_batch_success_before_commit");
        check(overflow.status==MessageApplicationStatus::kStorageError&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id='overload'")=="0","overload_rejected_without_write");
        unsigned created=0,reused=0,conflicts=0;for(auto& r:replies){created+=r.Created();reused+=r.Reused();conflicts+=r.Conflict();}
        check(leader_result.Created()&&created==53&&reused==9&&conflicts==2,"mixed_batch_new_duplicate_conflict_and_sender_identity_results");
        check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="55"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="55","53_new_records_and_events_commit_atomically_once");
        check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id='quote\\'key' AND content='quoted \\'body\\nline'")=="1","sql_escaping_and_exact_payload_preserved");
    }
    // Cross-adapter collision exercises the UNIQUE arbiter and read-only recovery.
    MessageRepositoryAdapter adapter2(&repo,&pool,&outbox);MessageApplicationService app2(&adapter2);
    std::barrier start(33);std::atomic<unsigned> race_created{0},race_errors{0};std::vector<std::jthread> racers;
    for(unsigned i=0;i<32;++i)racers.emplace_back([&,i]{start.arrive_and_wait();auto r=(i%2?app:app2).PersistPrivateMessage(11,92,"cross-adapter",1,"payload");race_created+=r.Created();race_errors+=!r.Accepted();});
    start.arrive_and_wait();for(auto& t:racers)t.join();
    check(race_created==1&&race_errors==0&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id='cross-adapter'")=="1","concurrent_cross_adapter_cid_exactly_one_created");
    auto before_messages=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages"),before_events=Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox");
    unsigned lost_hooks=0;
    adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection* c){++lost_hooks;tinyimx::MySqlQueryResult id;Require(c->Query("SELECT CONNECTION_ID()",&id)&&id.rows.size()==1,"own connection id");Execute(pool,"KILL "+id.rows[0][0]);});
    auto lost=send("lost-before");adapter.SetPrivatePersistBeforeCommitHookForTest({});
    check(lost_hooks==1&&!lost.Accepted()&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")==before_messages&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")==before_events,"pre_commit_connection_loss_rolls_back_both_no_automatic_replay");
    check(send("lost-before").Created(),"explicit_retry_after_rolled_back_transaction_creates_once");
    unsigned committed_hooks=0;
    adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection* c){++committed_hooks;Require(c->Commit(),"test commit before apparent response failure");});
    auto recovered=send("committed-before-error");adapter.SetPrivatePersistBeforeCommitHookForTest({});
    check(committed_hooks==1&&recovered.Reused()&&send("committed-before-error").message_id==recovered.message_id,"already_committed_outcome_resolved_by_cid_reads_without_reinsert");
    check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")==Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"),"all_new_messages_have_exactly_one_atomic_outbox_event");
    // Force an actual multi-request transaction to lose its connection at COMMIT.
    if(enabled){
        std::atomic<unsigned> calls{0};std::atomic<bool> held{false},release{false};
        adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection* c){
            auto n=++calls;if(n==1){held=true;Require(Wait([&]{return release.load();}),"rollback leader gate");}
            else {tinyimx::MySqlQueryResult id;Require(c->Query("SELECT CONNECTION_ID()",&id),"rollback batch id");Execute(pool,"KILL "+id.rows[0][0]);}
        });
        PersistPrivateMessageApplicationResult lead;std::jthread leader([&]{lead=send("rollback-leader");});
        bool entered=Wait([&]{return held.load();});std::vector<PersistPrivateMessageApplicationResult> replies(16);std::vector<std::jthread> jobs;
        for(unsigned i=0;i<16;++i)jobs.emplace_back([&,i]{replies[i]=send("rollback-q"+std::to_string(i));});
        bool queued=Wait([&]{return adapter.PrivatePersistQueuedForTest()==16;});release=true;leader.join();for(auto& t:jobs)t.join();adapter.SetPrivatePersistBeforeCommitHookForTest({});
        bool all_failed=true;for(auto& r:replies)all_failed=all_failed&&!r.Accepted();
        check(entered&&queued&&lead.Created()&&calls==2&&all_failed&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'rollback-q%'")=="0","multi_request_commit_failure_rolls_back_every_new_row_without_replay");
        for(unsigned i=0;i<16;++i)Require(send("rollback-q"+std::to_string(i)).Created(),"explicit batch retry");
        check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")==Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"),"explicit_retries_restore_complete_message_event_pairs");
    }
    result["enabled"]=enabled;result["final_messages"]=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages");result["final_outbox_events"]=Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox");result["status"]="PASS";
}catch(const std::exception& e){result["failure"]=e.what();}
std::cout<<result.dump()<<'\n';return result["status"]=="PASS"?0:2;
}
