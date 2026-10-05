#include "common/cache/RedisConnectionPool.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <time.h>

// Isolated mechanism only. Never linked/deployed into Gateway. Every key is
// in one fixed task-owned namespace; no DEL, FLUSH or config commands exist.
namespace probe_online {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
using TP=Clock::time_point;
namespace fs=std::filesystem;
constexpr char Prefix[]="codex:online-maintenance-probe-20261005:";
constexpr std::size_t Users=10000,Planned=13340,BatchMax=16;
constexpr double Rate=667;
constexpr int Seconds=20,RefreshTtl=120,FixtureTtl=300;
std::int64_t ns(TP t=Clock::now()){return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();}
std::int64_t cpu(){timespec t{};if(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t))throw std::runtime_error("OwnCPUclock");return t.tv_sec*1000000000LL+t.tv_nsec;}
std::string read(const fs::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("OwnInputFileMissing");return std::string(std::istreambuf_iterator<char>(f),{});}
void save(const fs::path& p,const Json& j){auto temp=p;temp+=".tmp";{std::ofstream f(temp);f<<j.dump(2)<<'\n';f.flush();if(!f)throw std::runtime_error("OwnEvidenceWriteFailed");}fs::rename(temp,p);}
void own(const std::string& key){if(key.rfind(Prefix,0)!=0)throw std::runtime_error("NonownedRedisKeyRejected");}
std::string key(std::size_t i){return std::string(Prefix)+"perf-u"+std::to_string(i);}
std::string gateway(std::size_t i){return "probe-gateway-"+std::to_string(i%2);}
std::string connection(std::size_t i){return "probe-connection-"+std::to_string(i);}
std::string value(std::size_t i){return Json{{"user_id",1000000+i},{"gateway_id",gateway(i)},{"connection_name",connection(i)},{"login_time",0}}.dump();}
void replace_all(std::string& s,const std::string& from,const std::string& to){std::size_t p=0;while((p=s.find(from,p))!=std::string::npos){s.replace(p,from.size(),to);p+=to.size();}}
std::string vector_script(std::string body){
 // Same exact original per-key function; only isolate Redis command errors
 // so one wrongtype key preserves the independent outcomes of its peers.
 replace_all(body,"KEYS[1]","key");replace_all(body,"ARGV[1]","gateway");replace_all(body,"ARGV[2]","connection");replace_all(body,"ARGV[3]","ttl");
 return "local function refresh_one(key,gateway,connection,ttl)\n"+body+
 "\nend\nlocal results={}\nfor i=1,#KEYS do\nlocal ok,result=pcall(refresh_one,KEYS[i],ARGV[3*i-2],ARGV[3*i-1],ARGV[3*i])\nif ok then results[i]=tostring(result) else results[i]='redis_error' end\nend\nreturn results";
}
struct Scripts{std::string single,batch;};
struct Pools{std::array<tinyimx::RedisConnectionPool,2> pool;};
void init(Pools& ps,const fs::path& configs,const std::string& ip){
 for(int i=0;i<2;++i){auto j=Json::parse(read(configs/(i==0?"gateway-a.json":"gateway-b.json")))["redis"];tinyimx::RedisConfig c;c.enable=j.at("enable").get<bool>();c.host=ip;c.port=j.at("port").get<int>();c.db=j.at("db").get<int>();c.password=j.at("password").get<std::string>();c.pool_size=j.at("pool_size").get<int>();if(c.pool_size!=8)throw std::runtime_error("ExpectedAuditedPool8");if(!ps.pool[i].Initialize(c))throw std::runtime_error("OwnRedisPoolInitFailed");}
}
std::vector<int> refresh(tinyimx::RedisConnection& c,const Scripts& scripts,const std::vector<std::string>& keys,const std::vector<std::string>& args,bool batch){
 for(const auto& k:keys)own(k);if(args.size()!=keys.size()*3)throw std::runtime_error("OwnArgumentCount");
 if(!batch){if(keys.size()!=1)throw std::runtime_error("SingleOnlyOneKey");auto x=c.EvalInteger(scripts.single,keys,args);return {x?static_cast<int>(*x):-1};}
 auto result=c.EvalStringArray(scripts.batch,keys,args);if(!result||result->size()!=keys.size())return std::vector<int>(keys.size(),-9);
 std::vector<int> out;for(auto& s:*result){if(s=="redis_error")out.push_back(-1);else if(s.size()==1&&s[0]>='0'&&s[0]<='5')out.push_back(s[0]-'0');else out.push_back(-9);}return out;
}
void prepare(Pools& ps,const Scripts& scripts,const fs::path& out){
 auto c=ps.pool[0].Acquire();if(!c)throw std::runtime_error("OwnFixtureLease");Json checks=Json::array();
 struct Case{std::string name,stored,expected_gateway,expected_connection,ttl;int status;bool exists=true,list=false;};
 std::vector<Case> cases={
 {"valid",value(0),gateway(0),connection(0),"120",1},
 {"missing","",gateway(0),connection(0),"120",0,false},
 {"old-gateway",value(0),"probe-old-gateway",connection(0),"120",2},
 {"old-connection",value(0),gateway(0),"probe-old-connection","120",2},
 {"malformed","{broken",gateway(0),connection(0),"120",3},
 {"scalar","7",gateway(0),connection(0),"120",3},
 {"null","null",gateway(0),connection(0),"120",3},
 {"missing-owners","{}",gateway(0),connection(0),"120",2},
 {"zero-ttl",value(0),gateway(0),connection(0),"0",4},
 {"negative-ttl",value(0),gateway(0),connection(0),"-1",4},
 {"invalid-ttl",value(0),gateway(0),connection(0),"bad",4},
 {"wrongtype","",gateway(0),connection(0),"120",-1,true,true},
 {"replacement-owner",value(1),gateway(0),connection(0),"120",2},
 {"unicode",Json{{"gateway_id","probe-网关"},{"connection_name","probe-连接"}}.dump(),"probe-网关","probe-连接","120",1},
 {"expired",value(0),gateway(0),connection(0),"120",0}
 };
 // Set only new owned fixtures. Missing/expired remain missing; never delete.
 for(bool batch:{false,true}){
  std::vector<std::string> keys,args;
  for(const auto& t:cases){auto k=std::string(Prefix)+(batch?"functional-batch-":"functional-single-")+t.name;own(k);auto absent=c->EvalInteger("return redis.call('EXISTS',KEYS[1])",{k},{});if(!absent||*absent!=0)throw std::runtime_error("PreserveExistingOwnedFixture");
   if(t.exists){if(t.list){auto x=c->EvalInteger("redis.call('RPUSH',KEYS[1],'owned');redis.call('EXPIRE',KEYS[1],300);return 1",{k},{});if(!x||*x!=1)throw std::runtime_error("OwnListFixture");}
    else if(!c->SetEx(k,t.stored,t.name=="expired"?1:FixtureTtl))throw std::runtime_error("OwnStringFixture");}
   keys.push_back(k);args.insert(args.end(),{t.expected_gateway,t.expected_connection,t.ttl});
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1150));
  std::vector<int> result;
  if(batch)result=refresh(*c,scripts,keys,args,true);
  else for(std::size_t i=0;i<keys.size();++i){auto x=refresh(*c,scripts,{keys[i]},{args[3*i],args[3*i+1],args[3*i+2]},false);result.push_back(x.at(0));}
  for(std::size_t i=0;i<cases.size();++i){const auto& t=cases[i];bool status=result.at(i)==t.status;checks.push_back({{"mode",batch?"batch":"single"},{"case",t.name},{"check","per-key-status"},{"pass",status},{"actual",result.at(i)},{"expected",t.status}});if(!status){save(out/"functional-checks.json",checks);throw std::runtime_error("OwnedConformanceStatusMismatch");}
   auto ttl=c->EvalInteger("return redis.call('PTTL',KEYS[1])",{keys[i]},{});if(!ttl)throw std::runtime_error("OwnFixtureTTLRead");bool timing=t.status==0?*ttl==-2:t.status==1?(*ttl>110000&&*ttl<=120000):(*ttl>280000&&*ttl<=300000);checks.push_back({{"mode",batch?"batch":"single"},{"case",t.name},{"check","TTL-preserved-or-refreshed"},{"pass",timing},{"pttl_ms",*ttl}});if(!timing){save(out/"functional-checks.json",checks);throw std::runtime_error("OwnedConformanceTTLMismatch");}
   if(t.exists&&!t.list&&t.status!=0){auto stored=c->Get(keys[i]);bool unchanged=stored&&*stored==t.stored;checks.push_back({{"mode",batch?"batch":"single"},{"case",t.name},{"check","record-byte-preserved"},{"pass",unchanged}});if(!unchanged){save(out/"functional-checks.json",checks);throw std::runtime_error("OwnedRecordChanged");}}
  }
 }
 save(out/"functional-checks.json",checks);
 // Owned performance fixture initialized only after conformance passes.
 constexpr char create[]="for i=1,#KEYS do if redis.call('EXISTS',KEYS[i])~=0 then return 0 end end;for i=1,#KEYS do redis.call('SETEX',KEYS[i],300,ARGV[i]) end;return 1";
 for(std::size_t begin=0;begin<Users;begin+=128){std::vector<std::string> keys,args;for(std::size_t i=begin;i<std::min(Users,begin+128);++i){keys.push_back(key(i));args.push_back(value(i));own(keys.back());}auto created=c->EvalInteger(create,keys,args);if(!created||*created!=1)throw std::runtime_error("OwnedPerformanceFixtureCreateFailed");}
 save(out/"result.json",{{"status","ONLINE_REFRESH_OWNED_LUA_CONFORMANCE_PASS"},{"checks",checks.size()},{"cases_per_mode",cases.size()},{"modes",2},{"performance_owned_keys",Users},{"namespace",Prefix},{"ttl_seconds",FixtureTtl},{"delete_or_production_writes",false}});
}
template<class T>class Queue{std::mutex m;std::condition_variable cv;std::deque<T> q;bool closed=false;public:
 void push(T value){std::lock_guard<std::mutex> l(m);if(closed||q.size()>=512)throw std::runtime_error("OwnBoundedQueueOverflow");q.push_back(std::move(value));cv.notify_one();}
 bool pop(T& value){std::unique_lock<std::mutex> l(m);cv.wait(l,[&]{return closed||!q.empty();});if(q.empty())return false;value=std::move(q.front());q.pop_front();return true;}
 bool pop_until(T& value,TP until){std::unique_lock<std::mutex> l(m);cv.wait_until(l,until,[&]{return closed||!q.empty();});if(q.empty())return false;value=std::move(q.front());q.pop_front();return true;}
 void close(){std::lock_guard<std::mutex> l(m);closed=true;cv.notify_all();}
};
struct Row{std::int64_t scheduled=0,enqueued=0,work=0,acquired=0,finished=0;int result=-99;std::size_t size=0;};
struct Job{std::size_t id=0,user=0;};
using Group=std::vector<Job>;
Json summary(const std::vector<double>& data){if(data.empty())return Json{{"samples",0}};auto v=data;std::sort(v.begin(),v.end());Json j{{"samples",v.size()},{"mean_ms",std::accumulate(v.begin(),v.end(),0.0)/v.size()},{"max_ms",v.back()}};for(auto [name,f]:std::vector<std::pair<std::string,double>>{{"p50_ms",.5},{"p95_ms",.95},{"p99_ms",.99},{"p999_ms",.999}})j[name]=v[static_cast<std::size_t>(std::ceil(v.size()*f))-1];return j;}
void run(Pools& ps,const Scripts& scripts,const fs::path& out,bool batch){
 // Verification is outside active CPU/latency counters and uses only ownkeys.
 auto c=ps.pool[0].Acquire();if(!c)throw std::runtime_error("OwnPreflightLease");
 constexpr char verify[]="local r={};for i=1,#KEYS do r[i]=redis.call('GET',KEYS[i]) or '__missing__' end;return r";
 for(std::size_t begin=0;begin<Users;begin+=128){std::vector<std::string> keys;for(std::size_t i=begin;i<std::min(Users,begin+128);++i)keys.push_back(key(i));auto vs=c->EvalStringArray(verify,keys,{});if(!vs||vs->size()!=keys.size())throw std::runtime_error("OwnKeyPreflightRead");for(std::size_t i=0;i<keys.size();++i)if(vs->at(i)!=value(begin+i))throw std::runtime_error("OwnedFixtureMissingOrReplaced");}c.Reset();
 std::array<Queue<Job>,2> input;std::array<Queue<Group>,2> work;std::vector<Row> rows(Planned);std::vector<std::thread> workers,collectors;std::atomic<std::size_t> done{0},errors{0},groups{0},leases{0};std::atomic<std::size_t> max_group{0};
 struct JoinGuard{std::array<Queue<Job>,2>& input;std::array<Queue<Group>,2>& work;std::vector<std::thread>& workers;std::vector<std::thread>& collectors;~JoinGuard(){for(auto& q:input)q.close();for(auto& t:collectors)if(t.joinable())t.join();for(auto& q:work)q.close();for(auto& t:workers)if(t.joinable())t.join();}}join_guard{input,work,workers,collectors};
 for(std::size_t gw=0;gw<2;++gw)for(int worker=0;worker<4;++worker)workers.emplace_back([&,gw]{Group group;while(work[gw].pop(group)){auto started=ns();auto lease=ps.pool[gw].Acquire();auto acquired=ns();std::vector<int> values(group.size(),-9);try{if(lease){++leases;std::vector<std::string> keys,args;for(auto job:group){keys.push_back(key(job.user));args.insert(args.end(),{gateway(job.user),connection(job.user),std::to_string(RefreshTtl)});}values=refresh(*lease,scripts,keys,args,batch);}}catch(...){values.assign(group.size(),-9);}auto finished=ns();++groups;auto old=max_group.load();while(old<group.size()&&!max_group.compare_exchange_weak(old,group.size())){}
  for(std::size_t i=0;i<group.size();++i){auto& row=rows[group[i].id];row.work=started;row.acquired=acquired;row.finished=finished;row.result=values.at(i);row.size=group.size();if(row.result!=1)++errors;++done;}
 }});
 if(batch)for(std::size_t gw=0;gw<2;++gw)collectors.emplace_back([&,gw]{try{Job first;while(input[gw].pop(first)){Group group{first};const auto deadline=Clock::now()+std::chrono::milliseconds(5);while(group.size()<BatchMax){Job next;if(!input[gw].pop_until(next,deadline))break;group.push_back(next);}work[gw].push(std::move(group));}}catch(...){++errors;}work[gw].close();});
 save(out/"ready.json",{{"status","READY"},{"mode",batch?"batch":"single"},{"workers",8},{"collectors",batch?2:0},{"pool_per_logical_gateway",8},{"owned_keys_verified",Users}});
 const auto wait_end=Clock::now()+std::chrono::seconds(40);while(!fs::exists(out/"start_ns")){if(Clock::now()>wait_end)throw std::runtime_error("OwnStartBarrierTimeout");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 const auto start_value=std::stoll(read(out/"start_ns"));TP start{std::chrono::nanoseconds(start_value)};if(start<Clock::now())throw std::runtime_error("OwnStartBarrierLate");std::this_thread::sleep_until(start);auto cpu_start=cpu();auto wall_start=ns();
 std::size_t issued=0,attempted=0;bool producer_error=false;
 try{for(std::size_t i=0;i<Planned;++i){const auto scheduled=start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(i/Rate));std::this_thread::sleep_until(scheduled);const auto user=i%Users;auto& row=rows[i];row.scheduled=ns(scheduled);row.enqueued=ns();attempted=i+1;Job job{i,user};if(batch)input[user%2].push(job);else work[user%2].push(Group{job});issued=i+1;if(i%667==0)save(out/"live.json",{{"planned",Planned},{"attempted",attempted},{"issued",issued},{"completed",done.load()},{"errors",errors.load()},{"mode",batch?"batch":"single"}});}}catch(...){producer_error=true;++errors;}
 if(batch){for(auto& q:input)q.close();for(auto& t:collectors)t.join();}else for(auto& q:work)q.close();for(auto& t:workers)t.join();auto wall_end=ns();auto cpu_end=cpu();
 std::vector<double> total,scheduled_total,lag,queue,acquire,command;Json raw=Json::array();std::map<std::size_t,std::size_t> sizes;
 for(std::size_t i=0;i<attempted;++i){auto& row=rows[i];raw.push_back({i,row.scheduled,row.enqueued,row.work,row.acquired,row.finished,row.result,row.size});if(row.finished){total.push_back((row.finished-row.enqueued)/1e6);scheduled_total.push_back((row.finished-row.scheduled)/1e6);lag.push_back((row.enqueued-row.scheduled)/1e6);queue.push_back((row.work-row.enqueued)/1e6);acquire.push_back((row.acquired-row.work)/1e6);command.push_back((row.finished-row.acquired)/1e6);++sizes[row.size];}}
 const bool pass=!producer_error&&issued==Planned&&done==Planned&&errors==0;const double wall=(wall_end-wall_start)/1e9;
 Json result{{"status",pass?"ONLINE_MAINTENANCE_COMPONENT_COMPLETE":"ONLINE_MAINTENANCE_COMPONENT_INCOMPLETE"},{"mode",batch?"batch":"single"},{"planned",Planned},{"attempted",attempted},{"issued",issued},{"completed",done.load()},{"errors",errors.load()},{"successful_healthy_leases",leases.load()},{"eval_groups",groups.load()},{"max_group_size",max_group.load()},{"batch_max",BatchMax},{"batch_wait_ms",batch?5:0},{"fixed_workers",8},{"collector_threads",batch?2:0},{"duration_seconds",Seconds},{"offered_per_second",Rate},{"namespace",Prefix},{"process_active_cpu_seconds",(cpu_end-cpu_start)/1e9},{"active_wall_seconds",wall},{"mean_process_active_cpu_cores",(cpu_end-cpu_start)/1e9/wall},{"enqueue_to_result",summary(total)},{"schedule_to_result",summary(scheduled_total)},{"producer_schedule_lag",summary(lag)},{"maintenance_queue_wait",summary(queue)},{"healthy_acquire_ping",summary(acquire)},{"redis_eval_call",summary(command)},{"group_size_item_counts",sizes},{"raw_columns",{"id","scheduled_ns","enqueued_ns","work_ns","acquired_ns","finished_ns","status","group_size"}},{"raw",raw},{"limits","Isolated native-to-Docker maintenance mechanism only, no heartbeat/Pong/business/10k50k acceptance. Batch five-ms coalescing changes asynchronous maintenance freshness andincludescollectorCPU; sameeightworkers/twopool8. Script checks currentgateway/connection at execution; no skiphealthchecks/deletes/retries."}};
 save(out/"result.json",result);save(out/"active-completed.json",{{"status",pass?"COMPLETE":"INCOMPLETE"},{"completed",done.load()},{"errors",errors.load()},{"native_active_wall_end_ns",wall_end}});
 const auto release_end=Clock::now()+std::chrono::seconds(30);while(!fs::exists(out/"release")){if(Clock::now()>release_end)throw std::runtime_error("OwnReleaseBarrierTimeout");std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 if(!pass)throw std::runtime_error("OwnedMaintenancePopulationFailed");
}
} // namespace probe_online
int main(int argc,char**argv){namespace p=probe_online;try{
 if(argc!=6)throw std::runtime_error("ExpectedOwnDiagnosticArguments");std::string mode=argv[1];if(mode!="prepare"&&mode!="single"&&mode!="batch")throw std::runtime_error("ModeRejected");auto out=p::fs::path(argv[5]);const p::fs::path base="/home/jackson7/projects/TinyIMX_publish/.local/codex/online-maintenance-component-probe-20261005";const std::vector<std::string> allowed{"prepare","single-A1","batch-B1","batch-B2","single-A2"};if(out.parent_path()!=base||!p::fs::is_directory(out)||std::find(allowed.begin(),allowed.end(),out.filename().string())==allowed.end()||out.filename().string().rfind(mode,0)!=0)throw std::runtime_error("OwnedCaseDirectoryMissing");p::Scripts scripts;scripts.single=p::read(p::fs::path(argv[4])/"running-original-refresh.lua");scripts.batch=p::vector_script(scripts.single);p::save(out/"scripts.json",{{"single",scripts.single},{"batch",scripts.batch}});
 tinyimx::LoggerConfig logs;logs.level="warn";logs.file="";logs.console=true;if(!tinyimx::Logger::Instance().Init(logs))throw std::runtime_error("OwnLoggerInit");p::Pools pools;p::init(pools,argv[2],argv[3]);if(mode=="prepare")p::prepare(pools,scripts,out);else p::run(pools,scripts,out,mode=="batch");return 0;
 }catch(const std::exception& error){std::cerr<<"OWN_COMPONENT_FAILURE="<<error.what()<<'\n';return 2;}}
