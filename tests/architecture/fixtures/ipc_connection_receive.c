#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ipc/connection.c"
static int64_t offset_ms;
int __real_clock_gettime(clockid_t,struct timespec *);
int __wrap_clock_gettime(clockid_t id,struct timespec *t){int rc=__real_clock_gettime(id,t);if(!rc && id==CLOCK_MONOTONIC)t->tv_sec+=offset_ms/1000;return rc;}
static mimi_connection *pair(int fd[2]){assert(!socketpair(AF_UNIX,SOCK_STREAM,0,fd));mimi_connection *c=mimi_connection_create(fd[0]);assert(c);return c;}
int main(void){
 unsigned char frame[sizeof(ipc_header_t)+512];ipc_header_t h={.magic=IPC_MAGIC,.topic=23,.payload_len=512};memcpy(frame,&h,sizeof(h));memset(frame+sizeof(h),'p',512);
 for(size_t split=0;split<=sizeof(frame);split++){
  int fd[2];mimi_connection *c=pair(fd);ipc_msg_t out,before;memset(&out,0x55,sizeof(out));before=out;
  if(split)assert(send(fd[1],frame,split,0)==(ssize_t)split);
  int rc=mimi_connection_receive(c,&out,1000);
  if(split<sizeof(frame)){assert(rc==0 && !memcmp(&out,&before,sizeof(out)));assert(send(fd[1],frame+split,sizeof(frame)-split,0)==(ssize_t)(sizeof(frame)-split));rc=mimi_connection_receive(c,&out,1000);}
  assert(rc==(int)sizeof(frame) && !memcmp(&out,frame,sizeof(frame)));
  mimi_connection_release(c);close(fd[1]);
 }
 puts("PASS 529 split boundaries preserve output until complete");
 int fd[2];mimi_connection *c=pair(fd);ipc_msg_t out,before;memset(&out,0x55,sizeof(out));before=out;
 assert(send(fd[1],frame,1,0)==1);assert(mimi_connection_receive(c,&out,1000)==0);
 offset_ms=2000;assert(mimi_connection_receive(c,&out,1000)<0 && errno==ETIMEDOUT);offset_ms=0;
 assert(!memcmp(&out,&before,sizeof(out)) && mimi_connection_stopped(c));mimi_connection_release(c);close(fd[1]);
 c=pair(fd);assert(send(fd[1],frame,7,0)==7);assert(mimi_connection_receive(c,&out,1000)==0);shutdown(fd[1],SHUT_WR);
 assert(mimi_connection_receive(c,&out,1000)<0 && errno==ECONNRESET);assert(!memcmp(&out,&before,sizeof(out)));mimi_connection_release(c);close(fd[1]);
 for(int invalid=0;invalid<2;invalid++){
  c=pair(fd);ipc_header_t bad=h;if(invalid)bad.payload_len=513;else bad.magic=0;
  assert(send(fd[1],&bad,sizeof(bad),0)==sizeof(bad));assert(mimi_connection_receive(c,&out,1000)<0 && mimi_connection_stopped(c));assert(!memcmp(&out,&before,sizeof(out)));mimi_connection_release(c);close(fd[1]);
 }
 c=pair(fd);assert(send(fd[1],frame,sizeof(frame),0)==sizeof(frame));assert(send(fd[1],frame,sizeof(frame),0)==sizeof(frame));
 pthread_mutex_lock(&c->rx);assert(mimi_connection_receive(c,&out,1000)==0 && errno==EAGAIN);pthread_mutex_unlock(&c->rx);
 for(int i=0;i<2;i++){assert(mimi_connection_receive(c,&out,1000)==sizeof(frame));assert(!memcmp(&out,frame,sizeof(frame)));}
 assert(mimi_connection_receive(c,&out,1000)==0);mimi_connection_release(c);close(fd[1]);
 puts("PASS partial deadline/EOF/invalid header, RX contention and queued frame boundaries");return 0;
}
