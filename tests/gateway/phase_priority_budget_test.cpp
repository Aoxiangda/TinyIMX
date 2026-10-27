#include "gateway/ChatRequestPhaseTrace.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
void Require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
int main(){try{
    tinyimx::ChatPhasePriorityLogLimiter budget;
    for(int i=0;i<1000;++i)Require(budget.Admit(100,false)==(i<8),"slow class bound changed");
    for(int i=0;i<8;++i)Require(budget.Admit(100,true),"matched MID starved by unrelated slow logs");
    Require(!budget.Admit(100,true),"matched class exceeds8");
    Require(budget.TakeSuppressed(false)==992 && budget.TakeSuppressed(true)==1,"separate suppression counters lost");
    Require(budget.Admit(101,false)&&budget.Admit(101,true),"new bucket not admitted");
    Require(!budget.Admit(100,false)&&!budget.Admit(100,true),"clock reversal admitted");
    tinyimx::ChatPhasePriorityLogLimiter concurrent;
    std::atomic<int> fast{0},slow{0};std::vector<std::thread> threads;
    for(int i=0;i<64;++i)threads.emplace_back([&,i]{bool selected=i%2; if(concurrent.Admit(100,selected))(selected?fast:slow).fetch_add(1);});
    for(auto& t:threads)t.join();
    Require(fast==8&&slow==8,"concurrent budget exceeds16 or loses lane independence");
    tinyimx::diagnostics::MessageTraceLogBudget storage;
    for(int i=0;i<1000;++i)Require(storage.Admit(200,false)==(i<8),"storage slow bound changed");
    std::uint64_t mid=1;while(!tinyimx::diagnostics::MessageTraceSelected(mid))++mid;
    for(int i=0;i<8;++i)Require(storage.Admit(200,tinyimx::diagnostics::MessageTraceSelected(mid)),
                              "selected slow storage request lost reserved MID budget");
    Require(!storage.Admit(200,true)&&!storage.Admit(199,true),"storage bound or reverse clock violated");
    Require(storage.Admit(201,true)&&storage.Admit(201,false),"storage new bucket lost lane independence");
    tinyimx::diagnostics::MessageTraceLogBudget storage_concurrent;
    fast=0;slow=0;threads.clear();
    for(int i=0;i<64;++i)threads.emplace_back([&,i]{bool selected=i%2;if(storage_concurrent.Admit(300,selected))(selected?fast:slow).fetch_add(1);});
    for(auto&t:threads)t.join();Require(fast==8&&slow==8,"storage concurrent bound exceeds16");
    std::cout<<"[PASS] matched MID survives slow-log flood; each class8/s; concurrent total16; default slow lane unchanged; no deadline/business mutation\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
