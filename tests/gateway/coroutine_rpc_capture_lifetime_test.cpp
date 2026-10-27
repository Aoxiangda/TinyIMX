#include "services/rpc/RpcAwaitable.h"
#include <iostream>
#include <string>
#include <vector>
using tinyimx::BusinessCoroutine;
using tinyimx::rpc::RpcAwaitable;
struct Metadata {std::string request_id,trace_id,caller_service,caller_instance;};
BusinessCoroutine Probe(std::function<void()>& callback,int* completed) {
    Metadata options{"gateway:message:peer-validate:req:1","gateway:peer-validate:trace:1:M:7000000041000000","gateway","gateway-a"};
    const auto make_rpc=[&] {return RpcAwaitable<int>(true,
        [options]{return static_cast<int>(options.caller_service.size());},
        [options,&callback](auto done){
            if(options.caller_service!="gateway")throw std::runtime_error("metadata capture corrupted");
            callback=[done=std::move(done)]()mutable{done(7);};
        });};
    const auto result=co_await make_rpc();
    if(result!=7||options.caller_service!="gateway")throw std::runtime_error("resume corrupted");
    ++*completed;
}
int main(){
    for(int i=0;i<100;++i){
        std::vector<std::function<void()>> jobs;std::function<void()> callback;
        int completed=0,finished=0;
        auto task=Probe(callback,&completed);
        std::move(task).Start([&](auto job){jobs.push_back(std::move(job));},{},[&](std::exception_ptr error){if(error)std::rethrow_exception(error);++finished;});
        callback();
        for(auto&job:jobs)job();
        if(completed!=1||finished!=1)return 2;
    }
    std::cout<<"PASS string-bearing metadata survives non-coroutine factory on pinned GCC 100 times\n";
}
