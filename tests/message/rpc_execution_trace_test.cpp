#include "common/observability/RpcExecutionTrace.h"
#include "common/observability/StorageCallTrace.h"
#include <iostream>
#include <stdexcept>

namespace {
std::int64_t wall=0,cpu=0;int calls=0,failed=0;
tinyimx::diagnostics::RpcExecutionTrace::Snapshot rpc;
tinyimx::diagnostics::StorageCallTrace::Snapshot storage;
std::int64_t Now()noexcept{++calls;return wall;}
std::int64_t Cpu()noexcept{++calls;return cpu;}
void Rpc(const decltype(rpc)& s)noexcept{rpc=s;}
void Db(const decltype(storage)& s)noexcept{storage=s;}
void Check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failed;}
}
int main(){
    using tinyimx::diagnostics::RpcExecutionTrace;
    using tinyimx::diagnostics::StorageCallTrace;
    wall=100;cpu=1;
    RpcExecutionTrace q(1,true,0,7,8,true,Rpc,Now,Cpu);
    wall=400;q.Start();wall=900;cpu=21;q.Finish(0,123);
    Check(rpc.admitted_us==100&&rpc.started_us==400&&rpc.finished_us==900,"queue wait and core interval have independent boundaries");
    Check(rpc.cpu_us==20&&rpc.mid==123&&rpc.from==7&&rpc.to==8,"persist output MID and worker CPU retained");
    const auto saved=rpc.finished_us;q.Finish(13,0);
    Check(rpc.finished_us==saved&&rpc.mid==123,"finish emits at most once");
    wall=1000;RpcExecutionTrace rejected(3,true,44,0,0,true,Rpc,Now,Cpu);
    wall=1200;rejected.Finish(8,44);
    Check(rpc.started_us==-1&&rpc.cpu_us==-1&&rpc.status==8,"rejected queue has absent execution and CPU instead of invented zero");
    const auto before=calls;
    RpcExecutionTrace disabled(2,false,9,0,0,false,Rpc,Now,Cpu);
    disabled.Start();disabled.Finish(0,9);
    {StorageCallTrace trace(3,9,false,Db,Now,Cpu);Check(trace.Measure(StorageCallTrace::Sql,[]{return 17;})==17,"disabled storage trace preserves return value");}
    Check(calls==before,"disabled tracing performs no clock reads");
    wall=2000;cpu=10;
    {
        StorageCallTrace trace(3,44,true,Db,Now,Cpu);
        const auto lease=trace.Measure(StorageCallTrace::Acquire,[]{wall=2300;cpu=12;return true;});
        const auto query=trace.Measure(StorageCallTrace::Sql,[]{wall=2900;cpu=15;return 23;});
        Check(lease&&query==23,"storage acquisition and query results unchanged");
    }
    Check(storage.acquire_us==300&&storage.sql_us==600&&storage.acquire_cpu_us==2&&storage.sql_cpu_us==3,"pool acquire and SQL elapsed/CPU are separately measured");
    bool thrown=false;wall=3000;
    try {StorageCallTrace trace(2,55,true,Db,Now,Cpu);trace.Measure(StorageCallTrace::Sql,[]{wall=3400;throw std::runtime_error("owned fault");});}
    catch(const std::runtime_error&){thrown=true;}
    Check(thrown&&storage.threw&&storage.sql_us==400,"storage exception and timing survive unchanged");
    return failed?1:0;
}
