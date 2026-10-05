#include "services/cache/OnlineStatusCache.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace test_online {
using Json=nlohmann::json;
using Status=tinyimx::RefreshOnlineIfMatchStatus;
using Request=tinyimx::OnlineStatusRefreshRequest;
using Result=tinyimx::RefreshOnlineIfMatchResult;
namespace fs=std::filesystem;
Json checks=Json::array();
fs::path out;
void save(const std::string& filename,const Json& value){auto temp=out/(filename+".tmp");{std::ofstream stream(temp);stream<<value.dump(2)<<'\n';stream.flush();if(!stream)throw std::runtime_error("OwnEvidenceWrite");}fs::rename(temp,out/filename);}
void check(const std::string& name,bool pass,const Json& detail=Json::object()){
 checks.push_back({{"name",name},{"pass",pass},{"detail",detail}});save("checks.json",checks);if(!pass)throw std::runtime_error("OwnedCheckFailed:"+name);
}
std::string read(const fs::path& path){std::ifstream f(path);if(!f)throw std::runtime_error("OwnConfigMissing");return std::string(std::istreambuf_iterator<char>(f),{});}
std::string record(const Request& r){return Json{{"user_id",r.user_id},{"gateway_id",r.gateway_id},{"connection_name",r.connection_name},{"login_time",0}}.dump();}
std::string key(const std::string& prefix,std::uint64_t id){if(prefix.rfind("codex:online-maintenance-batch-api-20261005:",0)!=0)throw std::runtime_error("NonownedKeyRejected");return prefix+std::to_string(id);}
void initial(tinyimx::RedisConnection& c,const std::string& k,const std::string& bytes,int ttl=300){auto existing=c.EvalInteger("return redis.call('EXISTS',KEYS[1])",{k},{});if(!existing||*existing!=0)throw std::runtime_error("OwnedFixtureCollisionPreserved");if(!c.SetEx(k,bytes,ttl))throw std::runtime_error("OwnFixtureCreate");}
struct Case{std::string name,bytes;Request request;Status expected;bool fixture=true,list=false,expired=false;};
std::vector<Case> cases(){
 const Request normal{100,"api-gateway","api-connection",120};const auto bytes=record(normal);
 std::vector<Case> tests{
  {"valid",bytes,normal,Status::kRefreshed},
  {"missing","",normal,Status::kNotFound,false},
  {"old-gateway",bytes,{100,"old-gateway","api-connection",120},Status::kMismatch},
  {"old-connection",bytes,{100,"api-gateway","old-connection",120},Status::kMismatch},
  {"malformed","{broken",normal,Status::kInvalidRecord},
  {"scalar","7",normal,Status::kInvalidRecord},
  {"wrongtype","",normal,Status::kRedisError,true,true},
  {"valid-after-wrongtype",bytes,normal,Status::kRefreshed},
  {"null","null",normal,Status::kInvalidRecord},
  {"missing-owners","{}",normal,Status::kMismatch},
  {"zero-ttl",bytes,{100,"api-gateway","api-connection",0},Status::kInvalidArgument},
  {"negative-ttl",bytes,{100,"api-gateway","api-connection",-1},Status::kInvalidArgument},
  {"replaced-owner",record({100,"replacement-gateway","replacement-connection",120}),normal,Status::kMismatch},
  {"unicode",record({100,"网关","连接",120}),{100,"网关","连接",120},Status::kRefreshed},
  {"expired",bytes,normal,Status::kNotFound,true,false,true},
  {"invalid-user","",{0,"api-gateway","api-connection",120},Status::kInvalidArgument,false},
  {"empty-gateway",bytes,{100,"","api-connection",120},Status::kInvalidArgument},
  {"empty-connection",bytes,{100,"api-gateway","",120},Status::kInvalidArgument}
 };
 for(std::size_t i=0;i<tests.size();++i){if(tests[i].request.user_id!=0)tests[i].request.user_id=100+i;auto parsed=Json::parse(tests[i].bytes,nullptr,false);if(parsed.is_object()&&parsed.contains("user_id")){parsed["user_id"]=tests[i].request.user_id;tests[i].bytes=parsed.dump();}}
 return tests;
}
void conformance(tinyimx::RedisConnectionPool& pool,const std::string& root){
 std::vector<Status> previous;
 for(bool batch:{false,true}){
  const auto prefix=root+(batch?"batch:":"single:");auto ts=cases();
  {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnFixtureLease");for(const auto& t:ts){const auto k=key(prefix,t.request.user_id);auto absent=c->EvalInteger("return redis.call('EXISTS',KEYS[1])",{k},{});if(!absent||*absent!=0)throw std::runtime_error("OwnCaseCollision");if(!t.fixture)continue;if(t.list){auto created=c->EvalInteger("redis.call('RPUSH',KEYS[1],'owned');redis.call('EXPIRE',KEYS[1],300);return 1",{k},{});if(!created||*created!=1)throw std::runtime_error("OwnListFixture");}else initial(*c,k,t.bytes,t.expired?1:300);}}
  std::this_thread::sleep_for(std::chrono::milliseconds(1150));tinyimx::OnlineStatusCache cache(&pool,prefix);std::vector<Result> results;
  if(batch){for(std::size_t start=0;start<ts.size();start+=16){std::vector<Request> requests;for(std::size_t i=start;i<ts.size()&&i<start+16;++i)requests.push_back(ts[i].request);auto part=cache.RefreshOnlineIfMatchBatch(requests);check("batch-result-count",part.size()==requests.size());results.insert(results.end(),part.begin(),part.end());}}
  else for(const auto& t:ts)results.push_back(cache.RefreshOnlineIfMatch(t.request.user_id,t.request.gateway_id,t.request.connection_name,t.request.ttl_seconds));
  auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnValidationLease");
  for(std::size_t i=0;i<ts.size();++i){const auto& t=ts[i];const auto label=(batch?"batch:":"single:")+t.name;check(label+":status",results[i].status==t.expected,{{"actual",static_cast<int>(results[i].status)},{"expected",static_cast<int>(t.expected)}});
   if(batch)check(label+":same-single-status",results[i].status==previous[i]);else previous.push_back(results[i].status);
   auto ttl=c->EvalInteger("return redis.call('PTTL',KEYS[1])",{key(prefix,t.request.user_id)},{});const bool gone=!t.fixture||t.expired;check(label+":TTL",ttl&&(gone?*ttl==-2:t.expected==Status::kRefreshed?(*ttl>110000&&*ttl<=120000):(*ttl>280000&&*ttl<=300000)),{{"pttl_ms",ttl?*ttl:-999}});
   if(t.fixture&&!t.expired&&!t.list){auto stored=c->Get(key(prefix,t.request.user_id));check(label+":record-byte-preserved",stored&&*stored==t.bytes);}
  }
 }
}
void boundaries(tinyimx::RedisConnectionPool& pool,const std::string& prefix,const tinyimx::RedisConfig& config){
 tinyimx::OnlineStatusCache cache(&pool,prefix);Request good{200,"api-gateway","api-connection",120};auto bytes=record(good);
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnBoundaryLease");initial(*c,key(prefix,200),bytes);}
 check("empty-result",cache.RefreshOnlineIfMatchBatch({}).empty());
 auto one=cache.RefreshOnlineIfMatchBatch({good});check("size1",one.size()==1&&one[0].Refreshed());
 auto max=cache.RefreshOnlineIfMatchBatch(std::vector<Request>(16,good));check("size16",max.size()==16);for(std::size_t i=0;i<max.size();++i)check("size16:ordered:"+std::to_string(i),max[i].Refreshed());
 // A fresh guard key verifies oversize rejection performs no refresh.
 Request guarded{201,"api-gateway","api-connection",120};auto guarded_bytes=record(guarded);
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnGuardLease");initial(*c,key(prefix,201),guarded_bytes);}
 auto oversized=cache.RefreshOnlineIfMatchBatch(std::vector<Request>(17,guarded));check("size17-result-count",oversized.size()==17);for(std::size_t i=0;i<oversized.size();++i)check("size17:rejected:"+std::to_string(i),oversized[i].status==Status::kInvalidArgument);
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnGuardVerifyLease");auto ttl=c->EvalInteger("return redis.call('PTTL',KEYS[1])",{key(prefix,201)},{});check("size17:TTL-unmodified",ttl&&*ttl>280000&&*ttl<=300000);auto value=c->Get(key(prefix,201));check("size17:bytes-unmodified",value&&*value==guarded_bytes);}
 auto old=good;old.connection_name="old-connection";auto dup=cache.RefreshOnlineIfMatchBatch({old,good,old});check("duplicate-owner-order",dup.size()==3&&dup[0].status==Status::kMismatch&&dup[1].Refreshed()&&dup[2].status==Status::kMismatch);
 auto newer=guarded;newer.connection_name="new-connection";const auto newer_bytes=record(newer);
 {auto c=pool.Acquire();if(!c||!c->SetEx(key(prefix,201),newer_bytes,300))throw std::runtime_error("OwnReplacementFixture");}
 auto replaced=cache.RefreshOnlineIfMatchBatch({guarded});check("replacement-stale-rejected",replaced.size()==1&&replaced[0].status==Status::kMismatch);
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnReplacementVerifyLease");auto value=c->Get(key(prefix,201));check("replacement-byte-preserved",value&&*value==newer_bytes);auto ttl=c->EvalInteger("return redis.call('PTTL',KEYS[1])",{key(prefix,201)},{});check("replacement-TTL-preserved",ttl&&*ttl>280000&&*ttl<=300000);}
 tinyimx::OnlineStatusCache null_cache(nullptr,prefix);Request invalid{0,"","",0};check("null-empty",null_cache.RefreshOnlineIfMatchBatch({}).empty());auto invalid_only=null_cache.RefreshOnlineIfMatchBatch({invalid});check("invalid-doesnot-need-pool",invalid_only.size()==1&&invalid_only[0].status==Status::kInvalidArgument);
 auto null_mixed=null_cache.RefreshOnlineIfMatchBatch({invalid,good,invalid});check("null-pool-valid-invalid-order",null_mixed.size()==3&&null_mixed[0].status==Status::kInvalidArgument&&null_mixed[1].status==Status::kRedisError&&null_mixed[2].status==Status::kInvalidArgument);
 tinyimx::RedisConnectionPool uninitialized;tinyimx::OnlineStatusCache uninitialized_cache(&uninitialized,prefix);auto empty_pool=uninitialized_cache.RefreshOnlineIfMatchBatch({good,invalid});check("uninitialized-pool",empty_pool.size()==2&&empty_pool[0].status==Status::kRedisError&&empty_pool[1].status==Status::kInvalidArgument);
 // Concurrent mixed-owner batches on pool1/pool4 check result association
 // and healthy lease release under competing callers, without global faults.
 std::vector<Request> concurrent;
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnConcurrentFixtureLease");for(int i=0;i<4;++i){Request request{static_cast<std::uint64_t>(300+i),"api-gateway","worker-"+std::to_string(i),120};initial(*c,key(prefix,request.user_id),record(request));concurrent.push_back(request);}}
 std::vector<std::thread> threads;std::atomic<int> errors{0},completed{0};
 for(const auto& request:concurrent)threads.emplace_back([&,request]{try{auto stale=request;stale.gateway_id="old-gateway";for(int iteration=0;iteration<25;++iteration){auto result=cache.RefreshOnlineIfMatchBatch({stale,request,stale});if(result.size()!=3||result[0].status!=Status::kMismatch||!result[1].Refreshed()||result[2].status!=Status::kMismatch)++errors;++completed;}}catch(...){++errors;}});
 for(auto& thread:threads)thread.join();check("concurrent100-mixed-batches",completed==100&&errors==0,{{"completed_batches",completed.load()},{"errors",errors.load()}});
 check("pool-all-leases-returned",pool.AvailableCount()==pool.Size());
 {auto c=pool.Acquire();if(!c)throw std::runtime_error("OwnConcurrentVerifyLease");for(const auto& request:concurrent){auto value=c->Get(key(prefix,request.user_id));check("concurrent:bytes:"+std::to_string(request.user_id),value&&*value==record(request));}}
 tinyimx::RedisConnectionPool observer;auto observer_config=config;observer_config.pool_size=1;if(!observer.Initialize(observer_config))throw std::runtime_error("OwnObserverInit");pool.Shutdown();auto closed=cache.RefreshOnlineIfMatchBatch({good,invalid});check("own-shutdown-pool",closed.size()==2&&closed[0].status==Status::kRedisError&&closed[1].status==Status::kInvalidArgument);
 {auto c=observer.Acquire();if(!c)throw std::runtime_error("OwnAfterShutdownObserverLease");auto value=c->Get(key(prefix,201));check("shutdown-no-record-mutation",value&&*value==newer_bytes);}
}
} // namespace test_online
int main(int argc,char**argv){namespace t=test_online;try{
 if(argc!=5)throw std::runtime_error("OwnArguments");const std::string mode=argv[1];if(mode!="p1"&&mode!="p4")throw std::runtime_error("OwnPoolMode");t::out=t::fs::path(argv[4]);const t::fs::path root="/home/jackson7/projects/TinyIMX_publish/.local/codex/online-maintenance-batch-api-20261005";if(t::out.parent_path()!=root||t::out.filename()!=mode||!t::fs::is_directory(t::out))throw std::runtime_error("OwnStagePath");
 tinyimx::LoggerConfig logs;logs.level="warn";logs.file="";if(!tinyimx::Logger::Instance().Init(logs))throw std::runtime_error("OwnLoggerInit");auto j=t::Json::parse(t::read(t::fs::path(argv[2])/"gateway-a.json"))["redis"];tinyimx::RedisConfig config;config.enable=j.at("enable").get<bool>();config.host=argv[3];config.port=j.at("port").get<int>();config.db=j.at("db").get<int>();config.password=j.at("password").get<std::string>();config.pool_size=mode=="p1"?1:4;tinyimx::RedisConnectionPool pool;if(!pool.Initialize(config))throw std::runtime_error("OwnPoolInitialize");
 const auto prefix="codex:online-maintenance-batch-api-20261005:"+mode+":";t::conformance(pool,prefix);t::boundaries(pool,prefix+"bounds:",config);t::save("result.json",{{"status","ONLINE_MAINTENANCE_BATCH_API_FUNCTIONAL_PASS"},{"pool_size",config.pool_size},{"checks",t::checks.size()},{"original_single_cases",18},{"batch_comparison_cases",18},{"concurrent_batches",100},{"production_keys",false},{"namespace",prefix},{"delete_or_global_faults",false}});return 0;
 }catch(const std::exception& error){if(!t::out.empty()&&t::fs::is_directory(t::out))t::save("failed.json",{{"status","FAIL"},{"message",error.what()},{"checks",t::checks.size()}});std::cerr<<"OWN_API_TEST_FAILURE="<<error.what()<<'\n';return 2;}}
