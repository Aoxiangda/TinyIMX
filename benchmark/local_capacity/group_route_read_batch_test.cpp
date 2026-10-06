#include "services/cache/OnlineStatusCache.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>
#include <sys/resource.h>
namespace route_read_test {
using J=nlohmann::json;using S=tinyimx::GetOnlineStatusStatus;using R=tinyimx::GetOnlineStatusResult;namespace fs=std::filesystem;
fs::path out;J checks=J::array();std::atomic<long> ping{0},get{0},eval{0},other{0};
void save(const std::string& n,const J& j){auto temp=out/(n+".tmp");{std::ofstream f(temp);f<<j.dump(2)<<'\n';if(!f)throw std::runtime_error("OwnEvidenceWrite");}fs::rename(temp,out/n);}
void check(const std::string& n,bool ok,const J& details=J::object()){checks.push_back({{"name",n},{"pass",ok},{"details",details}});save("checks.json",checks);if(!ok)throw std::runtime_error("OwnCheckFailed:"+n);}
J view(const R& x){J a={{"status",static_cast<int>(x.status)},{"error",x.error_message}};if(x.record)a["record"]={{"uid",x.record->user_id},{"gateway",x.record->gateway_id},{"connection",x.record->connection_name},{"login",x.record->login_time}};return a;}
bool same(const R&a,const R&b){return view(a)==view(b);}
J counts(){return{{"PING",ping.load()},{"GET",get.load()},{"EVAL",eval.load()},{"other",other.load()}};}
void reset(){ping=0;get=0;eval=0;other=0;}
std::string raw(std::uint64_t u,const std::string& g="gateway-测试",const std::string& c="connection-\"\\\n",std::int64_t t=1710000000){return J{{"user_id",u},{"gateway_id",g},{"connection_name",c},{"login_time",t}}.dump();}
std::string key(const std::string& p,std::uint64_t u){if(p.rfind("codex:group-route-read-batch-20261006:",0)!=0)throw std::runtime_error("NonownedKeyRejected");return p+std::to_string(u);}
void initial(tinyimx::RedisConnection& c,const std::string& k,const std::string& value){auto v=c.EvalInteger("if redis.call('EXISTS',KEYS[1])~=0 then return -1 end;redis.call('SETEX',KEYS[1],900,ARGV[1]);return 1",{k},{value});if(!v||*v!=1)throw std::runtime_error("OwnFixtureCollisionOrCreate");}
struct Case{std::uint64_t uid;std::string bytes;S expected;bool present=true,list=false;};
std::vector<Case> cases(){
 std::vector<Case> a={{100,raw(100),S::kFound},{101,"",S::kNotFound,false},{102,"{broken",S::kInvalidRecord},{103,"7",S::kInvalidRecord},{104,"null",S::kInvalidRecord},{105,"{}",S::kInvalidRecord},{106,raw(999),S::kInvalidRecord},{107,raw(107,""),S::kInvalidRecord},{108,raw(108,"gateway",""),S::kInvalidRecord},{109,raw(109,"g","c",0),S::kInvalidRecord},{110,raw(110,"g","c",-1),S::kInvalidRecord},{111,"",S::kRedisError,true,true},{112,raw(112,"valid-after-error"),S::kFound},{113,"",S::kInvalidRecord},{114,std::string("{}\0",3),S::kInvalidRecord},{9007199254740993ULL,raw(9007199254740993ULL),S::kFound},{9223372036854775808ULL,raw(9223372036854775808ULL),S::kFound},{std::numeric_limits<std::uint64_t>::max(),raw(std::numeric_limits<std::uint64_t>::max()),S::kFound},{115,raw(115,"new-owner","new-connection"),S::kFound},{0,"",S::kInvalidArgument,false}};
 // Embedded-NUL parsing is intentionally compared to the existing parser;
 // its accepted/rejected behavior is not changed by the batch primitive.
 return a;
}
void fixtures(tinyimx::RedisConnectionPool& pool,const std::string& p,const std::vector<Case>& ts){
 auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnFixtureLease");
 for(const auto& t:ts){if(!t.present)continue;if(t.list){auto v=c->EvalInteger("if redis.call('EXISTS',KEYS[1])~=0 then return -1 end;redis.call('LPUSH',KEYS[1],'own');redis.call('EXPIRE',KEYS[1],900);return 1",{key(p,t.uid)},{});check("own-wrongtype-key-absent",v&&*v==1);}else initial(*c,key(p,t.uid),t.bytes);}
 const std::uint64_t base=18446744073709551000ULL;for(std::uint64_t i=0;i<256;++i)initial(*c,key(p,base+i),raw(base+i,"batch-gateway",std::to_string(i)));
}
void conformance(tinyimx::RedisConnectionPool&pool,const std::string& p,const std::vector<Case>& ts){
 tinyimx::OnlineStatusCache cache(&pool,p);std::vector<std::uint64_t> ids;std::vector<R> original;for(auto&t:ts){ids.push_back(t.uid);original.push_back(cache.GetOnlineStatus(t.uid));}ids.push_back(100);original.push_back(cache.GetOnlineStatus(100));auto batch=cache.GetOnlineStatusBatch(ids);check("mixed-and-duplicate-input-cardinality",batch.size()==ids.size());
 J rows=J::array();for(std::size_t i=0;i<ids.size();++i){check("exact-original-status-record-error-"+std::to_string(i),same(original[i],batch[i]));if(i<ts.size()&&ts[i].uid!=114)check("expected-case-status-"+std::to_string(i),batch[i].status==ts[i].expected);rows.push_back({{"input",ids[i]},{"original",view(original[i])},{"batch",view(batch[i])}});}save("conformance.json",rows);
 check("above-double-exact",batch[15].Found()&&batch[15].record->user_id==9007199254740993ULL);check("above-int64-exact",batch[16].Found()&&batch[16].record->user_id==9223372036854775808ULL);check("uint64-max-exact",batch[17].Found()&&batch[17].record->user_id==std::numeric_limits<std::uint64_t>::max());
 check("wrongtype-does-not-discard-following-record",batch[11].status==S::kRedisError&&batch[12].Found());check("empty-stored-is-invalid-not-missing",batch[13].status==S::kInvalidRecord&&batch[1].NotFound());
 reset();auto empty=cache.GetOnlineStatusBatch({});check("empty-zero-network",empty.empty()&&counts()==J{{"PING",0},{"GET",0},{"EVAL",0},{"other",0}});
 reset();auto invalid=cache.GetOnlineStatusBatch({0,0});check("invalid-only-zero-network",invalid.size()==2&&invalid[0].status==S::kInvalidArgument&&invalid[1].status==S::kInvalidArgument&&counts()==J{{"PING",0},{"GET",0},{"EVAL",0},{"other",0}});
 std::vector<std::uint64_t> max;const std::uint64_t base=18446744073709551000ULL;for(int i=0;i<256;++i)max.push_back(base+i);reset();auto all=cache.GetOnlineStatusBatch(max);check("256-cardinality",all.size()==256);check("256-exact-two-network-commands",counts()==J{{"PING",1},{"GET",0},{"EVAL",1},{"other",0}},counts());for(int i=0;i<256;++i)check("256-input-order-exact-"+std::to_string(i),all[i].Found()&&all[i].record->user_id==max[i]&&all[i].record->connection_name==std::to_string(i));
 max.push_back(100);reset();auto over=cache.GetOnlineStatusBatch(max);check("257-reject-no-network",over.size()==257&&std::all_of(over.begin(),over.end(),[](const auto& v){return v.status==S::kInvalidArgument;})&&counts()==J{{"PING",0},{"GET",0},{"EVAL",0},{"other",0}});
 std::vector<std::uint64_t> sixtyfour(max.begin(),max.begin()+64);reset();for(auto id:sixtyfour)if(!cache.GetOnlineStatus(id).Found())throw std::runtime_error("OwnSingleCounter");auto single=counts();reset();auto v=cache.GetOnlineStatusBatch(sixtyfour);auto batched=counts();check("64-original-128-network",single==J{{"PING",64},{"GET",64},{"EVAL",0},{"other",0}},single);check("64-batch-two-network",v.size()==64&&batched==J{{"PING",1},{"GET",0},{"EVAL",1},{"other",0}},batched);save("actual-command-counts.json",{{"single",single},{"batch",batched},{"server_GETs_each",64},{"wrapper","Counts actual redisCommandArgv; does not inject faults or skip network"}});
 tinyimx::OnlineStatusCache null(nullptr,p);check("null-empty",null.GetOnlineStatusBatch({}).empty());auto nm=null.GetOnlineStatusBatch({0,100,0});check("null-mixed-exact-old-errors",same(nm[0],null.GetOnlineStatus(0))&&same(nm[1],null.GetOnlineStatus(100))&&same(nm[2],null.GetOnlineStatus(0)));
 tinyimx::RedisConnectionPool uninit;tinyimx::OnlineStatusCache uc(&uninit,p);auto u=uc.GetOnlineStatusBatch({100,0});check("uninitialized-exact-old-errors",same(u[0],uc.GetOnlineStatus(100))&&same(u[1],uc.GetOnlineStatus(0)));
 std::atomic<int> completed{0},bad{0};std::vector<std::thread> threads;for(int i=0;i<4;++i)threads.emplace_back([&]{try{for(int j=0;j<25;++j){auto vals=cache.GetOnlineStatusBatch(ids);if(vals.size()!=original.size())++bad;else for(std::size_t k=0;k<vals.size();++k)if(!same(vals[k],original[k]))++bad;++completed;}}catch(...){++bad;}});for(auto& t:threads)t.join();check("100-concurrent-mixed-batches",completed==100&&bad==0);check("all-leases-returned",pool.AvailableCount()==pool.Size());
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnVerifyLease");for(auto&t:ts){if(!t.present)continue;auto ttl=c->EvalInteger("return redis.call('PTTL',KEYS[1])",{key(p,t.uid)},{});check("TTL-preserved-"+std::to_string(t.uid),ttl&&*ttl>750000&&*ttl<=900000);if(!t.list){auto value=c->Get(key(p,t.uid));check("bytes-preserved-"+std::to_string(t.uid),value&&*value==t.bytes);}}}
}
void atomicity(tinyimx::RedisConnectionPool&pool,const std::string&p,tinyimx::RedisConfig config){
 const auto a=key(p,401),b=key(p,402);{auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnAtomicLease");initial(*c,a,raw(401,"owner0"));initial(*c,b,raw(402,"owner0"));}
 config.pool_size=1;tinyimx::RedisConnectionPool writer;if(!writer.Initialize(config))throw std::runtime_error("OwnWriterInit");std::atomic<bool> stop{false};std::atomic<int> writes{0},bad{0};
 std::thread t([&]{try{auto c=writer.Acquire();if(!c){++bad;return;}while(!stop){auto g="owner"+std::to_string(writes.load()%2);auto x=c->EvalInteger("redis.call('SETEX',KEYS[1],900,ARGV[1]);redis.call('SETEX',KEYS[2],900,ARGV[2]);return 1",{a,b},{raw(401,g),raw(402,g)});if(!x||*x!=1)++bad;++writes;}}catch(...){++bad;}});
 tinyimx::OnlineStatusCache cache(&pool,p);int errors=0;try{for(int i=0;i<100;++i){auto v=cache.GetOnlineStatusBatch({401,402,401});if(v.size()!=3||!v[0].Found()||!v[1].Found()||!v[2].Found()||v[0].record->gateway_id!=v[1].record->gateway_id||v[0].record->gateway_id!=v[2].record->gateway_id)++errors;}}catch(...){++errors;}stop=true;t.join();check("atomic-snapshot-with-own-writer",errors==0&&bad==0&&writes>0);writer.Shutdown();
}
void performance(tinyimx::RedisConnectionPool&pool,const std::string&p){
 tinyimx::OnlineStatusCache cache(&pool,p);std::vector<std::uint64_t> ids;for(int i=0;i<64;++i)ids.push_back(18446744073709551000ULL+i);J runs=J::array();
 for(const auto& mode:{"single-A1","batch-B1","batch-B2","single-A2"}){bool batch=std::string(mode).find("batch")==0;std::vector<double> wall;int bad=0;reset();for(int iter=0;iter<40;++iter){auto start=std::chrono::steady_clock::now();std::vector<R> v;if(batch)v=cache.GetOnlineStatusBatch(ids);else for(auto id:ids)v.push_back(cache.GetOnlineStatus(id));wall.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());if(v.size()!=ids.size())++bad;else for(std::size_t i=0;i<v.size();++i)if(!v[i].Found()||v[i].record->user_id!=ids[i]||v[i].record->connection_name!=std::to_string(i))++bad;}auto command_counts=counts();auto sorted=wall;std::sort(sorted.begin(),sorted.end());double sum=0;for(auto x:wall)sum+=x;
 J row={{"case",mode},{"pages",40},{"rows_checked",2560},{"incorrect",bad},{"mean_ms",sum/40},{"max_ms",sorted.back()},{"p99","NOT_ESTIMATED_40_COMPONENT_SAMPLES"},{"wall_ms",wall},{"actual_network_commands",command_counts},{"server_GET_operations",2560}};runs.push_back(row);save("component-performance.json",runs);check(std::string(mode)+":values-correct",bad==0);check(std::string(mode)+":network-counts",command_counts==J{{"PING",batch?40:2560},{"GET",batch?0:2560},{"EVAL",batch?40:0},{"other",0}});
 }
}
}
extern "C" void* __real_redisCommandArgv(redisContext*,int,const char**,const size_t*);
extern "C" void* __wrap_redisCommandArgv(redisContext*c,int n,const char**a,const size_t*l){if(n>0){std::string command(a[0],l[0]);if(command=="PING")++route_read_test::ping;else if(command=="GET")++route_read_test::get;else if(command=="EVAL")++route_read_test::eval;else ++route_read_test::other;}return __real_redisCommandArgv(c,n,a,l);}
int main(int argc,char**argv){namespace t=route_read_test;try{
 if(argc!=5)throw std::runtime_error("OwnArguments");std::string mode=argv[1];if(mode!="p1"&&mode!="p4")throw std::runtime_error("OwnPoolMode");t::out=argv[4];t::fs::path root="/home/jackson7/projects/TinyIMX_publish/.local/codex/group-route-read-batch-native-20261006";if(t::out.parent_path()!=root||t::out.filename()!=mode||!t::fs::is_directory(t::out))throw std::runtime_error("OwnStagePath");
 tinyimx::LoggerConfig logs;logs.level="warn";logs.file="";if(!tinyimx::Logger::Instance().Init(logs))throw std::runtime_error("OwnLoggerInit");std::ifstream f(t::fs::path(argv[2])/"gateway-a.json");auto j=t::J::parse(f)["redis"];tinyimx::RedisConfig config;config.enable=j.at("enable").get<bool>();config.host=argv[3];config.port=j.at("port").get<int>();config.db=j.at("db").get<int>();config.password=j.at("password").get<std::string>();config.pool_size=mode=="p1"?1:4;tinyimx::RedisConnectionPool pool;if(!pool.Initialize(config))throw std::runtime_error("OwnPoolInit");auto p="codex:group-route-read-batch-20261006:"+mode+":";auto cases=t::cases();t::fixtures(pool,p,cases);t::conformance(pool,p,cases);t::atomicity(pool,p,config);t::performance(pool,p);pool.Shutdown();tinyimx::OnlineStatusCache closed(&pool,p);auto v=closed.GetOnlineStatusBatch({100,0});t::check("own-shutdown-no-server-outage",v.size()==2&&v[0].status==t::S::kRedisError&&v[1].status==t::S::kInvalidArgument);
 t::save("result.json",{{"status","GROUP_ROUTE_READ_BATCH_NATIVE_PASS"},{"checks",t::checks.size()},{"pool_size",config.pool_size},{"production_keys_changed",false},{"keys_cleanup","No deletion; own fixture TTL900 natural expiry"},{"runtime_deployed",false},{"capacity_acceptance",false}});return 0;
 }catch(const std::exception&e){if(!t::out.empty()&&t::fs::is_directory(t::out))t::save("failed.json",{{"status","FAIL"},{"error",e.what()},{"checks",t::checks.size()}});std::cerr<<"OWN_ROUTE_READ_FAILURE="<<e.what()<<'\n';return 2;}}
