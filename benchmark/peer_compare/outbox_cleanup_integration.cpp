// Real OutboxRepository integration probe. Requires a fresh, strictly owned schema.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/outbox/OutboxRepository.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <nlohmann/json.hpp>

using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
std::string Scalar(tinyimx::MySqlConnectionPool& pool, const std::string& sql) {
    auto c = pool.Acquire(); tinyimx::MySqlQueryResult r;
    Check(c && c->Query(sql, &r) && r.rows.size() == 1 && r.rows[0].size() == 1, "Scalar query failed");
    return r.rows[0][0];
}
Json Stats(tinyimx::MySqlConnectionPool& pool) {
    auto c = pool.Acquire(); tinyimx::MySqlQueryResult r;
    Check(c && c->Query("SHOW SESSION STATUS LIKE 'Handler_read%'", &r), "Handler stats failed");
    Json j=Json::object(); for (const auto& row:r.rows) j[row[0]]=std::stoull(row[1]); return j;
}
int main(int argc, char** argv) {
    Json output={{"issue_id","PERF-020"},{"status","ABORTED"}};
    try {
        Check(argc==4,"Usage: probe private-config owned-schema semantic|bench");
        std::string schema=argv[2], mode=argv[3];
        Check(schema.starts_with("codex_perf020_20261017_") && schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,
              "Refuse unowned schema");
        Check(mode=="semantic" || mode=="bench" || mode=="claimbench","Unknown mode");
        tinyimx::Config config; Check(config.LoadFromFile(argv[1]),"Private config load failed");
        auto db=config.MySql(); db.database=schema; db.pool_size=mode=="semantic"?4:1;
        tinyimx::MySqlConnectionPool pool; Check(pool.Initialize(db),"Owned pool initialize failed");
        Check(Scalar(pool,"SELECT DATABASE()") == schema,"Schema identity mismatch");
        tinyimx::outbox::OutboxRepository repo(&pool);
        output["schema"]=schema; output["mode"]=mode;
        output["rows_before"]=std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"));
        if (mode=="semantic") {
            Check(output["rows_before"]==8,"Expected fresh semantic fixture");
            Check(!repo.CleanupPublished(0,1).success && !repo.CleanupPublished(168,0).success &&
                  !repo.CleanupPublished(168,10001).success,"Invalid input must fail");
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="8","Invalid input mutated rows");
            auto first=repo.CleanupPublished(168,1);
            Check(first.success && first.deleted_rows==1,"First bounded cleanup");
            // Both versions must delete only eligible published rows; ordering intentionally differs.
            Check(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox WHERE status=1 AND published_at < NOW(3)-INTERVAL 168 HOUR")=="2","Eligibility count");
            auto second=repo.CleanupPublished(168,1000);
            Check(second.success && second.deleted_rows==2,"Remaining expired batch");
            auto empty=repo.CleanupPublished(168,1000);
            Check(empty.success && empty.deleted_rows==0,"No expired rows left");
            Check(Scalar(pool,"SELECT GROUP_CONCAT(outbox_id ORDER BY outbox_id) FROM im_event_outbox")=="3,4,5,6,7",
                  "Recent/null publication and pending/retry/quarantine must survive");
            auto claim=repo.ClaimBatch("codex-perf020-owned",10,5000);
            Check(claim.success && claim.records.size()==2,"Pending/retry claim regression");
            std::vector<std::uint64_t> ids; for(const auto&r:claim.records) ids.push_back(r.outbox_id);
            std::sort(ids.begin(),ids.end()); Check(ids==std::vector<std::uint64_t>{4,5},"Only pending/retry claimable");
            for(const auto&r:claim.records) {
                Check(!repo.ReleaseClaim(r.outbox_id,"wrong-owner").success,"Foreign lease must fail");
                Check(repo.ReleaseClaim(r.outbox_id,"codex-perf020-owned").success,"Owned lease release");
            }
            Check(!repo.ClaimBatch("",1,1000).success && !repo.ClaimBatch("a",0,1000).success &&
                  !repo.ClaimBatch("a",1025,1000).success && !repo.ClaimBatch("a",1,0).success,"Claim invalid inputs");
            auto execute=[&](const std::string&sql){auto c=pool.Acquire();Check(c&&c->Execute(sql),"Owned fixture mutation");};
            execute("UPDATE im_event_outbox SET next_attempt_at=NOW(3)+INTERVAL 1 HOUR WHERE outbox_id=4");
            execute("UPDATE im_event_outbox SET locked_by='foreign',locked_until=NOW(3)+INTERVAL 1 HOUR WHERE outbox_id=5");
            auto unavailable=repo.ClaimBatch("no-ready",10,5000);
            Check(unavailable.success&&unavailable.records.empty(),"Future retries and active leases excluded");
            execute("UPDATE im_event_outbox SET locked_until=NOW(3)-INTERVAL 1 SECOND WHERE outbox_id=5");
            auto expired=repo.ClaimBatch("expired-owner",1,5000);
            Check(expired.success&&expired.records.size()==1&&expired.records[0].outbox_id==5,"Expired lease recoverable");
            auto duplicate=repo.ClaimBatch("other-owner",10,5000);
            Check(duplicate.success&&duplicate.records.empty(),"Active claim is not reclaimed");
            Check(repo.RenewClaim(5,"expired-owner",5000).success,"Live owner can renew");
            Check(repo.MarkRetry(5,"expired-owner",60000,"injected transport error").success,"Retry release with bounded delay");
            auto delayed=repo.ClaimBatch("before-retry-due",10,5000);
            Check(delayed.success&&delayed.records.empty(),"Retry waits until due");
            execute("UPDATE im_event_outbox SET next_attempt_at=NOW(3)-INTERVAL 1 SECOND WHERE outbox_id=4");
            std::array<tinyimx::outbox::OutboxClaimResult,2> concurrent;
            std::jthread left([&]{concurrent[0]=repo.ClaimBatch("concurrent-left",1,5000);});
            std::jthread right([&]{concurrent[1]=repo.ClaimBatch("concurrent-right",1,5000);});left.join();right.join();
            Check(concurrent[0].success&&concurrent[1].success,"Concurrent claims complete");
            Check(concurrent[0].records.size()+concurrent[1].records.size()==1,"Concurrent claims lease available event once");
            auto winner=concurrent[0].records.empty()?concurrent[1].records[0]:concurrent[0].records[0];
            Check(winner.outbox_id==4,"Concurrent claim eligibility");
            execute("UPDATE im_event_outbox SET locked_by='replacement-owner' WHERE outbox_id=4");
            Check(!repo.RenewClaim(4,winner.locked_by,5000).success && !repo.MarkPublished(4,winner.locked_by).success &&
                  !repo.ReleaseClaim(4,winner.locked_by).success,"Stale owner cannot renew/publish/release replacement lease");
            Check(repo.MarkPublished(4,"replacement-owner").success,"Current owner may publish");
            output["checks"]={"invalid_inputs","retention","bounded_batches","protected_statuses","null_publication","claim_retry","lease_ownership",
                "future_due_exclusion","active_lease_exclusion","expired_lease_recovery","renewal","delayed_retry","concurrent_unique_claim","replacement_owner_protection"};
        } else if (mode=="claimbench") {
            Check(output["rows_before"].get<std::uint64_t>()>=100000,"Representative claim fixture missing");
            std::vector<double> samples; Json claimed=Json::array();
            const auto before=Stats(pool);
            for(int i=0;i<16;i++) {
                const auto started=Clock::now(); auto claim=repo.ClaimBatch("owned-claim-benchmark",32,5000);
                samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-started).count());
                Check(claim.success&&claim.records.size()==32,"Expected full claim batch");
                Json ids=Json::array();for(const auto&r:claim.records) {
                    ids.push_back(r.outbox_id);Check(repo.ReleaseClaim(r.outbox_id,"owned-claim-benchmark").success,"Benchmark release");
                }
                if(i==0)claimed=ids;else Check(ids==claimed,"Stable oldest-ID selection");
            }
            const auto after=Stats(pool);Json diff=Json::object();
            for(auto it=after.begin();it!=after.end();++it)diff[it.key()]=it.value().get<std::uint64_t>()-before[it.key()].get<std::uint64_t>();
            output["handler_read_delta"]=diff;output["claim_ms"]=samples;output["selected_ids"]=claimed;
            output["mean_claim_ms"]=std::accumulate(samples.begin(),samples.end(),0.0)/samples.size();
            output["checks"]={"full_batch","stable_oldest_ids","owned_release","future_and_leased_prefix_fixture"};
        } else {
            Check(output["rows_before"].get<std::uint64_t>()>=100000,"Representative fixture missing");
            tinyimx::MySqlConnection writer; Check(writer.Connect(db),"Writer connect");
            std::atomic<bool> begun=false; std::vector<double> insert_ms; std::atomic<int> writer_errors=0;
            std::jthread t([&]{
                while(!begun.load()) std::this_thread::yield();
                auto start=Clock::now();
                for(int i=0;i<300;i++) {
                    std::this_thread::sleep_until(start+std::chrono::milliseconds(i*10));
                    const auto before=Clock::now();
                    tinyimx::eventing::DomainEvent e; e.event_id="codex-insert-"+std::to_string(i);
                    e.event_type="test.created.v1"; e.aggregate_type="test"; e.aggregate_id=std::to_string(i);
                    e.producer_service="codex-probe"; e.occurred_at="2026-10-17T00:00:00.000Z";
                    if(!writer.BeginTransaction()) { writer_errors++; continue; }
                    auto r=repo.InsertOnConnection(&writer,e,"codex-perf020","test",e.event_id);
                    if(!r.success || !writer.Commit()) { writer.Rollback(); writer_errors++; }
                    insert_ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-before).count());
                }
            });
            auto before=Stats(pool); begun=true;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const auto start=Clock::now(); auto cleanup=repo.CleanupPublished(168,1000);
            output["cleanup_ms"]=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
            auto after=Stats(pool); Json diff=Json::object();
            for(auto it=after.begin();it!=after.end();++it) diff[it.key()]=it.value().get<std::uint64_t>()-before[it.key()].get<std::uint64_t>();
            output["handler_read_delta"]=diff;
            // Own connection's latest DELETE only; no other users' query text is exported.
            auto c=pool.Acquire(); tinyimx::MySqlQueryResult history;
            Check(c && c->Query("SELECT ROWS_EXAMINED,ROWS_AFFECTED,TIMER_WAIT,LOCK_TIME FROM performance_schema.events_statements_history WHERE THREAD_ID=(SELECT THREAD_ID FROM performance_schema.threads WHERE PROCESSLIST_ID=CONNECTION_ID()) AND SQL_TEXT LIKE 'DELETE%im_event_outbox%' ORDER BY EVENT_ID DESC LIMIT 1",&history),"Own statement history");
            output["own_delete_statement_counters"]=history.rows; c.Reset();
            t.join();
            Check(cleanup.success && cleanup.deleted_rows==1000,"Batch cleanup failed");
            Check(writer_errors==0 && insert_ms.size()==300,"Concurrent inserts failed");
            std::sort(insert_ms.begin(),insert_ms.end()); output["insert_ms_sorted"]=insert_ms;
            output["insert_p99_ms"]=insert_ms[296]; output["insert_max_ms"]=insert_ms.back();
            output["concurrent_inserts"]=300; output["deleted_rows"]=cleanup.deleted_rows;
        }
        output["rows_after"]=std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"));
        pool.Shutdown(); output["status"]="PASS";
    } catch(const std::exception&e) { output["status"]="FAIL"; output["failure"]=e.what(); }
    std::cout<<output.dump(2)<<'\n'; return output["status"]=="PASS" ? 0:2;
}
