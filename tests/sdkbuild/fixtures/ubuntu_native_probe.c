#define _GNU_SOURCE
#include "aitvbox/app.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <time.h>

static bool refused(long value) { return value == -1 && errno == EPERM; }
int main(int argc, char **argv)
{
    const char *a=aitvbox_app_action(argc,argv);
    if (!a) return 2;
    bool ok=false;
    if (!strcmp(a,"normal")) ok=getuid()==65534 && getgid()==65534 && !getenv("MIMI_LEAK_ME") && !getenv("LD_LIBRARY_PATH");
    else if (!strcmp(a,"hold")) {
        aitvbox_app_reply(true,"ready");sleep(20);return 0;
    } else if (!strcmp(a,"cpu")) { for (;;) {} }
    else if (!strcmp(a,"self")) { extern char **environ;char *args[]={"mimi-native-app","--action","normal",NULL};ok=refused(syscall(SYS_execveat,3,"",args,environ,AT_EMPTY_PATH)); }
    else if (!strcmp(a,"close-transfer")) ok=refused(close(4));
    else if (!strcmp(a,"replace-transfer")) ok=refused(dup2(1,4));
    else if (!strcmp(a,"connect-transfer")) ok=refused(connect(4,NULL,0));
    else if (!strcmp(a,"seccomp")) ok=refused(syscall(SYS_seccomp,1,0,NULL));
    else if (!strcmp(a,"sendmsg")) ok=refused(sendmsg(1,NULL,0));
    else if (!strcmp(a,"prlimit-pointer")) ok=refused(syscall(SYS_prlimit64,0,RLIMIT_NOFILE,(void *)0x100000000ULL,NULL));
    else if (!strcmp(a,"private")) { int fd=open(PRIVATE_PATH,O_RDONLY);ok=fd==-1 && errno==EACCES;if(fd>=0)close(fd); }
    else if (!strcmp(a,"write")) { int fd=open(WRITE_PATH,O_CREAT|O_WRONLY,0600);ok=refused(fd);if(fd>=0)close(fd); }
    else if (!strcmp(a,"internet")) { int fd=socket(AF_INET,SOCK_STREAM,0);ok=refused(fd);if(fd>=0)close(fd); }
    else if (!strcmp(a,"socketpair")) { int fds[2];int r=socketpair(AF_UNIX,SOCK_STREAM,0,fds);ok=refused(r);if(!r){close(fds[0]);close(fds[1]);} }
    else if (!strcmp(a,"fork")) { pid_t p=fork();if(p==0)_exit(0);ok=refused(p);if(p>0)waitpid(p,NULL,0); }
    else if (!strcmp(a,"exec")) { char *args[]={"/bin/true",NULL};ok=refused(execv(args[0],args)); }
    else if (!strcmp(a,"exec-absolute")) { char *args[]={"true",NULL};ok=refused(syscall(SYS_execveat,3,"/bin/true",args,NULL,AT_EMPTY_PATH)); }
    else if (!strcmp(a,"exec-other-fd")) { int fd=open("/bin/true",O_RDONLY);char *args[]={"true",NULL};ok=fd>=4 && refused(syscall(SYS_execveat,fd,"",args,NULL,AT_EMPTY_PATH));if(fd>=0)close(fd); }
    else if (!strcmp(a,"close-code")) ok=refused(close(3));
    else if (!strcmp(a,"replace-code")) { int fd=open("/bin/true",O_RDONLY);ok=fd>=4 && refused(dup2(fd,3));if(fd>=0)close(fd); }
    else if (!strcmp(a,"cloexec-code")) ok=refused(fcntl(3,F_SETFD,FD_CLOEXEC));
    else if (!strcmp(a,"ioctl")) ok=refused(ioctl(1,0,NULL));
    else if (!strcmp(a,"prctl")) ok=refused(prctl(PR_SET_DUMPABLE,1));
    else if (!strcmp(a,"leak")) ok=fcntl(17,F_GETFD)==-1 && errno==EBADF;
    else if (!strcmp(a,"transport")) { char b[1024];ok=aitvbox_capability_call("hardware.capabilities","{}",b,sizeof(b))==0 && strstr(b,"transport_only"); }
    return aitvbox_app_reply(ok,a);
}
