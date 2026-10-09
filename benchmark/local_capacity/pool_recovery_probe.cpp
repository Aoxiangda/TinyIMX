#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include "common/cache/RedisConnectionPool.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned failures=0;
void Check(bool ok,const char* label){std::cout<<"CHECK "<<label<<' '<<(ok?"PASS":"FAIL")<<std::endl;failures+=!ok;}
void Barrier(const char* label){std::cout<<"BARRIER "<<label<<std::endl;std::string s;if(!std::getline(std::cin,s)||s!="continue")throw std::runtime_error("CONTROL_BARRIER");}
bool Read(tinyimx::MySqlConnection& c){tinyimx::MySqlQueryResult r;return c.Query("SELECT 1",&r)&&r.rows.size()==1&&r.rows[0].size()==1&&r.rows[0][0]=="1";}
template<class Pool>void Slots(Pool& pool){
    for(int i=0;i<3;++i){auto lease=pool.Acquire(std::chrono::milliseconds(10));Check(!lease,"outage_returns_no_lease");lease.Reset();Check(pool.Size()==1,"configured_slot_count_stable");Check(pool.AvailableCount()==1,"failed_reconnect_slot_retained");}
}
}
int main(int argc,char**argv){if(argc!=3)return 64;try{
    tinyimx::Config config;if(!config.LoadFromFile(argv[2]))throw std::runtime_error("PRIVATE_CONFIG_LOAD");
    if(std::string(argv[1])=="mysql"){
        auto c=config.MySql();c.pool_size=1;tinyimx::MySqlConnection direct;
        Check(direct.Connect(c)&&Read(direct),"mysql_initial_real_select");direct.Close();
        Check(direct.Connect(c)&&Read(direct),"mysql_close_then_reconnect");direct.Close();
        tinyimx::MySqlConnectionPool pool;Check(pool.Initialize(c),"pool_initialized");
        {auto l=pool.Acquire();Check(l&&Read(*l),"initial_pool_real_select");}
        Barrier("fault");Check(!direct.Connect(c),"direct_connect_during_outage_fails");Slots(pool);
        Barrier("recover");Check(direct.Connect(c)&&Read(direct),"mysql_failed_connect_then_recover");
        {auto l=pool.Acquire(std::chrono::milliseconds(10));Check(l&&Read(*l),"pool_recovers_without_reinitialize");
         if(l){Check(l->BeginTransaction(),"empty_transaction_begins");}}
        {auto l=pool.Acquire(std::chrono::milliseconds(10));Check(l&&!l->InTransaction()&&Read(*l),"return_rolls_back_transaction");}
        auto held=pool.Acquire(std::chrono::milliseconds(10));Check(bool(held),"shutdown_has_outstanding_lease");
        pool.Shutdown();held.Reset();Check(pool.Size()==0&&pool.AvailableCount()==0,"shutdown_does_not_resurrect_slot");Check(!pool.Acquire(std::chrono::milliseconds(10)),"shutdown_rejects_acquire");
    }else if(std::string(argv[1])=="redis"){
        auto c=config.Redis();c.pool_size=1;tinyimx::RedisConnectionPool pool;Check(pool.Initialize(c),"pool_initialized");
        {auto l=pool.Acquire();Check(l&&l->Ping(),"initial_pool_real_ping");}
        Barrier("fault");Slots(pool);Barrier("recover");
        {auto l=pool.Acquire(std::chrono::milliseconds(10));Check(l&&l->Ping(),"pool_recovers_without_reinitialize");}
        auto held=pool.Acquire(std::chrono::milliseconds(10));Check(bool(held),"shutdown_has_outstanding_lease");
        pool.Shutdown();held.Reset();Check(pool.Size()==0&&pool.AvailableCount()==0,"shutdown_does_not_resurrect_slot");Check(!pool.Acquire(std::chrono::milliseconds(10)),"shutdown_rejects_acquire");
    }else return 64;
    std::cout<<"RESULT "<<(failures?"FAIL":"PASS")<<std::endl;return failures?1:0;
}catch(const std::exception&){std::cerr<<"PROBE_FAILED\n";return 2;}}
