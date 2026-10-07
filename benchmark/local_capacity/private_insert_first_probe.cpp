#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/logging/Logger.h"
#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/repository/MessageRepository.h"
#include "services/outbox/OutboxRepository.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
void Require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
Json Counts(tinyimx::MySqlConnectionPool& pool){
 auto connection=pool.Acquire();Require(bool(connection),"counter lease");tinyimx::MySqlQueryResult rows;
 Require(connection->Query("SHOW SESSION STATUS WHERE Variable_name IN ('Com_select','Com_insert','Com_commit','Com_rollback')",&rows),"session counters");
 Json j=Json::object();for(const auto& row:rows.rows){Require(row.size()==2,"counter cardinality");j[row[0]]=std::stoull(row[1]);}Require(j.size()==4,"four counters");return j;
}
Json Delta(const Json&a,const Json&b){Json j;for(auto it=a.begin();it!=a.end();++it){auto v=it.value().get<std::uint64_t>();auto w=b.at(it.key()).get<std::uint64_t>();Require(w>=v,"counter monotonic");j[it.key()]=w-v;}return j;}
Json Stats(const Json& rows,const char* kind){std::vector<double> a;for(const auto& r:rows)if(r.at("kind")==kind)a.push_back(r.at("elapsed_ms"));std::sort(a.begin(),a.end());Require(a.size()==100,"100 samples per phase");double sum=0;for(auto v:a)sum+=v;return Json{{"samples",a.size()},{"mean_ms",sum/a.size()},{"p50_ms",a[(a.size()+1)/2-1]},{"p99_ms",a[static_cast<std::size_t>(std::ceil(a.size()*.99))-1]},{"max_ms",a.back()}};}
int main(int argc,char**argv){try{
 Require(argc==5,"config, token, expected flag, output required");std::string token=argv[2];Require(token.size()<24&&token.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")==std::string::npos,"own token");
 const bool on=std::string(argv[3])=="1";tinyimx::Config config;Require(config.LoadFromFile(argv[1]),"config");Require(config.MySql().database.starts_with("codex_insert_first_20261007_")&&config.MySql().pool_size==1,"only isolated schema/pool1 counters");Require(tinyimx::Logger::Instance().Init(config.Logger()),"logger");
 tinyimx::MySqlConnectionPool pool;Require(pool.Initialize(config.MySql()),"pool");tinyimx::MessageRepository repository(&pool);tinyimx::outbox::OutboxRepository outbox(&pool);tinyimx::message::MessageRepositoryAdapter adapter(&repository,&pool,&outbox);
 std::vector<std::uint64_t> ids;Json rows=Json::array();const std::string content="owned insert-first probe quoted '; no SQL interpolation";
 const auto perform=[&](int i,const char*kind){const auto begin=Clock::now();auto result=adapter.PersistPrivateMessage(10001,10002,"ifprobe-"+token+"-n"+std::to_string(i),1,content);const auto end=Clock::now();Require(result.Completed(),"successful durable result");Require(result.record.message_id==result.message_id&&result.record.from_user_id==10001&&result.record.to_user_id==10002&&result.record.content==content,"full identity");rows.push_back(Json{{"kind",kind},{"index",i},{"mid",result.message_id},{"elapsed_ms",std::chrono::duration<double,std::milli>(end-begin).count()}});return result;};
 const auto a=Counts(pool);for(int i=0;i<100;++i){auto result=perform(i,"created");Require(result.outcome==tinyimx::message::PersistPrivateMessageOutcome::kCreated,"created");ids.push_back(result.message_id);}const auto b=Counts(pool);
 for(int i=0;i<100;++i){auto result=perform(i,"reused");Require(result.outcome==tinyimx::message::PersistPrivateMessageOutcome::kReused&&result.message_id==ids[i],"same MID retry");}const auto c=Counts(pool);
 for(int i=0;i<20;++i){auto result=adapter.PersistPrivateMessage(10001,10002,"ifprobe-"+token+"-n"+std::to_string(i),1,"conflicting content");Require(result.Completed()&&result.outcome==tinyimx::message::PersistPrivateMessageOutcome::kIdempotencyConflict&&result.message_id==ids[i],"conflict preserves MID");}const auto f=Counts(pool);
 const auto created=Delta(a,b),reused=Delta(b,c),conflict=Delta(c,f);Require(created.at("Com_select")== (on?100:200),"one SELECT saved per created");Require(created.at("Com_insert")==200&&created.at("Com_commit")==100,"message and outbox commit");Require(reused.at("Com_select")==100&&reused.at("Com_insert")== (on?100:0),"retry SELECT/insert count");Require(conflict.at("Com_select")==20&&conflict.at("Com_insert")== (on?20:0),"conflict attempt count");
 auto connection=pool.Acquire();Require(bool(connection),"verification lease");tinyimx::MySqlQueryResult q;
 Require(connection->Query("SELECT COUNT(*),COUNT(DISTINCT m.message_id),COUNT(o.event_id) FROM im_private_messages m LEFT JOIN im_event_outbox o ON o.event_id=CONCAT('message.created.v1:',m.message_id) WHERE m.client_message_id LIKE 'ifprobe-"+token+"-%'",&q),"retained exact100 messages/outbox");Require(q.rows.size()==1&&q.rows[0]==std::vector<std::string>{"100","100","100"},"100 logical messages and 100 events");connection.Reset();
 Json out={{"status","PRIVATE_INSERT_FIRST_REAL_SQL_COST_PASS"},{"flag_enabled",on},{"created",Stats(rows,"created")},{"reused",Stats(rows,"reused")},{"sql_counts",{{"created",created},{"reused",reused},{"conflict",conflict}}},{"raw",rows},{"conflicts_checked",20},{"logical_messages",100},{"outbox_events",100},{"limits","Serial repository calls,100 samples per phase,pool1 isolated schema; no wire/RPC/20k capacity acceptance"}};
 std::ofstream file(argv[4]);file.exceptions(std::ios::badbit|std::ios::failbit);file<<out.dump(2)<<'\n';file.close();std::cout<<Json{{"status",out["status"]},{"created",out["created"]},{"reused",out["reused"]},{"sql_counts",out["sql_counts"]}}.dump()<<'\n';pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 0;
 }catch(const std::exception& e){std::cerr<<"FIRST_FAILURE="<<e.what()<<'\n';return 1;}}
