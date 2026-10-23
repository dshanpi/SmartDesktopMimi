#include "ipc/ipc_socket.h"
#include "system/config/app_config.h"
#include "system/log/app_log.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/resource.h>
const app_config_t *app_config_get(void){abort();}
void app_log_write(app_log_level_t l,const char *m,const char *f,...){(void)l;(void)m;(void)f;}
int main(void){struct rlimit limit;assert(!getrlimit(RLIMIT_NOFILE,&limit));if(limit.rlim_max<FD_SETSIZE+128)return 77;if(limit.rlim_cur<FD_SETSIZE+128){limit.rlim_cur=FD_SETSIZE+128;assert(!setrlimit(RLIMIT_NOFILE,&limit));}int fd[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));int high=fcntl(fd[0],F_DUPFD_CLOEXEC,FD_SETSIZE+64);assert(high>=FD_SETSIZE+64);
assert(ipc_socket_poll(high,0)==0);assert(send(fd[1],"x",1,0)==1);assert(ipc_socket_poll(high,0)==1);char c;assert(recv(high,&c,1,0)==1 && c=='x');
assert(ipc_socket_poll(high,2)==0);shutdown(fd[1],SHUT_WR);assert(ipc_socket_poll(high,0)==1);assert(recv(high,&c,1,0)==0);
close(high);assert(ipc_socket_poll(high,0)==-1 && errno==EBADF);assert(ipc_socket_poll(-1,0)==-1 && errno==EBADF);assert(ipc_socket_poll(fd[0],-1)==-1 && errno==EINVAL);
close(fd[0]);close(fd[1]);puts("PASS high descriptor readiness timeout EOF and invalid inputs");return 0;}
