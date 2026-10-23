import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
class UpdateWorker(unittest.TestCase):
 def test_poll_action_order(self):
  source=Path(os.environ.get("MIMI_UPDATE_SOURCE",str(ROOT/"apps/lv_port_linux/src/ui/desktop_update.c"))).read_text()
  globals_=source[source.index("static pthread_mutex_t lock"):source.index("static lv_obj_t *overlay")]
  body=source[source.index("static void *worker"):source.index("static const char *text_field")]
  fixture=r'''#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
static pthread_mutex_t gate=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static bool entered,released,fail_create,hold_next=true,fail_action;
static int calls;
static char ops[32][16];
static int request(const char *op,char *out,size_t cap) {
 pthread_mutex_lock(&gate);
 assert(calls<32);snprintf(ops[calls++],16,"%s",op);
 if(hold_next){hold_next=false;entered=true;pthread_cond_broadcast(&cond);while(!released)pthread_cond_wait(&cond,&gate);}
 pthread_mutex_unlock(&gate);
 if(fail_action && strcmp(op,"status")) {snprintf(out,cap,"HTTP/1.1 409 Conflict\r\n\r\nBusy");return -2;}
 snprintf(out,cap,"HTTP/1.1 200 OK\r\n\r\n{}");return 0;
}
static int create_thread(pthread_t *t,const pthread_attr_t *a,void *(*fn)(void *),void *arg) {
 return fail_create?EAGAIN:pthread_create(t,a,fn,arg);
}
#define pthread_create create_thread
'''
  main=r'''
static void wait_done(void) {
 for(int i=0;i<1000;i++) {
  pthread_mutex_lock(&lock);bool busy=worker_busy;pthread_mutex_unlock(&lock);
  if(!busy)return;
  struct timespec ts={0,1000000};nanosleep(&ts,NULL);
 } assert(!"worker did not finish");
}
int main(void) {
 launch("status");pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);
 launch("check");launch("install");launch("status");
 pthread_mutex_lock(&gate);released=true;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&gate);
 wait_done();
 assert(calls==3 && !strcmp(ops[0],"status") && !strcmp(ops[1],"check") && !strcmp(ops[2],"status"));
 fail_create=true;launch("download");assert(!worker_busy && action_error[0]);
 fail_create=false;launch("download");wait_done();assert(calls==5 && !strcmp(ops[3],"download") && !strcmp(ops[4],"status") && !action_error[0]);
 pthread_mutex_lock(&gate);hold_next=true;entered=false;released=false;pthread_mutex_unlock(&gate);
 launch("download");pthread_mutex_lock(&gate);while(!entered)pthread_cond_wait(&cond,&gate);pthread_mutex_unlock(&gate);
 launch("download");launch("install");launch("status");
 pthread_mutex_lock(&gate);released=true;pthread_cond_broadcast(&cond);pthread_mutex_unlock(&gate);wait_done();assert(calls==7);
 fail_action=true;launch("install");wait_done();assert(calls==9 && !strcmp(action_error,"Busy"));
 launch("status");wait_done();assert(calls==10 && !strcmp(action_error,"Busy"));
 fail_action=false;launch("check");wait_done();assert(calls==12 && !action_error[0]);
 puts("PASS poll queue, active-action duplicate suppression, spawn failure, server refusal persistence and recovery");
 return 0;
}
'''
  with tempfile.TemporaryDirectory() as d:
   p=Path(d);(p/"probe.c").write_text(fixture+globals_+body+main)
   subprocess.run(["gcc","-Wall","-Wextra","-Werror",str(p/"probe.c"),"-lpthread","-o",str(p/"probe")],check=True)
   subprocess.run([str(p/"probe")],check=True,timeout=5)
