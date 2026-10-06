#include "services/cache/UnreadCountCache.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <algorithm>
#include <sys/resource.h>
namespace test_batch {
using J=nlohmann::json;using S=tinyimx::GetUnreadCountStatus;namespace fs=std::filesystem;
fs::path out;J checks=J::array();
void save(const std::string& n,const J& j){auto a=out/(n+".tmp");{std::ofstream f(a);f<<j.dump(2)<<'\n';if(!f)throw std::runtime_error("OwnEvidenceWrite");}fs::rename(a,out/n);}
void check(const std::string& n,bool pass){checks.push_back({{"name",n},{"pass",pass}});save("checks.json",checks);if(!pass)throw std::runtime_error("OwnedCheck:"+n);}
std::string key(const std::string& prefix,std::uint64_t r,std::uint64_t s){if(prefix.rfind("codex:conversation-unread-batch-20261006:",0)!=0)throw std::runtime_error("NonownedPrefix");return prefix+"private:"+std::to_string(r)+":"+std::to_string(s);}
bool same(const tinyimx::GetUnreadCountResult&a,const tinyimx::GetUnreadCountResult&b){return a.status==b.status&&a.count==b.count&&a.error_message==b.error_message;}
J serialize(const tinyimx::GetUnreadCountResult&r){return{{"status",static_cast<int>(r.status)},{"count",r.count},{"error",r.error_message}};}
void fixtures(tinyimx::RedisConnectionPool&pool,const std::string&prefix){
 auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnFixtureLease");
 std::vector<std::string> data={"0","7","9223372036854775807","9007199254740993","0000123","-1","9223372036854775808","","7x"," 7","+7",std::string("7\0",2)};
 for(std::size_t i=0;i<data.size();++i){auto k=key(prefix,100,201+i);auto exists=c->EvalInteger("return redis.call('EXISTS',KEYS[1])",{k},{});check("fixture-absent-"+std::to_string(i),exists&&*exists==0);if(!c->SetEx(k,data[i],300))throw std::runtime_error("OwnFixtureCreate");}
 auto k=key(prefix,100,213);auto v=c->EvalInteger("if redis.call('EXISTS',KEYS[1])~=0 then return -1 end;redis.call('LPUSH',KEYS[1],'own');redis.call('EXPIRE',KEYS[1],300);return 1",{k},{});check("own-wrongtype-fixture",v&&*v==1);
 for(int i=0;i<50;++i){auto k2=key(prefix,100,300+i);auto e=c->EvalInteger("return redis.call('EXISTS',KEYS[1])",{k2},{});if(!e||*e!=0||!c->SetEx(k2,std::to_string(i),300))throw std::runtime_error("OwnPageFixture");}
}
void conformance(tinyimx::RedisConnectionPool&pool,const std::string&prefix){
 tinyimx::UnreadCountCache cache(&pool,prefix);std::vector<std::uint64_t> peers;
 for(int i=201;i<=214;++i)peers.push_back(i);peers.push_back(0);peers.push_back(100);peers.push_back(202);
 std::vector<tinyimx::GetUnreadCountResult> original;for(auto p:peers)original.push_back(cache.GetPrivateUnread(100,p));
 auto batch=cache.GetPrivateUnreadBatch(100,peers);check("mixed-cardinality",batch.size()==peers.size());
 J rows=J::array();for(std::size_t i=0;i<peers.size();++i){check("exact-original-status-count-error-"+std::to_string(i),same(batch[i],original[i]));rows.push_back({{"peer",peers[i]},{"original",serialize(original[i])},{"batch",serialize(batch[i])}});}save("conformance.json",rows);
 check("max-int64-exact",batch[2].Found()&&batch[2].count==std::numeric_limits<std::int64_t>::max());
 check("above-double-precision-exact",batch[3].Found()&&batch[3].count==9007199254740993LL);
 check("missing-not-zero-found",batch[13].NotFound()&&batch[0].Found()&&batch[0].count==0);
 check("valid-after-wrongtype",batch.back().Found()&&batch.back().count==7);
 std::vector<std::uint64_t> fifty;for(int i=300;i<350;++i)fifty.push_back(i);
 auto page=cache.GetPrivateUnreadBatch(100,fifty);check("max50-cardinality",page.size()==50);for(int i=0;i<50;++i)check("max50-order-"+std::to_string(i),page[i].Found()&&page[i].count==i);
 tinyimx::UnreadCountCache null(nullptr,prefix);check("null-empty-no-IO",null.GetPrivateUnreadBatch(100,{}).empty());
 auto invalid=null.GetPrivateUnreadBatch(0,{0,1,2});check("invalid-receiver-no-pool",invalid.size()==3&&std::all_of(invalid.begin(),invalid.end(),[](auto&r){return r.status==S::kInvalidArgument;}));
 auto oversize=null.GetPrivateUnreadBatch(100,std::vector<std::uint64_t>(51,202));check("51-rejected-without-pool",oversize.size()==51&&std::all_of(oversize.begin(),oversize.end(),[](auto&r){return r.status==S::kInvalidArgument;}));
 auto mixed=null.GetPrivateUnreadBatch(100,{0,202,100});check("null-mixed-original-errors",same(mixed[0],null.GetPrivateUnread(100,0))&&same(mixed[1],null.GetPrivateUnread(100,202))&&same(mixed[2],null.GetPrivateUnread(100,100)));
 tinyimx::RedisConnectionPool empty;tinyimx::UnreadCountCache ecache(&empty,prefix);
 auto er=ecache.GetPrivateUnreadBatch(100,{202,0});check("uninitialized-pool",same(er[0],ecache.GetPrivateUnread(100,202))&&er[1].status==S::kInvalidArgument);
 std::atomic<int> completed{0},errors{0};std::vector<std::thread> threads;
 for(int i=0;i<4;++i)threads.emplace_back([&]{try{for(int j=0;j<25;++j){auto v=cache.GetPrivateUnreadBatch(100,peers);if(v.size()!=original.size())++errors;else for(std::size_t k=0;k<v.size();++k)if(!same(v[k],original[k]))++errors;++completed;}}catch(...){++errors;}});
 for(auto&t:threads)t.join();check("100-concurrent-exact-mixed-batches",completed==100&&errors==0);check("all-leases-returned",pool.AvailableCount()==pool.Size());
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnVerifyLease");for(int i=201;i<=213;++i){auto k=key(prefix,100,i);auto ttl=c->EvalInteger("return redis.call('PTTL',KEYS[1])",{k},{});check("readonly-TTL-preserved-"+std::to_string(i),ttl&&*ttl>260000&&*ttl<=300000);if(i<213){auto value=c->Get(k);std::vector<std::string> expected={"0","7","9223372036854775807","9007199254740993","0000123","-1","9223372036854775808","","7x"," 7","+7",std::string("7\0",2)};check("readonly-bytes-preserved-"+std::to_string(i),value&&*value==expected[i-201]);}}}
}
void atomicity(tinyimx::RedisConnectionPool&pool,const std::string&prefix,tinyimx::RedisConfig config){
 auto a=key(prefix,100,401),b=key(prefix,100,402);
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnAtomicLease");for(auto&k:{a,b}){auto e=c->EvalInteger("return redis.call('EXISTS',KEYS[1])",{k},{});if(!e||*e!=0||!c->SetEx(k,"0",300))throw std::runtime_error("OwnAtomicFixture");}}
 config.pool_size=1;tinyimx::RedisConnectionPool writer;if(!writer.Initialize(config))throw std::runtime_error("OwnWriterPool");std::atomic<bool> stop{false};std::atomic<int> errors{0},writes{0};
 std::thread t([&]{try{auto c=writer.Acquire();if(!c){++errors;return;}while(!stop){auto v=c->EvalInteger("redis.call('SETEX',KEYS[1],300,ARGV[1]);redis.call('SETEX',KEYS[2],300,ARGV[1]);return 1",{a,b},{std::to_string(writes.load()%2)});if(!v||*v!=1)++errors;++writes;}}catch(...){++errors;}});
 tinyimx::UnreadCountCache cache(&pool,prefix);int read_errors=0;
 try{for(int i=0;i<200;++i){auto v=cache.GetPrivateUnreadBatch(100,{401,402,401});if(v.size()!=3||!v[0].Found()||!v[1].Found()||!v[2].Found()||v[0].count!=v[1].count||v[0].count!=v[2].count)++read_errors;}}catch(...){++read_errors;}
 stop=true;t.join();check("atomic-read-with-concurrent-own-writer",read_errors==0&&errors==0&&writes>0);writer.Shutdown();
}
void performance(tinyimx::RedisConnectionPool&pool,const std::string&prefix){
 tinyimx::UnreadCountCache cache(&pool,prefix);std::vector<std::uint64_t> peers;for(int i=300;i<350;++i)peers.push_back(i);
 J runs=J::array();for(auto mode:{"single-A1","batch-B1","batch-B2","single-A2"}){
  bool batch=std::string(mode).find("batch")==0;std::vector<double> wall;wall.reserve(100);rusage u0{},u1{};getrusage(RUSAGE_SELF,&u0);auto all=std::chrono::steady_clock::now();int bad=0;
  for(int j=0;j<100;++j){auto start=std::chrono::steady_clock::now();std::vector<tinyimx::GetUnreadCountResult> v;if(batch)v=cache.GetPrivateUnreadBatch(100,peers);else for(auto p:peers)v.push_back(cache.GetPrivateUnread(100,p));auto end=std::chrono::steady_clock::now();wall.push_back(std::chrono::duration<double,std::milli>(end-start).count());if(v.size()!=50)++bad;else for(int i=0;i<50;++i)if(!v[i].Found()||v[i].count!=i)++bad;}
  auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-all).count();getrusage(RUSAGE_SELF,&u1);
  auto cpu=[](const rusage&u){return double(u.ru_utime.tv_sec+u.ru_stime.tv_sec)+(u.ru_utime.tv_usec+u.ru_stime.tv_usec)/1e6;};
  auto raw=wall;std::sort(wall.begin(),wall.end());double sum=0;for(auto v:wall)sum+=v;
  J row={{"case",mode},{"pages",100},{"counts_checked",5000},{"incorrect",bad},{"mean_ms",sum/wall.size()},{"p50_ms",wall[49]},{"p99_ms",wall[98]},{"max_ms",wall.back()},{"wall_seconds",seconds},{"cpu_seconds",cpu(u1)-cpu(u0)},{"healthy_leases",batch?100:5000},{"read_network_commands",batch?100:5000},{"server_GET_operations",5000},{"page_wall_ms",raw}};
  runs.push_back(row);save("component-performance.json",runs);check(std::string(mode)+":all-counts-exact",bad==0);
 }
}
}
int main(int argc,char**argv){namespace t=test_batch;try{
 if(argc!=5)throw std::runtime_error("OwnArguments");const std::string mode=argv[1];if(mode!="p1"&&mode!="p4")throw std::runtime_error("OwnPoolMode");
 t::out=argv[4];const t::fs::path root="/home/jackson7/projects/TinyIMX_publish/.local/codex/conversation-unread-batch-api-20261006";if(t::out.parent_path()!=root||t::out.filename()!=mode||!t::fs::is_directory(t::out))throw std::runtime_error("OwnStagePath");
 tinyimx::LoggerConfig logs;logs.level="warn";logs.file="";if(!tinyimx::Logger::Instance().Init(logs))throw std::runtime_error("OwnLoggerInit");
 std::ifstream f(t::fs::path(argv[2])/"gateway-a.json");auto j=t::J::parse(f)["redis"];tinyimx::RedisConfig config;config.enable=j.at("enable").get<bool>();config.host=argv[3];config.port=j.at("port").get<int>();config.db=j.at("db").get<int>();config.password=j.at("password").get<std::string>();config.pool_size=mode=="p1"?1:4;
 tinyimx::RedisConnectionPool pool;if(!pool.Initialize(config))throw std::runtime_error("OwnPoolInit");const auto prefix="codex:conversation-unread-batch-20261006:"+mode+":";
 t::fixtures(pool,prefix);t::conformance(pool,prefix);t::atomicity(pool,prefix,config);t::performance(pool,prefix);
 pool.Shutdown();tinyimx::UnreadCountCache closed(&pool,prefix);auto v=closed.GetPrivateUnreadBatch(100,{202,0});t::check("own-shutdown-pool",v.size()==2&&v[0].status==t::S::kRedisError&&v[1].status==t::S::kInvalidArgument);
 t::save("result.json",{{"status","CONVERSATION_UNREAD_BATCH_API_PASS"},{"checks",t::checks.size()},{"pool_size",config.pool_size},{"component_cases",4},{"page_size",50},{"production_keys_changed",false},{"performance_acceptance",false},{"limits","Native component closed-loop 100 pages/case, not real Gateway/MCP or fullfeature capacity; healthy PING preserved; own keys only no cleanup"}});return 0;
 }catch(const std::exception&e){if(!t::out.empty()&&t::fs::is_directory(t::out))t::save("failed.json",{{"status","FAIL"},{"error",e.what()},{"checks",t::checks.size()}});std::cerr<<"OWN_BATCH_FAILURE="<<e.what()<<'\n';return 2;}}
