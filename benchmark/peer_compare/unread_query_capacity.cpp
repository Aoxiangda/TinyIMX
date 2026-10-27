// Exact production snapshot SQL against frozen owned data, controlled index access.
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using Json=nlohmann::json;using Clock=std::chrono::steady_clock;
int main(int argc,char**argv){Json result={{"issue_id","PERF-032"},{"status","FAIL"}};try{
    if(argc!=6)throw std::runtime_error("config schema variant threads seconds");
    std::string schema=argv[2],variant=argv[3];unsigned threads=std::stoul(argv[4]),seconds=std::stoul(argv[5]);
    if(!schema.starts_with("codex_perf032_20261017_")||schema.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos||
       (variant!="old"&&variant!="cover")||threads<1||threads>16||seconds<1||seconds>30)throw std::runtime_error("Owned fixture bounds rejected");
    tinyimx::Config config;if(!config.LoadFromFile(argv[1]))throw std::runtime_error("Config failed");auto db=config.MySql();db.database=schema;db.pool_size=threads;
    tinyimx::MySqlConnectionPool pool;if(!pool.Initialize(db))throw std::runtime_error("Fixture pool failed");
    const std::string index=variant=="old"?" IGNORE INDEX (idx_im_private_messages_unread_projection)":" FORCE INDEX (idx_im_private_messages_unread_projection)";
    auto sql=[&](unsigned u){return "SELECT COALESCE(SUM(from_user_id = "+std::to_string(u+1)+"),0),COUNT(*) FROM im_private_messages"+index+" WHERE to_user_id="+std::to_string(u)+" AND delivery_status IN (0,1)";};
    std::vector<std::vector<std::string>> expected(1000);
    {auto c=pool.Acquire();for(unsigned i=0;i<1000;++i){tinyimx::MySqlQueryResult r;if(!c||!c->Query(sql(500001+i),&r)||r.rows.size()!=1||r.rows[0].size()!=2)throw std::runtime_error("Preflight query failed");expected[i]=r.rows[0];}}
    std::barrier ready(threads+1);std::atomic<unsigned> errors{0};std::vector<std::vector<double>> times(threads);Clock::time_point start,end;std::vector<std::thread>workers;
    for(unsigned t=0;t<threads;++t)workers.emplace_back([&,t]{auto c=pool.Acquire();auto&samples=times[t];samples.reserve(200000);unsigned index=t*997;ready.arrive_and_wait();
        while(Clock::now()<end){auto began=Clock::now();tinyimx::MySqlQueryResult r;unsigned i=index++%1000;
            if(!c||!c->Query(sql(500001+i),&r)||r.rows.size()!=1||r.rows[0]!=expected[i]){++errors;break;}
            samples.push_back(std::chrono::duration<double,std::milli>(Clock::now()-began).count());if(samples.size()>=200000){++errors;break;}
        }});
    start=Clock::now();end=start+std::chrono::seconds(seconds);ready.arrive_and_wait();for(auto&t:workers)t.join();double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
    std::vector<double>all;for(auto&v:times)all.insert(all.end(),v.begin(),v.end());std::sort(all.begin(),all.end());
    if(all.empty())throw std::runtime_error("No samples");result.update({{"status",errors==0?"PASS":"FAIL"},{"threads",threads},{"seconds",elapsed},{"variant",variant},{"queries",all.size()},{"queries_per_s",all.size()/elapsed},{"errors",errors.load()},{"p50_ms",all[(all.size()-1)/2]},{"p99_ms",all[std::min(all.size()-1,static_cast<size_t>(std::ceil(all.size()*.99)-1))]},{"max_ms",all.back()},{"scope","Frozen owned read-only query-stage throughput, not whole IM or production latency."}});pool.Shutdown();
}catch(const std::exception&e){result["failure"]=e.what();}std::cout<<result.dump()<<'\n';return result["status"]=="PASS"?0:2;}
