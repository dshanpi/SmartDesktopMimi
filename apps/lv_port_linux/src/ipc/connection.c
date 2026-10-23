#include "connection.h"
#include "ipc_socket.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
struct mimi_connection { int fd; atomic_uint refs; atomic_bool stopped; pthread_mutex_t tx,rx; unsigned char input[sizeof(ipc_header_t)+IPC_MAX_PAYLOAD_SIZE]; size_t received,expected; int64_t frame_started; };
static int64_t now_ms(void) {
 struct timespec ts;if(clock_gettime(CLOCK_MONOTONIC,&ts))return -1;
 return (int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000;
}
mimi_connection *mimi_connection_create(int fd) {
 if(fd<0){errno=EINVAL;return NULL;}
 mimi_connection *c=calloc(1,sizeof(*c));if(!c)return NULL;
 int rc=pthread_mutex_init(&c->tx,NULL);if(rc){free(c);errno=rc;return NULL;}
 rc=pthread_mutex_init(&c->rx,NULL);if(rc){pthread_mutex_destroy(&c->tx);free(c);errno=rc;return NULL;}
 c->fd=fd;atomic_init(&c->refs,1);atomic_init(&c->stopped,false);return c;
}
int mimi_connection_retain(mimi_connection *c) {
 unsigned refs=atomic_load(&c->refs);
 do {if(!refs || refs==UINT_MAX){errno=EOVERFLOW;return -1;}}
 while(!atomic_compare_exchange_weak(&c->refs,&refs,refs+1));return 0;
}
bool mimi_connection_stopped(mimi_connection *c){return atomic_load(&c->stopped);}
void mimi_connection_stop(mimi_connection *c) {
 if(!atomic_exchange(&c->stopped,true))shutdown(c->fd,SHUT_RDWR);
}
void mimi_connection_release(mimi_connection *c) {
 if(atomic_fetch_sub(&c->refs,1)==1){close(c->fd);pthread_mutex_destroy(&c->tx);pthread_mutex_destroy(&c->rx);free(c);}
}
int mimi_connection_fd(mimi_connection *c){return c->fd;}
int mimi_connection_send(mimi_connection *c,uint32_t topic,const void *data,size_t size,uint32_t flags,unsigned timeout_ms) {
 if(!c || size>IPC_MAX_PAYLOAD_SIZE || (size && !data) || !timeout_ms || timeout_ms>INT_MAX){errno=EINVAL;return -1;}
 int64_t start=now_ms();if(start<0)return -1;int64_t deadline=start+timeout_ms;
 for(;;){
  if(atomic_load(&c->stopped)){errno=ESHUTDOWN;return -1;}
  int rc=pthread_mutex_trylock(&c->tx);if(!rc)break;
  if(rc!=EBUSY){errno=rc;return -1;}
  int64_t now=now_ms();if(now<0)return -1;if(now>=deadline){errno=ETIMEDOUT;return -1;}
  struct timespec delay={.tv_nsec=1000000};nanosleep(&delay,NULL);
 }
 unsigned char frame[sizeof(ipc_header_t)+IPC_MAX_PAYLOAD_SIZE];
 ipc_header_t header={.magic=IPC_MAGIC,.topic=topic,.payload_len=(uint32_t)size,.flags=flags};
 memcpy(frame,&header,sizeof(header));if(size)memcpy(frame+sizeof(header),data,size);
 size_t used=0,total=sizeof(header)+size;int error=0;
 while(used<total){
  if(atomic_load(&c->stopped)){error=ESHUTDOWN;break;}
  int64_t now=now_ms();if(now<0){error=errno;break;}if(now>=deadline){error=ETIMEDOUT;break;}
  ssize_t n=send(c->fd,frame+used,total-used,MSG_NOSIGNAL|MSG_DONTWAIT);
  if(n>0){used+=(size_t)n;continue;}if(n==0){error=EPIPE;break;}
  if(errno==EINTR)continue;
  if(errno!=EAGAIN && errno!=EWOULDBLOCK){error=errno;break;}
  struct pollfd p={.fd=c->fd,.events=POLLOUT};
  now=now_ms();if(now<0){error=errno;break;}if(now>=deadline){error=ETIMEDOUT;break;}
  int rc=poll(&p,1,(int)(deadline-now));
  if(rc<0 && errno==EINTR)continue;
  if(rc<=0){error=rc==0?ETIMEDOUT:errno;break;}
 }
 /* A deadline before any byte is sent leaves stream framing intact. */
 if(error && (used || error!=ETIMEDOUT))mimi_connection_stop(c);
 volatile unsigned char *clear=frame;for(size_t i=0;i<sizeof(frame);i++)clear[i]=0;
 pthread_mutex_unlock(&c->tx);if(error){errno=error;return -1;}return 0;
}

int mimi_connection_receive(mimi_connection *c,ipc_msg_t *out,unsigned frame_timeout_ms)
{
 if(!c || !out || !frame_timeout_ms || frame_timeout_ms>INT_MAX){errno=EINVAL;return -1;}
 int rc=pthread_mutex_trylock(&c->rx);if(rc){errno=rc==EBUSY?EAGAIN:rc;return rc==EBUSY?0:-1;}
 int error=0,result=0;
 for(unsigned attempt=0;attempt<8;attempt++){
  if(atomic_load(&c->stopped)){error=ESHUTDOWN;break;}
  int64_t now=now_ms();if(now<0){error=errno;break;}
  if(c->received && now-c->frame_started>=(int64_t)frame_timeout_ms){error=ETIMEDOUT;break;}
  size_t target=c->expected?c->expected:sizeof(ipc_header_t);
  ssize_t n=recv(c->fd,c->input+c->received,target-c->received,MSG_DONTWAIT);
  if(n==0){error=ECONNRESET;break;}
  if(n<0){if(errno==EINTR)continue;if(errno==EAGAIN || errno==EWOULDBLOCK)break;error=errno;break;}
  if(!c->received)c->frame_started=now;
  c->received+=(size_t)n;
  if(!c->expected && c->received==sizeof(ipc_header_t)){
   ipc_header_t h;memcpy(&h,c->input,sizeof(h));
   if(h.magic!=IPC_MAGIC){error=EPROTO;break;}
   if(h.payload_len>IPC_MAX_PAYLOAD_SIZE){error=EMSGSIZE;break;}
   c->expected=sizeof(h)+h.payload_len;
  }
  if(c->expected && c->received==c->expected){
   memset(out,0,sizeof(*out));memcpy(out,c->input,c->expected);result=(int)c->expected;
   volatile unsigned char *p=c->input;for(size_t i=0;i<c->received;i++)p[i]=0;
   c->received=0;c->expected=0;break;
  }
 }
 if(error)mimi_connection_stop(c);
 pthread_mutex_unlock(&c->rx);if(error){errno=error;return -1;}return result;
}
