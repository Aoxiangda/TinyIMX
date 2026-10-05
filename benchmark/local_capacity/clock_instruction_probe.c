#define _GNU_SOURCE
#include <elf.h>
#include <cpuid.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static int direct_time(struct timespec *t) {
    return (int)syscall(SYS_clock_gettime, CLOCK_MONOTONIC, t);
}
static int64_t ns(struct timespec t) { return (int64_t)t.tv_sec*1000000000+t.tv_nsec; }
static int64_t usage_us(struct timeval t) { return (int64_t)t.tv_sec*1000000+t.tv_usec; }
static int public_vdso(void) {
    const unsigned long a=getauxval(AT_SYSINFO_EHDR);
    const long page=sysconf(_SC_PAGESIZE);
    unsigned long lo=0,hi=0;char line[1024],perms[8];int found=0;
    FILE *f=fopen("/proc/self/maps","r");if (!f || !a || page<=0) return 10;
    while(fgets(line,sizeof line,f)) {
        if (!strstr(line,"[vdso]")) continue;
        if (++found!=1 || sscanf(line,"%lx-%lx %7s",&lo,&hi,perms)!=3) { fclose(f);return 11; }
    }
    fclose(f);
    if(found!=1 || lo!=a || hi<=lo || hi-lo>65536 || (hi-lo)%(unsigned long)page ||
       strcmp(perms,"r-xp") || (hi-lo)/(unsigned long)page>16) return 12;
    const size_t n=hi-lo;const unsigned char *base=(const unsigned char *)a;
    if(n<sizeof(Elf64_Ehdr)) return 13;
    Elf64_Ehdr e;memcpy(&e,base,sizeof e);
    if(memcmp(e.e_ident,ELFMAG,SELFMAG) || e.e_ident[EI_CLASS]!=ELFCLASS64 ||
       e.e_ident[EI_DATA]!=ELFDATA2LSB || e.e_machine!=EM_X86_64 || e.e_type!=ET_DYN ||
       e.e_ehsize!=sizeof e || e.e_phentsize!=sizeof(Elf64_Phdr) || !e.e_phnum || e.e_phnum>32 ||
       e.e_phoff>n || (size_t)e.e_phnum*sizeof(Elf64_Phdr)>n-e.e_phoff ||
       e.e_shentsize!=sizeof(Elf64_Shdr) || !e.e_shnum || e.e_shnum>128 ||
       e.e_shoff>n || (size_t)e.e_shnum*sizeof(Elf64_Shdr)>n-e.e_shoff) return 14;
    int load=0;
    for(unsigned i=0;i<e.e_phnum;++i) {
        Elf64_Phdr p;memcpy(&p,base+e.e_phoff+i*sizeof p,sizeof p);
        if(p.p_offset>n || p.p_filesz>n-p.p_offset) return 15;
        if(p.p_type==PT_LOAD) {
            if(p.p_vaddr>n || p.p_memsz>n-p.p_vaddr || p.p_filesz>p.p_memsz) return 16;
            ++load;
        }
    }
    if(!load) return 17;
    for(unsigned i=0;i<e.e_shnum;++i) {
        Elf64_Shdr q;memcpy(&q,base+e.e_shoff+i*sizeof q,sizeof q);
        if(q.sh_type!=SHT_NOBITS && (q.sh_offset>n || q.sh_size>n-q.sh_offset)) return 18;
    }
    size_t offset=0;
    while(offset<n) { ssize_t z=write(STDERR_FILENO,base+offset,n-offset);if(z<0 && errno==EINTR)continue;if(z<=0)return 19;offset+=(size_t)z; }
    printf("{\"type\":\"public_vdso\",\"bytes\":%zu,\"pages\":%zu,\"own_self_mapping_only\":true}\n",n,n/(size_t)page);
    return 0;
}
static int policy(void) {
    if(getuid()!=1000 || geteuid()!=1000 || getgid()!=1000 || getegid()!=1000) return 20;
    FILE *f=fopen("/proc/self/status","r");if(!f)return 21;
    char line[256];unsigned mask=0;
    while(fgets(line,sizeof line,f)) {
        const char *fields[]={"CapInh:","CapPrm:","CapEff:","CapBnd:","CapAmb:"};
        for(unsigned i=0;i<5;++i) { unsigned long long x;if(!strncmp(line,fields[i],strlen(fields[i]))) {
            if(sscanf(line+strlen(fields[i]),"%llx",&x)!=1 || x) { fclose(f);return 22; }mask|=1u<<i;
        }}
        int x;if(!strncmp(line,"NoNewPrivs:",11)) { if(sscanf(line+11,"%d",&x)!=1 || x!=1){fclose(f);return 23;}mask|=32; }
        if(!strncmp(line,"Seccomp:",8)) { if(sscanf(line+8,"%d",&x)!=1 || x!=2){fclose(f);return 24;}mask|=64; }
    }
    fclose(f);if(mask!=127)return 25;
    printf("{\"type\":\"policy\",\"uid\":1000,\"gid\":1000,\"all_cap_sets_zero\":true,\"nnp\":1,\"seccomp\":2}\n");return 0;
}
static uint64_t counter(unsigned mode) {
    unsigned lo,hi,aux;
    if(mode==4) __asm__ volatile("rdtsc" : "=a"(lo),"=d"(hi) :: "memory");
    else if(mode==5) __asm__ volatile("lfence; rdtsc" : "=a"(lo),"=d"(hi) :: "memory");
    else { __asm__ volatile("rdtscp" : "=a"(lo),"=d"(hi),"=c"(aux) :: "memory");(void)aux; }
    return ((uint64_t)hi<<32)|lo;
}
static int counter_supported(void) {
    unsigned a,b,c,d;
    if(!__get_cpuid(1,&a,&b,&c,&d) || !(d&(1u<<4)) || !(d&(1u<<26))) return 40;
    if(!__get_cpuid(0x80000001,&a,&b,&c,&d) || !(d&(1u<<27))) return 41;
    return 0;
}
int main(void) {
    int z=counter_supported();if(z)return z;z=policy();if(z)return z;z=public_vdso();if(z)return z;
    const char *names[]={"CAPI_MONOTONIC","CAPI_REALTIME","SYS_MONOTONIC","CAPI_MONOTONIC_COARSE","RDTSC","LFENCE_RDTSC","RDTSCP"};
    const clockid_t ids[]={CLOCK_MONOTONIC,CLOCK_REALTIME,CLOCK_MONOTONIC,CLOCK_MONOTONIC_COARSE};
    volatile uint64_t checksum=0;
    for(unsigned round=0;round<4;++round)for(unsigned order=0;order<7;++order) {
        const unsigned mode=(order+round)%7;struct timespec start,end,t;struct rusage a,b;
        if(getrusage(RUSAGE_SELF,&a) || direct_time(&start))return 30;
        unsigned success=0;
        for(unsigned i=0;i<20000;++i) {
            if(mode>=4) checksum+=counter(mode)&UINT64_C(0xfffffff);
            else {
                int code=mode==2?(int)syscall(SYS_clock_gettime,ids[mode],&t):clock_gettime(ids[mode],&t);
                if(code || t.tv_nsec<0 || t.tv_nsec>=1000000000)return 31;
                checksum+=(uint64_t)t.tv_nsec;
            }
            ++success;
        }
        if(direct_time(&end) || getrusage(RUSAGE_SELF,&b))return 32;
        printf("{\"type\":\"clock_cost\",\"round\":%u,\"mode\":\"%s\",\"calls\":20000,\"success\":%u,\"wall_ns\":%lld,\"user_us\":%lld,\"system_us\":%lld,\"checksum\":%llu}\n",round,names[mode],success,(long long)(ns(end)-ns(start)),(long long)(usage_us(b.ru_utime)-usage_us(a.ru_utime)),(long long)(usage_us(b.ru_stime)-usage_us(a.ru_stime)),(unsigned long long)checksum);
        fflush(stdout);
    }
    return 0;
}
