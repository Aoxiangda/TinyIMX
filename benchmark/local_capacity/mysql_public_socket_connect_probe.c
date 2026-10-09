#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
int main(int argc,char** argv) {
    umask(0077);
    if(argc!=2 || strcmp(argv[1],"public-connect") || getuid()!=1000 || geteuid()!=1000 || getgid()!=1000)return 2;
    const char *path="/opt/codex/mysql.sock";struct stat st;
    if(stat(path,&st) || !S_ISSOCK(st.st_mode) || (st.st_mode&0777)!=0777 || st.st_uid!=999 || st.st_gid!=999 || st.st_ino!=3932320)return 3;
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);if(fd<0)return 4;
    struct sockaddr_un a;memset(&a,0,sizeof a);a.sun_family=AF_UNIX;strcpy(a.sun_path,path);
    int rc=connect(fd,(const struct sockaddr *)&a,sizeof a),error=rc?errno:0;
    if(rc && error==EINPROGRESS) {
        struct pollfd p={fd,POLLOUT,0};int n=poll(&p,1,1000);socklen_t len=sizeof error;
        if(n>0 && !getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len))rc=error?-1:0;
        else {error=n==0?ETIMEDOUT:errno;rc=-1;}
    }
    close(fd); // Only the new socket owned by this diagnostic process.
    printf("{\"status\":\"PUBLIC_SOCKET_CONNECT_DIAGNOSTIC_COMPLETE\",\"connect_result\":%d,\"os_errno\":%d,\"stat_dev\":%llu,\"stat_ino\":%llu,\"auth_or_SQL\":false,\"production_acceptance\":false}\n",rc,error,(unsigned long long)st.st_dev,(unsigned long long)st.st_ino);
    return 0;
}
