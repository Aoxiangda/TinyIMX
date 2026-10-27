#include "services/rpc/RpcAwaitable.h"
#include <iostream>
#include <thread>
#include <vector>

namespace {
int failed=0;
void Check(bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failed;}
struct Probe{int value=0,done=0,destroyed=0,entered=0;std::function<void(int)> reply;std::vector<std::function<void()>> jobs;};
thread_local bool in_scope=false;
tinyimx::BusinessCoroutine Task(Probe* p){
    struct Guard{Probe* p;~Guard(){++p->destroyed;}}guard{p};
    if(!in_scope)throw std::logic_error("initial scope absent");
    int a=co_await tinyimx::rpc::RpcAwaitable<int>(true,[]{return -1;},[p](auto done){p->reply=std::move(done);});
    if(!in_scope)throw std::logic_error("resume scope absent");
    int b=co_await tinyimx::rpc::RpcAwaitable<int>(true,[]{return -1;},[](auto done){done(20);done(90);});
    p->value=a+b;co_return;
}
tinyimx::BusinessCoroutine Throw(){
    co_await tinyimx::rpc::RpcAwaitable<int>(true,[]{return 0;},[](auto){throw std::runtime_error("injected start failure");});
}
}
int main(){
    Probe p;auto task=Task(&p);
    std::move(task).Start([&](auto fn){p.jobs.push_back(std::move(fn));},[&](auto fn){
        bool old=in_scope;in_scope=true;++p.entered;
        try{fn();}catch(...){in_scope=old;throw;}in_scope=old;},[&](auto error){Check(!error,"resumed frame has no exception");++p.done;});
    Check(p.value==0&&p.destroyed==0&&!in_scope,"suspended frame lives and initial thread scope is restored");
    std::thread reaction([&]{p.reply(10);p.reply(99);});reaction.join();
    Check(p.jobs.size()==1&&p.done==0,"reaction thread schedules one resume without executing business work");
    auto job=std::move(p.jobs.front());p.jobs.clear();job();job={};
    Check(p.value==30&&p.done==1&&p.destroyed==1,"late plus inline duplicate callbacks complete and destroy exactly once");
    Check(p.entered==3&&!in_scope,"each resume installs and restores thread-local scope");
    int done=0;auto throwing=Throw();std::move(throwing).Start([](auto fn){fn();},{},[&](auto error){Check(bool(error),"async start exception reaches logical task completion");++done;});
    Check(done==1,"exception path completes exactly once");
    return failed?1:0;
}
