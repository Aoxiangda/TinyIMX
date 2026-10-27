// Actual MySQL batch mutation invariants, strictly owned generated schema only.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/outbox/OutboxRepository.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <set>
using Json=nlohmann::json;using Record=tinyimx::outbox::OutboxRecord;
void Check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
std::string Scalar(tinyimx::MySqlConnectionPool&p,const std::string&q){auto c=p.Acquire();tinyimx::MySqlQueryResult r;Check(c&&c->Query(q,&r)&&r.rows.size()==1&&r.rows[0].size()==1,"Owned scalar failed");return r.rows[0][0];}
void Execute(tinyimx::MySqlConnectionPool&p,const std::string&q){auto c=p.Acquire();Check(c&&c->Execute(q),"Owned fixture mutation failed");}
Record Identity(std::uint64_t id,std::string token){Record r;r.outbox_id=id;r.locked_by=std::move(token);return r;}
int main(int argc,char**argv){Json out={{"issue_id","PERF-028"},{"status","FAIL"}};try{
    Check(argc==3,"probe config owned-schema");std::string schema=argv[2];Check(schema.starts_with("codex_perf028_20261017_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"Refuse unowned schema");
    tinyimx::Config config;Check(config.LoadFromFile(argv[1]),"Config failed");auto db=config.MySql();db.database=schema;db.pool_size=4;tinyimx::MySqlConnectionPool pool;Check(pool.Initialize(db),"Owned pool failed");
    Check(Scalar(pool,"SELECT DATABASE()")==schema&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="8","Fresh eight-row fixture required");tinyimx::outbox::OutboxRepository repo(&pool);
    Check(!repo.MarkPublishedBatch({}).success,"Empty batch rejected");
    std::vector<Record> oversized;for(int i=1;i<=65;i++)oversized.push_back(Identity(i,"owner-a"));Check(!repo.MarkPublishedBatch(oversized).success,"Oversized batch rejected");
    Check(!repo.MarkPublishedBatch({Identity(1,"owner-a"),Identity(1,"owner-b")}).success,"Duplicate IDs reject whole batch");
    Check(!repo.MarkPublishedBatch({Identity(1,"owner-a"),Identity(2,"")}).success,"Missing owner rejects whole batch");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE status=1")=="1","Invalid batches mutate nothing");
    auto partial=repo.MarkPublishedBatch({Identity(1,"owner-a"),Identity(2,"stale-owner"),Identity(3,"owner-c"),Identity(4,"owner-d")});
    Check(partial.success&&partial.affected_rows==1&&partial.ownership_lost_rows==3,"Partial owner/status matches counted exactly");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE outbox_id=1 AND status=1 AND attempt_count=1 AND locked_by IS NULL AND locked_until IS NULL AND published_at IS NOT NULL AND last_error=''")=="1","Published state fields complete");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE outbox_id=2 AND status=2 AND locked_by='owner-b' AND attempt_count=0")=="1","Replacement retry owner protected");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE outbox_id=3 AND status=1 AND attempt_count=0")=="1"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE outbox_id=4 AND status=3 AND attempt_count=0")=="1","Published/quarantined rows protected");
    Execute(pool,"UPDATE im_event_outbox SET locked_by=CONVERT(0x6f776e2765725c746f6b656e USING utf8mb4) WHERE outbox_id=5");
    auto escaped=repo.MarkPublishedBatch({Identity(5,"own'er\\token"),Identity(6,"owner-f")});
    Check(escaped.success&&escaped.affected_rows==2&&escaped.ownership_lost_rows==0,"Mixed tokens escaped and published atomically");
    auto replay=repo.MarkPublishedBatch({Identity(5,"own'er\\token"),Identity(6,"owner-f")});Check(replay.success&&replay.affected_rows==0&&replay.ownership_lost_rows==2,"Repeated batch cannot increment published attempts");
    // A rejected DB mark leaves the durable claim available after expiry.
    auto rejected=repo.MarkPublishedBatch({Identity(7,"owner-g"),Identity(0,"owner-invalid")});Check(!rejected.success,"Injected invalid mark batch rejected");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE outbox_id=7 AND status=0 AND locked_by='owner-g'")=="1","Rejected batch retains pending durable event");
    Execute(pool,"UPDATE im_event_outbox SET locked_until=NOW(3)-INTERVAL 1 SECOND WHERE outbox_id=7");
    auto recovered=repo.ClaimBatch("recovery-owner",1,5000);Check(recovered.success&&recovered.records.size()==1&&recovered.records[0].outbox_id==7,"Unmarked event recoverable after lease expires");
    auto stale=repo.MarkPublishedBatch({Identity(7,"owner-g"),Identity(8,"wrong-owner")});Check(stale.success&&stale.affected_rows==0&&stale.ownership_lost_rows==2,"Old batch cannot publish replacement lease");
    auto final=repo.MarkPublishedBatch(recovered.records);Check(final.success&&final.affected_rows==1,"Recovered single tail event publishes without fill wait");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE outbox_id=8 AND status=0 AND locked_by='owner-h'")=="1","Unrelated leased event unchanged");
    Check(Scalar(pool,"SELECT @@session.transaction_isolation")=="REPEATABLE-READ","Claim override did not alter session isolation default");
    std::string values;
    for(int id=9;id<=40;++id){if(!values.empty())values+=",";const auto key=std::to_string(id);
        values+="("+key+",'parallel-"+key+"','test.v1',1,'test','"+key+"','probe','probe','','parallel-"+key+"',JSON_OBJECT(),0)";}
    Execute(pool,"INSERT INTO im_event_outbox(outbox_id,event_id,event_type,schema_version,aggregate_type,aggregate_id,producer_service,topic,tag,message_key,payload,status)VALUES"+values);
    tinyimx::outbox::OutboxClaimResult one,two;
    std::thread first([&]{one=repo.ClaimBatch("parallel-one",16,5000);});
    std::thread second([&]{two=repo.ClaimBatch("parallel-two",16,5000);});
    first.join();second.join();
    out["concurrent_claim_attempts"]={
        {"one",{{"success",one.success},{"rows",one.records.size()},{"message",one.message}}},
        {"two",{{"success",two.success},{"rows",two.records.size()},{"message",two.message}}}};
    Check(one.success&&two.success,"Concurrent claim statements succeed");
    std::set<std::uint64_t> claimed;
    for(const auto& item:one.records)Check(item.locked_by=="parallel-one"&&claimed.insert(item.outbox_id).second,"First claim token and exclusivity");
    for(const auto& item:two.records)Check(item.locked_by=="parallel-two"&&claimed.insert(item.outbox_id).second,"Second claim token and exclusivity");
    // SKIP LOCKED may legitimately return fewer rows while the other claim
    // holds locks. Check recovery of the remaining ready rows separately;
    // do not equate an underfilled concurrent batch with a lost event.
    auto remainder=repo.ClaimBatch("parallel-remainder",32,5000);
    out["concurrent_claim_attempts"]["remainder"]={{"success",remainder.success},{"rows",remainder.records.size()},{"message",remainder.message}};
    Check(remainder.success,"Remaining ready rows can be claimed");
    for(const auto& item:remainder.records)Check(item.locked_by=="parallel-remainder"&&claimed.insert(item.outbox_id).second,"Remainder exclusive and correctly owned");
    Check(claimed.size()==32&&*claimed.begin()==9&&*claimed.rbegin()==40,"Claimed exact owned ready range");
    auto no_ready=repo.ClaimBatch("parallel-third",32,5000);Check(no_ready.success&&no_ready.records.empty(),"Unexpired concurrent claims cannot be stolen");
    Check(Scalar(pool,"SELECT @@session.transaction_isolation")=="REPEATABLE-READ","Empty and concurrent claims preserve session default");
    out["checks"]={"invalid_no_mutation","max_batch","duplicate_identity","per_row_owner_fence","partial_outcomes","protected_states","escaped_mixed_tokens","published_fields","repeat_no_increment","unmarked_retained","lease_expiry_recovery","stale_recovery_owner","single_tail","unrelated_lease"};
    out["checks"].push_back("claim_session_default_preserved");out["checks"].push_back("concurrent_claim_exclusivity");out["checks"].push_back("exact_ready_range");out["checks"].push_back("unexpired_claim_not_stolen");
    out["status"]="PASS";pool.Shutdown();
}catch(const std::exception&e){out["failure"]=e.what();}std::cout<<out.dump(2)<<'\n';return out["status"]=="PASS"?0:2;}
