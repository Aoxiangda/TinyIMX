// Actual frozen MessageRepositoryAdapter path, strictly owned empty schema only.
// Closed-loop storage-stage calibration; excludes Gateway/RPC/MQ/Redis/receiver.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/MessageRepository.h"
#include "services/outbox/OutboxRepository.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>
#include <vector>
using Json=nlohmann::json;using Clock=std::chrono::steady_clock;
void Check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
std::string Scalar(tinyimx::MySqlConnectionPool&p,const std::string&sql){auto c=p.Acquire();tinyimx::MySqlQueryResult r;Check(c&&c->Query(sql,&r)&&r.rows.size()==1&&r.rows[0].size()==1,"Owned scalar failed");return r.rows[0][0];}
int main(int argc,char**argv){Json out={{"issue_id","PERF-027"},{"status","FAIL"}};
 try{
    Check(argc==5,"probe private-config owned-schema threads seconds");std::string schema=argv[2];int threads=std::stoi(argv[3]),seconds=std::stoi(argv[4]);
    Check(schema.starts_with("codex_perf027_20261017_")&&schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")==std::string::npos,"Refuse unowned schema");
    Check(threads>=1&&threads<=32&&seconds>=5&&seconds<=30,"Bounded writer calibration only");
    tinyimx::Config config;Check(config.LoadFromFile(argv[1]),"Private config load failed");auto db=config.MySql();db.database=schema;db.pool_size=threads+2;
    tinyimx::MySqlConnectionPool pool;Check(pool.Initialize(db),"Owned pool failed");Check(Scalar(pool,"SELECT DATABASE()")==schema,"Schema mismatch");
    Check(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages")=="0"&&Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox")=="0","Fresh generated fixture only");
    tinyimx::MessageRepository repository(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);tinyimx::message::MessageRepositoryAdapter adapter(&repository,&pool,&outbox);
    std::barrier start(threads+1);std::atomic<bool> abort=false;std::atomic<int> errors=0;std::vector<std::vector<double>> latency(threads);std::vector<std::jthread> writers;Clock::time_point deadline;
    for(int t=0;t<threads;t++)writers.emplace_back([&,t]{auto&samples=latency[t];samples.reserve(100000);start.arrive_and_wait();std::uint64_t sequence=0;
      while(Clock::now()<deadline&&!abort.load()){
        auto from=500001+t*2,to=from+1;auto cid="dbcap-t"+std::to_string(t)+"-n"+std::to_string(sequence++);const std::string content=Json{{"from",from},{"to",to},{"text",std::string(128,'x')}}.dump();
        const auto before=Clock::now();auto result=adapter.PersistPrivateMessage(from,to,cid,1,content);
        const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-before).count();
        if(!result.Accepted()||result.outcome!=tinyimx::message::PersistPrivateMessageOutcome::kCreated||result.message_id==0||result.record.client_message_id!=cid||result.record.from_user_id!=static_cast<std::uint64_t>(from)||result.record.to_user_id!=static_cast<std::uint64_t>(to)||result.record.content!=content){errors++;abort=true;break;}
        if(samples.size()>=250000){errors++;abort=true;break;}samples.push_back(elapsed);
      }
    });
    const auto began=Clock::now();deadline=began+std::chrono::seconds(seconds);start.arrive_and_wait();for(auto&t:writers)t.join();const auto elapsed=std::chrono::duration<double>(Clock::now()-began).count();
    std::vector<double> all;for(const auto&v:latency)all.insert(all.end(),v.begin(),v.end());std::sort(all.begin(),all.end());
    out["threads"]=threads;out["configured_seconds"]=seconds;out["elapsed_seconds"]=elapsed;out["operations"]=all.size();out["errors"]=errors.load();out["completed_per_s"]=all.size()/elapsed;
    out["message_rows"]=std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages"));out["outbox_rows"]=std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_event_outbox"));
    out["atomic_event_join_count"]=std::stoull(Scalar(pool,"SELECT COUNT(*) FROM im_private_messages m JOIN im_event_outbox e ON e.event_id=CONCAT('message.created.v1:',m.message_id) COLLATE utf8mb4_unicode_ci AND e.aggregate_id=CAST(m.message_id AS CHAR) COLLATE utf8mb4_unicode_ci AND e.aggregate_type='private_message' AND e.status=0"));
    if(!all.empty()){out["p50_ms"]=all[(all.size()-1)/2];out["p99_ms"]=all[(all.size()*99+99)/100-1];out["max_ms"]=all.back();out["mean_ms"]=std::accumulate(all.begin(),all.end(),0.0)/all.size();}
    out["scope"]="Closed-loop actual message+Outbox transactional repository capacity in fresh generated tables, all current indexes. No RPC/permissions/Redis/MQ/receiver confirmations; not whole-system QPS or an industry guarantee.";
    out["insert_first_env"]=std::getenv("TINYIMX_PRIVATE_INSERT_FIRST_ENABLE")?std::getenv("TINYIMX_PRIVATE_INSERT_FIRST_ENABLE"):"unset";
    out["begin_insert_read_batch_env"]=std::getenv("TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE")?std::getenv("TINYIMX_PRIVATE_BEGIN_INSERT_READ_BATCH_ENABLE"):"unset";
    Check(errors==0&&all.size()>0&&out["message_rows"]==all.size()&&out["outbox_rows"]==all.size()&&out["atomic_event_join_count"]==all.size(),"Actual repository or atomic row reconciliation failed");pool.Shutdown();out["status"]="PASS";
 }catch(const std::exception&e){out["failure"]=e.what();}
 std::cout<<out.dump(2)<<'\n';return out["status"]=="PASS"?0:2;
}
