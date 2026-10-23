#include "ipc/ipc_socket.h"
#include "system/config/app_config.h"
#include "system/log/app_log.h"
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <dirent.h>
static app_config_t cfg={.ipc_socket_path="/tmp/not-used-in-fault-injection"};
const app_config_t *app_config_get(void){return &cfg;}
void app_log_write(app_log_level_t l,const char *m,const char *f,...){(void)l;(void)m;(void)f;}
enum {READY,INTERRUPTED_READY,INTERRUPTED_TIMEOUT,TIMEOUT,SO_ERROR_FAIL,GETSOCKOPT_FAIL,LATE_READY,INVALID_POLL,CLOCK_FAIL,CONNECT_FAIL,HANGUP,ERROR_EVENT,EMPTY_EVENT};
static int mode,polls,budgets[10],now_ms;
int __wrap_clock_gettime(clockid_t id,struct timespec *t){assert(id==CLOCK_MONOTONIC);if(mode==CLOCK_FAIL){errno=EIO;return -1;}t->tv_sec=10+now_ms/1000;t->tv_nsec=(now_ms%1000)*1000000;return 0;}
int __wrap_connect(int fd,const struct sockaddr *a,socklen_t n){(void)fd;(void)a;(void)n;errno=mode==CONNECT_FAIL?EACCES:EINPROGRESS;return -1;}
int __wrap_poll(struct pollfd *p,nfds_t n,int timeout){assert(n==1 && polls<10);budgets[polls++]=timeout;
 if(mode==INTERRUPTED_TIMEOUT || (mode==INTERRUPTED_READY && polls==1)){now_ms+=250;errno=EINTR;return -1;}
 if(mode==TIMEOUT){now_ms+=timeout;return 0;}
 now_ms=mode==LATE_READY?1100:300;p[0].revents=mode==INVALID_POLL?POLLNVAL:mode==HANGUP?POLLHUP:mode==ERROR_EVENT?POLLERR:mode==EMPTY_EVENT?0:POLLOUT;return 1;}
int __wrap_getsockopt(int fd,int level,int name,void *data,socklen_t *len){(void)fd;assert(level==SOL_SOCKET && name==SO_ERROR && *len==sizeof(int));if(mode==GETSOCKOPT_FAIL){errno=EIO;return -1;}*(int*)data=mode==SO_ERROR_FAIL?ECONNREFUSED:0;return 0;}
static int fdcount(void){DIR*d=opendir("/proc/self/fd");assert(d);int n=0;while(readdir(d))n++;closedir(d);return n;}
int main(void){int failures=0;const int expected[]={0,0,ETIMEDOUT,ETIMEDOUT,ECONNREFUSED,EIO,ETIMEDOUT,EBADF,EIO,EACCES,ECONNABORTED,ECONNABORTED,ECONNABORTED};
 for(mode=0;mode<=EMPTY_EVENT;mode++){
  now_ms=0;polls=0;memset(budgets,0,sizeof(budgets));int before=fdcount(),fd=123;
  int rc=ipc_client_connect(&fd),error=errno;
  int ok=expected[mode]?rc<0 && fd==-1 && error==expected[mode]:rc==0 && fd>=0;
  if(fd>=0)close(fd);assert(fdcount()==before);
  if(mode==INTERRUPTED_TIMEOUT){assert(polls==4);for(int i=0;i<4;i++)assert(budgets[i]==1000-i*250);}
  if(mode==INTERRUPTED_READY)assert(polls==2 && budgets[1]==750);
  printf("mode%d rc%d errno%d polls%d check%d\n",mode,rc,error,polls,ok);if(!ok)failures++;
 }
 return failures?1:0;}
