#include "common/config/Config.h"
#include "common/db/PermissionBoundaryTrace.h"
#include "services/repository/FriendRepository.h"
#include "services/social/repository/FriendRepositoryAdapter.h"
#include "services/rpc/LocalPrivateChatPermissionClient.h"
#include <nlohmann/json.hpp>
#include <barrier>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace tinyimx;
using namespace std::chrono_literals;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
struct Sample {
    std::uint64_t from{},to{};unsigned round{},lane{};
    std::string rid;
    std::int64_t submitted{},worker_start{},worker_end{},returned{},caller_cpu{},worker_cpu{};
    int outcome{-1};bool rpc_ok{false};
};
using Key=std::pair<std::uint64_t,std::uint64_t>;
class TimedRepository final:public social::FriendRepositoryPort {
public:
    social::FriendRepositoryAdapter actual;
    const std::map<Key,std::shared_ptr<Sample>>* samples{};
    explicit TimedRepository(FriendRepository* repo):actual(repo){}
    social::ChatPermissionApplicationResult CheckPrivateChatPermission(std::uint64_t from,std::uint64_t to)override{
        auto row=samples->at({from,to});row->worker_start=diagnostics::SteadyMicros();
        const auto cpu=diagnostics::ThreadCpuMicros();
        // Test-only scope: the unchanged frozen production repository emits
        // its real pool-acquire and query marks with the same request hash.
        diagnostics::PermissionBoundaryTrace trace(2,from,to,row->rid,"owned-local-permission-r899");
        diagnostics::PermissionBoundaryTrace::Scope scope(trace);
        auto result=actual.CheckPrivateChatPermission(from,to);
        trace.Result(static_cast<int>(result.status));
        row->worker_cpu=diagnostics::CpuDelta(cpu,diagnostics::ThreadCpuMicros());
        row->worker_end=diagnostics::SteadyMicros();return result;
    }
    social::FriendRepositoryListResult ListFriends(std::uint64_t,std::size_t)override{throw std::logic_error("list not permitted");}
};
}
int main(int argc,char**argv)try{
    if(argc!=3)return 2;
    Config config;Check(config.LoadFromFile(argv[1]),"owned config required");
    Check(config.MySql().enable&&config.MySql().database=="codex_perf041_20261018_r8852"&&config.MySql().pool_size==4,"exact owned read-only diagnostic scope and4connections per lane");
    const std::filesystem::path out=argv[2];Check(out=="/out/phase.json","exact container output");
    Check(Logger::Instance().Init("warn","/out/permission.log",false),"private diagnostic log initialized");
    MySqlConnection observer;Check(observer.Connect(config.MySql()),"owned observer connect");
    MySqlQueryResult relations;Check(observer.Query("SELECT user_id,peer_user_id,relation_status FROM im_user_relations ORDER BY user_id,peer_user_id",&relations)&&relations.rows.size()==8,"exact8owned relation rows");
    std::map<Key,int> expected;
    for(auto& r:relations.rows){auto a=std::stoull(r.at(0)),b=std::stoull(r.at(1));Check(a>=500001&&a<=500008&&b>=500001&&b<=500008&&r.at(2)=="1","owned restored friendships");expected[{a,b}]=0;}
    MySqlConnectionPool pool0,pool1;Check(pool0.Initialize(config.MySql())&&pool1.Initialize(config.MySql()),"same two4connection SQL pools");
    FriendRepository repo0(&pool0),repo1(&pool1);TimedRepository timed0(&repo0),timed1(&repo1);
    social::FriendApplicationService app0(&timed0),app1(&timed1);
    rpc::LocalPrivateChatPermissionClient local0(&app0,4,32),local1(&app1,4,32);
    std::vector<std::shared_ptr<Sample>> all;
    for(unsigned round=0;round<8;++round){
        std::map<Key,std::shared_ptr<Sample>> samples;
        for(std::uint64_t a=500001;a<=500008;++a)for(std::uint64_t b=500001;b<=500008;++b){
            if(a==b||(!(round%2)&&!expected.contains({a,b})))continue;
            auto s=std::make_shared<Sample>();s->from=a;s->to=b;s->round=round;s->lane=(a-500001)%2;
            s->rid="owned-local-r899-"+std::to_string(round)+"-"+std::to_string(a)+"-"+std::to_string(b);
            samples[{a,b}]=s;all.push_back(s);
        }
        timed0.samples=&samples;timed1.samples=&samples;
        std::barrier start(static_cast<std::ptrdiff_t>(samples.size()));std::vector<std::thread> callers;
        for(auto& [key,s]:samples)callers.emplace_back([&,s]{
            rpc::RpcCallOptions opts;opts.remaining_timeout=4s;opts.request_id=s->rid;opts.caller_instance="owned-local-permission-r899";
            start.arrive_and_wait();const auto cpu=diagnostics::ThreadCpuMicros();s->submitted=diagnostics::SteadyMicros();
            auto result=(s->lane?local1:local0).CheckPrivateChatPermission({s->from,s->to},opts);
            s->returned=diagnostics::SteadyMicros();s->caller_cpu=diagnostics::CpuDelta(cpu,diagnostics::ThreadCpuMicros());
            s->rpc_ok=result.ok();if(result.ok())s->outcome=static_cast<int>(result.value->outcome);
        });
        for(auto& thread:callers)thread.join();
        for(auto& [key,s]:samples){Check(s->rpc_ok&&s->outcome==(expected.contains(key)?0:2),"real authoritative SQL outcome retained");Check(s->submitted<=s->worker_start&&s->worker_start<=s->worker_end&&s->worker_end<=s->returned,"same-call four wall boundaries ordered");}
        std::this_thread::sleep_for(1100ms);
    }
    local0.Drain();local1.Drain();
    auto a=local0.Stats(),b=local1.Stats();Check(a.submitted+b.submitted==all.size()&&a.executed+b.executed==all.size()&&a.rejected+b.rejected==0,"all bounded diagnostic calls accounted");
    MySqlQueryResult after;Check(observer.Query("SELECT user_id,peer_user_id,relation_status FROM im_user_relations ORDER BY user_id,peer_user_id",&after)&&after.rows==relations.rows,"all relation data unchanged");
    nlohmann::json rows=nlohmann::json::array();
    for(auto& s:all)rows.push_back({{"round",s->round},{"lane",s->lane},{"from",s->from},{"to",s->to},{"rid_hash",diagnostics::PermissionTraceHash(s->rid)},
        {"submit_us",s->submitted},{"worker_start_us",s->worker_start},{"worker_end_us",s->worker_end},{"returned_us",s->returned},
        {"submit_to_worker_us",s->worker_start-s->submitted},{"repository_wall_us",s->worker_end-s->worker_start},
        {"worker_to_caller_us",s->returned-s->worker_end},{"caller_total_us",s->returned-s->submitted},
        {"caller_thread_cpu_us",s->caller_cpu},{"worker_thread_cpu_us",s->worker_cpu},{"outcome",s->outcome}});
    std::ofstream file(out);file<<nlohmann::json{{"status","FROZEN_LOCAL_PERMISSION_REAL_SQL_PHASE_DIAGNOSTIC_PASS"},{"calls",rows},
       {"workers_per_lane",4},{"connections_per_lane",4},{"queue_per_lane",32},{"read_only",true},{"full_IM_or_capacity_result",false},
       {"queue_definition","call entry to repository wrapper entry includes admission/pool submission/scheduling"}}.dump(2);file.close();
    Logger::Instance().Shutdown();std::cout<<"OWNED_PERMISSION_PHASE_PASS calls="<<all.size()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"OWNED_PERMISSION_PHASE_FAIL "<<e.what()<<'\n';return 1;}
