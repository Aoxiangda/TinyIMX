#include <csignal>
#include <cstdio>
#include <dlfcn.h>
#include <unistd.h>
namespace { volatile std::sig_atomic_t stop=0; void Stop(int){stop=1;} }
int main(){
    void* library=dlopen("librocketmq.so.5",RTLD_NOW|RTLD_LOCAL);
    if(!library){std::fprintf(stderr,"SDK_MAP_PROBE_LOAD_FAILED %s\n",dlerror());return 2;}
    std::signal(SIGTERM,Stop);std::signal(SIGINT,Stop);
    std::puts("SDK_MAP_PROBE_READY_NO_CLIENT_CREATED");std::fflush(stdout);
    for(unsigned i=0;i<1200&&!stop;++i)usleep(100000);
    dlclose(library);return stop?0:3;
}
