#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
enum { SLOTS=4096, BUCKETS=7 };
struct row { _Atomic uintptr_t caller; _Atomic uint64_t count[BUCKETS]; };
static struct row rows[SLOTS];
static _Atomic uint64_t overflow,fallback;
static int (*real_clock)(clockid_t,struct timespec *);
__attribute__((constructor)) static void init_counter(void) {
    int saved=errno;real_clock=(int (*)(clockid_t,struct timespec *))dlsym(RTLD_NEXT,"clock_gettime");errno=saved;
}
int clock_gettime(clockid_t id,struct timespec *t) {
    int code;
    if(real_clock) code=real_clock(id,t);
    else { atomic_fetch_add_explicit(&fallback,1,memory_order_relaxed);code=(int)syscall(SYS_clock_gettime,id,t); }
    const int saved=errno;
    const uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    unsigned bucket=id>=0 && id<=3?(unsigned)id:id==5?4:id==6?5:6;
    unsigned start=(unsigned)(((caller>>4)^(caller>>16))&(SLOTS-1));int added=0;
    for(unsigned probe=0;probe<16;++probe) {
        struct row *p=&rows[(start+probe)&(SLOTS-1)];uintptr_t old=atomic_load_explicit(&p->caller,memory_order_relaxed);
        if(!old) { uintptr_t empty=0;if(atomic_compare_exchange_strong_explicit(&p->caller,&empty,caller,memory_order_relaxed,memory_order_relaxed))old=caller;else old=empty; }
        if(old==caller) { atomic_fetch_add_explicit(&p->count[bucket],1,memory_order_relaxed);added=1;break; }
    }
    if(!added)atomic_fetch_add_explicit(&overflow,1,memory_order_relaxed);
    errno=saved;return code;
}
__attribute__((destructor)) static void dump_counter(void) {
    const char *path=getenv("TINYIMX_CLOCK_COUNTER_OUTPUT");
    const char prefix[]="/home/jackson7/projects/TinyIMX_publish/.local/codex/private-clock-callsite-probe-20261005/runtime-private/";
    if(!path || strncmp(path,prefix,sizeof prefix-1))return;
    const char *leaf=path+sizeof prefix-1;if(!*leaf || strchr(leaf,'/') || strstr(leaf,".."))return;
    for(const char *p=leaf;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='.'))return;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return;
    uint64_t total=atomic_load_explicit(&overflow,memory_order_relaxed);unsigned records=0;
    for(unsigned i=0;i<SLOTS;++i) {
        uintptr_t caller=atomic_load_explicit(&rows[i].caller,memory_order_relaxed);if(!caller)continue;
        Dl_info info;if(!dladdr((void *)caller,&info)||!info.dli_fname||!info.dli_fbase||strpbrk(info.dli_fname,"\"\r\n\\")) { close(fd);return; }
        uint64_t counts[BUCKETS];for(unsigned j=0;j<BUCKETS;++j) { counts[j]=atomic_load_explicit(&rows[i].count[j],memory_order_relaxed);total+=counts[j]; }
        if(dprintf(fd,"{\"type\":\"caller\",\"dso\":\"%s\",\"offset\":%llu,\"counts\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu]}\n",info.dli_fname,(unsigned long long)(caller-(uintptr_t)info.dli_fbase),(unsigned long long)counts[0],(unsigned long long)counts[1],(unsigned long long)counts[2],(unsigned long long)counts[3],(unsigned long long)counts[4],(unsigned long long)counts[5],(unsigned long long)counts[6])<0) { close(fd);return; }
        ++records;
    }
    dprintf(fd,"{\"type\":\"totals\",\"records\":%u,\"calls\":%llu,\"overflow\":%llu,\"fallback\":%llu,\"clock_bucket_ids\":[0,1,2,3,5,6,-1]}\n",records,(unsigned long long)total,(unsigned long long)atomic_load_explicit(&overflow,memory_order_relaxed),(unsigned long long)atomic_load_explicit(&fallback,memory_order_relaxed));close(fd);
}
