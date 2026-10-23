"""Exercise exact user app socket functions with private socket peers."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
PREFIX = r'''#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <json-c/json.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
static struct { const char *user_apps_socket_path; } config={"/tmp/private-fixture"};
#define mimi_user_app_config_get() (&config)
static int mode;
static pthread_t peer;
static const char *frames[]={
 "{\"ok\":true}garbage\n", "{\"ok\":true}{\"ok\":false}\n",
 "{\"ok\":true} /* comment */\n", "{\"ok\":true,}\n",
 "[]\n", "true\n", "null\n", "{\"ok\":true\n",
 "{\"ok\":true}", "\n", "{\"ok\":true}\0junk\n",
 " \t{\"ok\":true} \r\n", "{\"ok\":false,\"message\":\"line\\nnext\"}\n",
 "{\"ok\":false,\"ok\":true}\n",
 "{\"ok\":true,\"ok\":false}\n",
 "{\"ok\":false,\"\\u006fk\":true}\n",
 "{\"ok\":true,\"message\":\"a\",\"message\":\"b\"}\n",
 "{\"ok\":true,\"extra\":1,\"extra\":2}\n",
 "{\"ok\\u0000ignored\":true}\n",
 "{\"ok\":true,\"message\":\"done\\u0000hidden\"}\n",
 "{\"ok\":true,\"extra\":{\"x\":1,\"y\":2},\"message\":\"colon:comma,quote\\\"\"}\n",
 "{\"\\u006fk\":true,\"message\":\"literal\\\\u0000\"}\n"

};

static void *serve(void *arg) {
 int fd=(int)(intptr_t)arg;char buf[256];size_t used=0;
 do {ssize_t n=read(fd,buf+used,sizeof(buf)-used);assert(n>0);used+=(size_t)n;}
 while(!memchr(buf,'\n',used) && used<sizeof(buf));
 assert(memchr(buf,'\n',used));
 if(mode>=3) {
  const char *s=frames[mode-3];size_t n=mode==13?sizeof("{\"ok\":true}\0junk\n")-1:strlen(s);
  assert(send(fd,s,n,MSG_NOSIGNAL)==(ssize_t)n);
 } else if(mode==1) {
  for(int i=0;i<30;i++){if(send(fd," ",1,MSG_NOSIGNAL)<0)break;usleep(200000);}
  send(fd,"\n",1,MSG_NOSIGNAL);
 } else {
  const char *s="{\"ok\":true}\n";
  for(size_t i=0;i<strlen(s);i++){assert(send(fd,s+i,1,MSG_NOSIGNAL)==1);usleep(1000);}
 }
 close(fd);return NULL;
}
static int fixture_connect(int fd,const struct sockaddr *addr,socklen_t len){
 (void)addr;(void)len;int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 assert(dup2(pair[0],fd)==fd);close(pair[0]);
 if(mode==0)close(pair[1]);
 else assert(!pthread_create(&peer,NULL,serve,(void *)(intptr_t)pair[1]));
 return 0;
}
#define connect fixture_connect
'''
SUFFIX = r'''
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);struct timespec start,end;
 assert(!clock_gettime(CLOCK_MONOTONIC,&start));
 json_object *q=json_object_new_object(),*result=app_request(q);json_object_put(q);
 assert(!clock_gettime(CLOCK_MONOTONIC,&end));
 double elapsed=end.tv_sec-start.tv_sec+(end.tv_nsec-start.tv_nsec)/1e9;
 fprintf(stderr,"mode=%d elapsed=%.3f\n",mode,elapsed);
 if(mode==2 || mode==14 || mode==15 || mode==23 || mode==24){assert(result);json_object_put(result);}else assert(!result);
 if(mode==1)assert(elapsed>=4.8 && elapsed<5.8);
 if(mode==0)assert(elapsed<1);
 if(mode)assert(!pthread_join(peer,NULL));
 return 0;
}
'''
class UserAppTransport(unittest.TestCase):
 def test_peer_close_deadline_and_fragmented_reply(self):
  source=(ROOT/'apps/lv_port_linux/src/apps/user/user_app.c').read_text()
  # Exact contiguous production functions; only endpoint/config and connect are adapters.
  start=source.index('static int write_all(') if 'static int write_all(' in source else source.index('static int64_t app_now_ms(')
  body=source[start:source.index('\nenum {',start)]
  with tempfile.TemporaryDirectory() as tmp:
   c=pathlib.Path(tmp)/'test.c';binary=pathlib.Path(tmp)/'test'
   c.write_text(PREFIX+body+SUFFIX)
   subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',str(c),'-o',str(binary),'-ljson-c','-pthread'],check=True)
   for mode in range(25):
    with self.subTest(mode=mode):subprocess.run([str(binary),str(mode)],check=True,timeout=9)
if __name__=='__main__':unittest.main()
