from pathlib import Path
import subprocess,sys
r=Path(__file__).resolve().parents[3] / 'third_party/TuyaOpen'
e=Path(sys.argv[1]).resolve()
s=(r/'src/tuya_ai_service/svc_ai_agent/src/tuya_ai_input.c').read_text()
a=s.index('typedef struct {\n    AI_INPUT_STATE_E state;');b=s.index('STATIC AI_INPUT_CTX_T ai_input_ctx;',a)+len('STATIC AI_INPUT_CTX_T ai_input_ctx;')
structs=s[a:b]
a=s.index('STATIC VOID __ai_input_thread(VOID* arg)');b=s.index('OPERATE_RET tuya_ai_input_init(',a)
worker=s[a:b]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef void VOID;
typedef char CHAR_T;
typedef int BOOL_T;
typedef int OPERATE_RET;
typedef unsigned UINT_T;
typedef uint32_t UINT32_T;
typedef uint64_t UINT64_T;
typedef int QUEUE_HANDLE;
typedef int THREAD_HANDLE;
typedef int MUTEX_HANDLE;
typedef int TUYA_RINGBUFF_T;
typedef enum {AI_INPUT_IDLE,AI_INPUT_START_LAZY,AI_INPUT_START,AI_INPUT_PROC,AI_INPUT_STOPPING,AI_INPUT_STOP} AI_INPUT_STATE_E;
typedef struct{int len,type,biz,total_len;} AI_RINGBUF_HEAD_T;
#define STATIC static
#define SIZEOF sizeof
#define AI_SOLUTION_CODE_LEN 16
#define AI_SESSION_MAX_NUM 1
#define TRUE 1
#define FALSE 0
#define OPRT_OK 0
#define THREAD_STATE_RUNNING 1
#define AI_EVENT_CHAT_BREAK 1
#define AT_NETWORK_FAIL 1
#define AI_INPUT_TASK_DELAY 10
#define PR_DEBUG(...) ((void)0)
'''
stubs=r'''
static AI_INPUT_SESSION_CTX_T session;
static AI_INPUT_COMMAND_T queue[3];
static int count,pos,ticks,budget,lock,ends;
static UINT64_T completed[8];
static int states[8];
static int tal_thread_get_state(int t){(void)t;return ticks<budget;}
static void tal_mutex_lock(int m){(void)m;assert(!lock);lock=1;}
static void tal_mutex_unlock(int m){(void)m;assert(lock);lock=0;}
static int tal_queue_fetch(int q,AI_INPUT_COMMAND_T *command,int wait){(void)q;(void)wait;if(pos==count)return -1;*command=queue[pos++];return 0;}
static void tuya_ai_agent_event_s(char *s,int e,int p){(void)s;(void)e;(void)p;}
static void tuya_ai_output_stop(int b){(void)b;}
static int tuya_ai_agent_is_ready(void){return 1;}
static int tuya_ai_agent_start(char *s){(void)s;return 0;}
static void tuya_ai_output_alert(int a){(void)a;}
static int __ai_ringbuf_read(AI_INPUT_SESSION_CTX_T *c,AI_RINGBUF_HEAD_T *h,char*b){(void)c;(void)b;h->len=0;return 0;}
static void tuya_ai_agent_upload_stream(char*s,int t,int*b,char*p,int n,int total){(void)s;(void)t;(void)b;(void)p;(void)n;(void)total;}
static void tuya_ai_agent_end(char*s){(void)s;ends++;}
static void tuya_ring_buff_reset(int b){(void)b;}
static void __ai_input_free(void){}
static void tal_system_sleep(unsigned ms){assert(ms==10 && !lock && ticks<8);completed[ticks]=session.completed_stop_id;states[ticks]=session.state;ticks++;}
'''
main=r'''
int main(void){
 const AI_INPUT_COMMAND_T cases[][3]={
 {{AI_INPUT_START,0},{AI_INPUT_STOPPING,1}},
 {{AI_INPUT_START_LAZY,0},{AI_INPUT_STOPPING,1}},
 {{AI_INPUT_STOPPING,1},{AI_INPUT_START,0}},
 {{AI_INPUT_STOPPING,1},{AI_INPUT_STOPPING,2}},
 {{AI_INPUT_START,0},{AI_INPUT_STOPPING,1},{AI_INPUT_STOPPING,2}},
 {{AI_INPUT_STOPPING,1},{AI_INPUT_START,0},{AI_INPUT_STOPPING,2}}
 };
 for(int c=0;c<6;c++){
  memset(&session,0,sizeof(session));memset(&ai_input_ctx,0,sizeof(ai_input_ctx));
  memcpy(queue,cases[c],sizeof(queue));count=c<4?2:3;pos=ticks=ends=lock=0;budget=6;
  ai_input_ctx.sctx[0]=&session;
  __ai_input_thread(NULL);
  for(int i=0;i<count;i++)assert(completed[i]==0);
  if(c==2){assert(ends==0 && session.completed_stop_id==0 && session.state==AI_INPUT_PROC);}
  else {assert(ends==1 && session.completed_stop_id==(c<2?1:2) && session.state==AI_INPUT_IDLE);}
  if(c<2)assert(states[0]==AI_INPUT_PROC && states[1]==AI_INPUT_STOP && completed[2]==1);
 }
 puts("PASS six exact worker FIFO scenarios: START cannot confirm STOP; superseded STOP not confirmed; final matching STOP cleanup confirms");
}
'''
(e/'worker-test.c').write_text(prefix+structs+stubs+worker+main)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(e/'worker-test.c'),'-o',str(e/'worker-test')],check=True)
subprocess.run([str(e/'worker-test')],check=True)
