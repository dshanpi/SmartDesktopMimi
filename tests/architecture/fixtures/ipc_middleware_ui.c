#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include "middleware/middleware.c"
#include "system/config/app_config.h"
const app_config_t *app_config_get(void){abort();}
void app_log_write(app_log_level_t l,const char *m,const char *f,...){(void)l;(void)m;(void)f;}
static int connect_calls,connect_fd=-1,stop_on_connect;
int __wrap_ipc_client_connect(int *fd){connect_calls++;assert(connect_fd>=0);*fd=connect_fd;connect_fd=-1;if(stop_on_connect)mw_deinit();return 0;}
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static int partial,release_first;
static atomic_int second_started;
static _Thread_local int role,calls;
ssize_t __real_send(int,const void *,size_t,int);
ssize_t __wrap_send(int fd,const void *data,size_t len,int flags){
 if(role==3){errno=EPIPE;return -1;}
 if(role==1 && calls++==0){assert(len>20);ssize_t n=__real_send(fd,data,20,flags);assert(n==20);
 pthread_mutex_lock(&gate);partial=1;pthread_cond_broadcast(&wake);while(!release_first)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);return n;}
 return __real_send(fd,data,len,flags);
}
static void *writer(void *arg){role=(int)(intptr_t)arg;char data[64];memset(data,role==1?'A':'B',64);if(role==2)atomic_store(&second_started,1);assert(!mw_publish(TOPIC_WIFI_CONTROL_V2,data,64,MW_DIR_UI_TO_BACKEND));return NULL;}
static mimi_connection *install(int fds[2]){
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fds));mimi_connection *c=mimi_connection_create(fds[0]);assert(c);
 pthread_mutex_lock(&mw_state.lock);mw_state.initialized=true;mw_state.is_backend=false;mw_state.ui_epoch++;mw_state.ui_connection=c;mw_state.ipc_socket=fds[0];pthread_mutex_unlock(&mw_state.lock);return c;
}
static int received_callback;
static void receiver_callback(const mw_msg_t *msg){
 assert(msg->topic==TOPIC_WIFI_STATUS && msg->data_len==1 && msg->data[0]=='z');
 received_callback++;
 assert(!mw_publish(TOPIC_WIFI_COMMAND,"reply",5,MW_DIR_UI_TO_BACKEND));
 mw_deinit();
}
int main(void){
 int fd[2];mimi_connection *old=install(fd);pthread_t a,b;
 assert(!pthread_create(&a,NULL,writer,(void *)(intptr_t)1));pthread_mutex_lock(&gate);while(!partial)pthread_cond_wait(&wake,&gate);pthread_mutex_unlock(&gate);
 assert(!pthread_create(&b,NULL,writer,(void *)(intptr_t)2));while(!atomic_load(&second_started))usleep(1000);usleep(10000);
 char peek[200];assert(recv(fd[1],peek,sizeof(peek),MSG_PEEK|MSG_DONTWAIT)==20);
 pthread_mutex_lock(&gate);release_first=1;pthread_cond_broadcast(&wake);pthread_mutex_unlock(&gate);pthread_join(a,NULL);pthread_join(b,NULL);
 for(int i=0;i<2;i++){ipc_msg_t msg;assert(ipc_server_recv(fd[1],&msg,sizeof(msg),false)>0);assert(msg.header.payload_len==64);for(int j=0;j<64;j++)assert(msg.payload[j]==(i?'B':'A'));}
 puts("PASS actual candidate mw_publish serialized partial frames");
 assert(!mimi_connection_retain(old));ui_detach(old);int freshfd[2];mimi_connection *fresh=install(freshfd);
 ui_detach(old);assert(mw_state.ui_connection==fresh && mw_state.ipc_socket==freshfd[0]);mimi_connection_release(old);close(fd[1]);
 assert(!mw_publish(TOPIC_WIFI_COMMAND,"x",1,MW_DIR_UI_TO_BACKEND));puts("PASS old identity cannot detach replacement");
 role=3;assert(mw_publish(TOPIC_WIFI_COMMAND,"x",1,MW_DIR_UI_TO_BACKEND)<0);assert(!mw_state.ui_connection && connect_calls==0);role=0;close(freshfd[1]);
 puts("PASS failed send detaches without implicit reconnect or replay");
 int reconnect[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,reconnect));connect_fd=reconnect[0];stop_on_connect=1;
 assert(mw_publish(TOPIC_WIFI_COMMAND,"x",1,MW_DIR_UI_TO_BACKEND)<0);assert(connect_calls==1 && !mw_state.initialized && !mw_state.ui_connection);
 assert(recv(reconnect[1],peek,sizeof(peek),0)==0);close(reconnect[1]);puts("PASS in-flight reconnect discarded after deinit epoch change");
 stop_on_connect=0;int incoming[2];install(incoming);
 assert(!mw_subscribe(TOPIC_WIFI_STATUS,receiver_callback));
 assert(!ipc_server_send(incoming[1],TOPIC_WIFI_STATUS,"z",1,0));
 mw_process_ui_messages();assert(received_callback==1 && !mw_state.ui_connection && !mw_state.initialized);
 ipc_msg_t reply;assert(ipc_server_recv(incoming[1],&reply,sizeof(reply),false)>0);
 assert(reply.header.payload_len==5 && !memcmp(reply.payload,"reply",5));close(incoming[1]);
 puts("PASS actual receive callback publishes and deinitializes without lock/UAF");
 return 0;
}
