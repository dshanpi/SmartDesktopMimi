#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include "middleware/middleware.c"
#include "system/config/app_config.h"
const app_config_t *app_config_get(void){static const app_config_t cfg={0};return &cfg;}
void app_log_write(app_log_level_t l,const char *m,const char *f,...){(void)l;(void)m;(void)f;}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static int partial,release_first,threads_ready;
static int clock_offset;
int __real_clock_gettime(clockid_t,struct timespec *);
int __wrap_clock_gettime(clockid_t id,struct timespec *t){int rc=__real_clock_gettime(id,t);if(!rc && id==CLOCK_MONOTONIC)t->tv_sec+=clock_offset;return rc;}
static atomic_int second_started;
static int writer_error[3];
static _Thread_local int role,calls;
ssize_t __real_send(int,const void *,size_t,int);
ssize_t __wrap_send(int fd,const void *data,size_t len,int flags){
 if(role==1 && calls++==0){assert(len>20);ssize_t n=__real_send(fd,data,20,flags);assert(n==20);
 pthread_mutex_lock(&gate);partial=1;pthread_cond_broadcast(&wake);while(!release_first)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);return n;}
 return __real_send(fd,data,len,flags);
}
static void *writer(void *arg){
 pthread_mutex_lock(&gate);threads_ready++;pthread_cond_broadcast(&wake);while(threads_ready<2)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);
 role=(int)(intptr_t)arg;char data[64];memset(data,role==1?'A':'B',64);if(role==2){pthread_mutex_lock(&gate);while(!partial)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);atomic_store(&second_started,1);}struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);int rc=mw_publish(TOPIC_WIFI_STATUS,data,64,MW_DIR_BACKEND_TO_UI);int err=errno;writer_error[role]=err;clock_gettime(CLOCK_MONOTONIC,&end);fprintf(stderr,"writer%d rc%d errno%d elapsed%.3fms\n",role,rc,err,(end.tv_sec-begin.tv_sec)*1000.0+(end.tv_nsec-begin.tv_nsec)/1000000.0);return (void *)(intptr_t)rc;}
static mimi_connection *install(int slot,int fds[2]){
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fds));mimi_connection *c=mimi_connection_create(fds[0]);assert(c);
 pthread_mutex_lock(&mw_state.lock);mw_state.initialized=true;mw_state.is_backend=true;mw_state.listen_socket=-1;
 mw_state.clients[slot]=(client_t){.active=true,.connection=c,.fd=fds[0],.last_heartbeat=time(NULL)};
 pthread_mutex_unlock(&mw_state.lock);return c;
}
static int callback_count;
static void receive_callback(const mw_msg_t *msg){
 assert(msg->topic==TOPIC_WIFI_COMMAND);callback_count++;
 assert(!mw_publish(TOPIC_WIFI_STATUS,"reply",5,MW_DIR_BACKEND_TO_UI));mw_deinit();
}
int main(void){
 int fd[2];mimi_connection *old=install(0,fd);pthread_t a,b;
 assert(!pthread_create(&a,NULL,writer,(void *)(intptr_t)1));assert(!pthread_create(&b,NULL,writer,(void *)(intptr_t)2));pthread_mutex_lock(&gate);while(!partial)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);
 while(!atomic_load(&second_started)){usleep(100);}
 usleep(1000);
 char peek[200];assert(recv(fd[1],peek,sizeof(peek),MSG_PEEK|MSG_DONTWAIT)==20);
 pthread_mutex_lock(&gate);release_first=1;pthread_cond_broadcast(&wake);pthread_mutex_unlock(&gate);void *ra,*rb;pthread_join(a,&ra);pthread_join(b,&rb);assert(!ra);
 if(rb){assert(writer_error[2]==ETIMEDOUT);assert(mw_state.clients[0].connection==old && !mimi_connection_stopped(old));}

 for(int i=0;i<(rb?1:2);i++){ipc_msg_t msg;assert(ipc_client_recv(fd[1],&msg,sizeof(msg))>0);assert(msg.header.payload_len==64);for(int j=0;j<64;j++)assert(msg.payload[j]==(i?'B':'A'));}
 if(rb){
  assert(recv(fd[1],peek,sizeof(peek),MSG_DONTWAIT)<0 && errno==EAGAIN);
  char retry[64];memset(retry,'B',64);assert(!mw_publish(TOPIC_WIFI_STATUS,retry,64,MW_DIR_BACKEND_TO_UI));
  ipc_msg_t msg;assert(ipc_client_recv(fd[1],&msg,sizeof(msg))>0 && msg.header.payload_len==64 && !memcmp(msg.payload,retry,64));
  puts("PASS unsent deadline preserved stream; explicit retry exact, no automatic replay");
 }
 assert(!mimi_connection_retain(old));backend_detach(0,old,false,0);
 int freshfd[2];mimi_connection *fresh=install(0,freshfd);backend_detach(0,old,false,0);assert(mw_state.clients[0].connection==fresh);
 mimi_connection_release(old);close(fd[1]);
 time_t stale=time(NULL)-30;mw_state.clients[0].last_heartbeat=stale;
 mw_state.clients[0].last_heartbeat=time(NULL);backend_detach(0,fresh,true,stale);assert(mw_state.clients[0].connection==fresh);
 puts("PASS backend concurrent frame integrity and stale slot/heartbeat rejection");
 assert(!mw_backend_register_handler(TOPIC_WIFI_COMMAND,receive_callback));
 assert(!ipc_client_send(freshfd[1],TOPIC_WIFI_COMMAND,"x",1,0));mw_process();
 assert(callback_count==1 && !mw_state.initialized && !mw_state.clients[0].active);
 ipc_msg_t reply;assert(ipc_client_recv(freshfd[1],&reply,sizeof(reply))>0);assert(reply.header.payload_len==5 && !memcmp(reply.payload,"reply",5));close(freshfd[1]);
 puts("PASS actual backend poll/receive callback publish and deinit");
 int exp[2],live[2];install(0,exp);fresh=install(1,live);mw_state.clients[0].last_heartbeat=time(NULL)-30;
 mw_backend_check_heartbeat_timeouts();assert(!mw_state.clients[0].active && mw_state.clients[1].connection==fresh);
 assert(recv(exp[1],peek,sizeof(peek),0)==0);mw_deinit();close(exp[1]);close(live[1]);
 puts("PASS heartbeat expires only old current client");
 int incomplete[2];install(0,incomplete);assert(send(incomplete[1],"L",1,0)==1);
 mw_process();assert(mw_state.clients[0].active);clock_offset=2;
 mw_process();clock_offset=0;assert(!mw_state.clients[0].active);
 mw_deinit();close(incomplete[1]);puts("PASS backend incomplete frame expires without new POLLIN");return 0;
}
