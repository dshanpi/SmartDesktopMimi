#include "ipc/ipc_socket.h"
#include "system/config/app_config.h"
#include "system/log/app_log.h"
#include "ipc/connection.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
static app_config_t cfg;
const app_config_t *app_config_get(void){return &cfg;}
void app_log_write(app_log_level_t l,const char *m,const char *f,...){(void)l;(void)m;(void)f;}
static int fdcount(void){DIR*d=opendir("/proc/self/fd");assert(d);int n=0;while(readdir(d))n++;closedir(d);return n;}
static void flags(int fd){assert(fcntl(fd,F_GETFL)&O_NONBLOCK);assert(fcntl(fd,F_GETFD)&FD_CLOEXEC);}
int main(void){char dir[]="/tmp/mimi-connect-XXXXXX";assert(mkdtemp(dir));snprintf(cfg.ipc_socket_path,sizeof(cfg.ipc_socket_path),"%s/socket",dir);cfg.ipc_socket_mode=0600;
 int baseline=fdcount(),server=-1;assert(!ipc_server_start(&server));flags(server);
 int client=-1,accepted=-1;assert(ipc_server_accept(server,&accepted)<0 && accepted==-1 && (errno==EAGAIN || errno==EWOULDBLOCK));
 assert(!ipc_client_connect(&client));assert(!ipc_server_accept(server,&accepted));flags(client);flags(accepted);
 mimi_connection *a=mimi_connection_create(client),*b=mimi_connection_create(accepted);assert(a && b);ipc_msg_t msg;
 assert(!mimi_connection_send(a,23,"query",5,0,100));assert(mimi_connection_receive(b,&msg,1000)>0 && msg.header.topic==23 && msg.header.payload_len==5 && !memcmp(msg.payload,"query",5));
 assert(!mimi_connection_send(b,24,"reply",5,0,100));assert(mimi_connection_receive(a,&msg,1000)>0 && msg.header.topic==24 && msg.header.payload_len==5 && !memcmp(msg.payload,"reply",5));
 mimi_connection_release(a);mimi_connection_release(b);
 int queued[40],count=0,before=fdcount();
 for(int i=0;i<40;i++){int fd=-1;if(ipc_client_connect(&fd)){assert(fd==-1);break;}queued[count++]=fd;}
 assert(count>0 && count<40);assert(fdcount()==before+count);
 int fd=-1;assert(ipc_client_connect(&fd)<0 && fd==-1);assert(fdcount()==before+count);
 for(int i=0;i<count;i++){close(queued[i]);}
 printf("PASS real handshake/bidirectional frames; full backlog after %d clients no leaked failure fd\n",count);
 ipc_server_stop(server);assert(fdcount()==baseline);
 assert(ipc_client_connect(&fd)<0 && fd==-1 && errno==ENOENT);assert(fdcount()==baseline);
 memset(cfg.ipc_socket_path,'x',sizeof(cfg.ipc_socket_path));assert(ipc_client_connect(&fd)<0 && fd==-1 && errno==ENAMETOOLONG);assert(fdcount()==baseline);
 char lock_path[128];snprintf(lock_path,sizeof(lock_path),"%s/socket.lock",dir);assert(!unlink(lock_path));assert(!rmdir(dir));puts("PASS nonblocking/cloexec, missing and unterminated endpoint failure cleanup");return 0;}
