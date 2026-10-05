#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <grp.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace {
[[noreturn]] void Fail(const char* step) {
    std::fprintf(stderr,"PROFILER_BOOTSTRAP_FAIL step=%s errno=%d\n",step,errno);
    std::_Exit(111);
}
void Check(bool ok,const char* step) {if(!ok)Fail(step);}
}
int main(int argc,char** argv) {
    Check(argc>=5&&argc<=128,"argc");
    Check(std::strcmp(argv[1],"1000")==0&&std::strcmp(argv[2],"1000")==0,"fixed_uid_gid");
    Check(std::strcmp(argv[3],"/opt/codex-perf/perf")==0&&std::strcmp(argv[4],"record")==0,"fixed_record_exec");
    Check(geteuid()==0&&prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0)==1&&prctl(PR_GET_SECCOMP,0,0,0,0)==2,"initial_policy");
    Check(prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_CLEAR_ALL,0,0,0)==0,"ambient_clear");
    for(int cap=0;cap<=CAP_LAST_CAP;++cap) {
        if(cap!=CAP_PERFMON)Check(prctl(PR_CAPBSET_DROP,cap,0,0,0)==0,"drop_bounding");
    }
    Check(prctl(PR_SET_KEEPCAPS,1,0,0,0)==0,"keep_before_uid");
    Check(setgroups(0,nullptr)==0,"supplemental_groups");
    Check(setresgid(1000,1000,1000)==0&&setresuid(1000,1000,1000)==0,"same_uid_gid");
    __user_cap_header_struct header{};header.version=_LINUX_CAPABILITY_VERSION_3;header.pid=0;
    __user_cap_data_struct caps[2]{};const std::uint32_t bit=1U<<(CAP_PERFMON%32);
    caps[CAP_PERFMON/32].effective=bit;caps[CAP_PERFMON/32].permitted=bit;caps[CAP_PERFMON/32].inheritable=bit;
    Check(syscall(SYS_capset,&header,caps)==0,"only_perf_capset");
    Check(prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_RAISE,CAP_PERFMON,0,0)==0,"only_perf_ambient");
    Check(prctl(PR_SET_KEEPCAPS,0,0,0,0)==0,"clear_keepcaps");
    __user_cap_data_struct actual[2]{};Check(syscall(SYS_capget,&header,actual)==0,"capget");
    Check(std::memcmp(caps,actual,sizeof(caps))==0,"exact_effective_permitted_inheritable");
    for(int cap=0;cap<=CAP_LAST_CAP;++cap) {
        Check(prctl(PR_CAPBSET_READ,cap,0,0,0)==(cap==CAP_PERFMON),"exact_bounding");
        Check(prctl(PR_CAP_AMBIENT,PR_CAP_AMBIENT_IS_SET,cap,0,0)==(cap==CAP_PERFMON),"exact_ambient");
    }
    uid_t ur,ue,us;gid_t gr,ge,gs;
    Check(getresuid(&ur,&ue,&us)==0&&getresgid(&gr,&ge,&gs)==0,"getres");
    Check(ur==1000&&ue==1000&&us==1000&&gr==1000&&ge==1000&&gs==1000&&getgroups(0,nullptr)==0,"uids_and_groups");
    Check(prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0)==1&&prctl(PR_GET_SECCOMP,0,0,0,0)==2,"final_policy");
    std::fprintf(stderr,"PROFILER_FINAL uid=1000 gid=1000 effective=0000004000000000 permitted=0000004000000000 inheritable=0000004000000000 bounding=0000004000000000 ambient=0000004000000000 nnp=1 seccomp=2\n");
    execv(argv[3],argv+3);Fail("execv");
}
