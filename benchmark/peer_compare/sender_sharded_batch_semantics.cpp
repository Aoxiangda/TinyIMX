// Actual application + adapter + MySQL. Only a fresh generated two-table schema.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/application/MessageEventFactory.h"
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
int main(int argc,char** argv){Json result={{"issue_id","PERF-047"},{"status","FAIL"},{"checks",Json::array()}};
try{
    Require(argc==4,"config schema enabled");const std::string schema=argv[2];const bool enabled=std::string(argv[3])=="1";
    Require(schema.starts_with("codex_perf047_20261017_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"unowned schema");
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
        for(auto r:std::vector<Request>{{"q0","payload"},{"conflict-shared","A"},{"conflict-shared","B"},{"one","payload"},{"one","changed"},{"one","payload",15},{"typed","payload",11,92,2},{"to93","payload",11,93}})requests.push_back(r);
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

    if(enabled){
        // Force a distinct canonical-CID batch, independent of scheduler timing.
        auto queued_batch=[&](const std::string& prefix,const std::vector<std::string>& bodies){
            std::atomic<unsigned> calls{0},returned{0};
            std::atomic<bool> first_held{false},second_held{false},release_first{false},release_second{false};
            auto old_messages=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages");
            auto old_events=Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox");
            adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection*){
                auto n=++calls;
                if(n==1){first_held=true;Require(Wait([&]{return release_first.load();}),"canonical leader gate");}
                if(n==2){second_held=true;Require(Wait([&]{return release_second.load();}),"canonical batch gate");}
            });
            PersistPrivateMessageApplicationResult lead;std::jthread leader([&]{lead=send(prefix+"leader");});
            bool entered=Wait([&]{return first_held.load();});
            std::vector<PersistPrivateMessageApplicationResult> replies(bodies.size());std::vector<std::jthread> jobs;
            for(unsigned i=0;i<bodies.size();++i)jobs.emplace_back([&,i]{replies[i]=send(prefix+std::to_string(i),bodies[i]);++returned;});
            bool queued=Wait([&]{return adapter.PrivatePersistQueuedForTest()==bodies.size();});
            bool first_hidden=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")==old_messages&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")==old_events;
            release_first=true;leader.join();bool batch_held=Wait([&]{return second_held.load();});
            bool hidden=batch_held&&returned==0&&std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages"))==std::stoull(old_messages)+1&&std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"))==std::stoull(old_events)+1;
            release_second=true;for(auto& t:jobs)t.join();adapter.SetPrivatePersistBeforeCommitHookForTest({});
            Require(entered&&queued&&first_hidden&&hidden&&lead.Created()&&calls==2,"canonical commit-gate and bounded batch");
            for(unsigned i=0;i<replies.size();++i){
                Require(replies[i].Created(),"canonical grouped result failed");
                auto found=repo.FindPrivateMessageByClientMessageId(11,prefix+std::to_string(i));
                Require(found.Succeeded()&&found.Found()&&found.record.message_id==replies[i].message_id&&found.record.content==bodies[i],"canonical exact read-back");
            }
            Require(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")==Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"),"canonical message/event pairs");
        };
        std::vector<std::string> bodies(16,"quoted ' ; SELECT 1; \\ content");bodies[0]=std::string("before\0after",12);
        queued_batch("canonical-",bodies);check(true,"canonical_batch_preserves_quotes_semicolons_nul_and_commit_boundary");
        queued_batch("chunked-",std::vector<std::string>(32,std::string(60000,'x')));
        check(true,"multi_command_chunk_boundaries_preserve_last_insert_id_and_all_payloads");

        auto c=pool.Acquire();Require(c&&c->BeginTransaction(),"grouped API fixture transaction");
        auto before=c->GroupedStatementCommandAttempts();
        std::vector<std::pair<std::uint64_t,std::string>> identities;
        std::vector<tinyimx::PrivateMessageRecord> inputs;
        for(unsigned i=0;i<16;++i){
            const auto cid="api-"+std::to_string(i);identities.emplace_back(11,cid);
            tinyimx::PrivateMessageRecord r;r.from_user_id=11;r.to_user_id=92;r.client_message_id=cid;r.message_type=1;r.content="api payload";r.delivery_status=0;inputs.push_back(r);
        }
        auto found=repo.FindPrivateMessageCidsOnConnection(c.operator->(),identities);
        Require(found.size()==16,"grouped lookup size");for(const auto& f:found)Require(f.Succeeded()&&!f.Found(),"grouped lookup result");
        auto inserted=repo.SaveAndFindPrivateMessagesOnConnection(c.operator->(),inputs);
        Require(inserted.Succeeded()&&inserted.records.size()==16,"grouped insert verification");
        std::vector<tinyimx::outbox::OutboxInsertSpec> events;
        for(const auto& r:inserted.records){
            tinyimx::message::MessageView view;view.message_id=r.message_id;view.client_message_id=r.client_message_id;view.from_user_id=r.from_user_id;view.to_user_id=r.to_user_id;view.message_type=r.message_type;view.content=r.content;view.created_at=r.created_at;
            auto e=tinyimx::message::MessageEventFactory::MessageCreated(view);events.push_back({e.event,e.topic,e.tag,e.message_key});
        }
        auto out=outbox.InsertBatchOnConnection(c.operator->(),events);Require(out.size()==16,"grouped outbox size");for(const auto& r:out)Require(r.success,"grouped outbox result");
        check(c->GroupedStatementCommandAttempts()-before==3,"16_messages_use_3_com_query_attempts_not_64_round_trips");
        Require(c->Rollback(),"API rollback");c.Reset();
        check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'api-%'")=="0","caller_owned_rollback_covers_all_grouped_commands");

        auto rejected=[&](const std::vector<tinyimx::MySqlTransactionStatement>& statements){
            auto lease=pool.Acquire();Require(lease&&lease->BeginTransaction(),"protocol failure transaction");
            Require(lease->Execute("INSERT INTO im_private_messages (from_user_id,to_user_id,client_message_id,message_type,content,delivery_status) VALUES (11,92,'protocol-uncommitted',1,'body',0)"),"protocol uncommitted row");
            std::vector<tinyimx::MySqlTransactionStatementResult> rows;
            Require(!lease->ExecuteTransactionStatements(statements,&rows)&&!lease->IsConnected(),"uncertain protocol must discard session");
            lease.Reset();Require(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id='protocol-uncommitted'")=="0","discarded transaction must roll back");
        };
        rejected({{"SELECT 1",true},{"SELECT missing_column FROM im_private_messages",true}});
        check(true,"middle_sql_error_closes_session_and_rolls_back_without_write_replay");
        rejected({{"SELECT 1",false}});rejected({{"SELECT 1 UNION ALL SELECT 2",true}});
        rejected({{"SELECT 1; SELECT 2",true}});
        check(true,"unexpected_result_type_extra_result_and_multirow_rejected_pool_recovers");
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

    if(enabled&&std::getenv("TINYIMX_PRIVATE_PERSIST_SHARDS")){
        std::atomic<unsigned> calls{0};std::atomic<bool> held{false},release{false},other_returned{false};
        adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection*){
            if(++calls==1){held=true;Require(Wait([&]{return release.load();}),"independent lane gate");}
        });
        PersistPrivateMessageApplicationResult one,two;
        std::jthread lead([&]{one=send("independent-one","payload",11);});
        bool entered=Wait([&]{return held.load();});
        std::jthread other([&]{two=send("independent-two","payload",12);other_returned=true;});
        bool independently_committed=Wait([&]{return other_returned.load();});
        bool pending_hidden=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id='independent-one'")=="0";
        release=true;lead.join();other.join();adapter.SetPrivatePersistBeforeCommitHookForTest({});
        check(entered&&independently_committed&&pending_hidden&&one.Created()&&two.Created(),"blocked_sender_lane_does_not_block_another_lane_or_expose_uncommitted_row");

        std::atomic<unsigned> hooks{0},returned{0};std::atomic<bool> release_all{false};
        adapter.SetPrivatePersistBeforeCommitHookForTest([&](tinyimx::MySqlConnection*){
            if(++hooks<=4)Require(Wait([&]{return release_all.load();}),"four lane gate");
        });
        std::vector<PersistPrivateMessageApplicationResult> leaders(4),replies(256);std::vector<std::jthread> leads,jobs;
        for(unsigned i=0;i<4;++i)leads.emplace_back([&,i]{leaders[i]=send("four-leader-"+std::to_string(i),"payload",11+i);});
        bool all_held=Wait([&]{return hooks==4;});
        for(unsigned i=0;i<256;++i)jobs.emplace_back([&,i]{replies[i]=send("four-q-"+std::to_string(i),"payload",11+i%4);++returned;});
        bool all_queued=Wait([&]{return adapter.PrivatePersistQueuedForTest()==256;});
        bool rejected=true;if(all_queued)for(unsigned i=0;i<4;++i)rejected=rejected&&!send("four-overflow-"+std::to_string(i),"payload",11+i).Accepted();
        bool none_visible=returned==0&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'four-%'")=="0";
        release_all=true;for(auto& t:leads)t.join();for(auto& t:jobs)t.join();adapter.SetPrivatePersistBeforeCommitHookForTest({});
        bool all_created=true;for(auto& r:leaders)all_created=all_created&&r.Created();for(auto& r:replies)all_created=all_created&&r.Created();
        check(all_held&&all_queued&&rejected,"four_lanes_each_bounded64_aggregate256_excess_rejected_without_write");
        check(none_visible&&all_created&&Scalar(pool,"SELECT COUNT(*) FROM im_private_messages WHERE client_message_id LIKE 'four-%'")=="260","four_lane_commit_boundaries_and_all_260_results_verified");
        check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")==Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"),"all_sharded_messages_keep_exactly_one_outbox_event");
    }

    result["enabled"]=enabled;result["final_messages"]=Scalar(pool,"SELECT COUNT(*) FROM im_private_messages");result["final_outbox_events"]=Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox");result["status"]="PASS";
}catch(const std::exception& e){result["failure"]=e.what();}
std::cout<<result.dump()<<'\n';return result["status"]=="PASS"?0:2;
}
