from pathlib import Path
import subprocess,sys
r=Path(__file__).resolve().parents[3] / 'third_party/TuyaOpen'
e=Path(sys.argv[1]).resolve()
s=(r/'src/tuya_ai_service/svc_ai_agent/src/tuya_ai_input.c').read_text();a=s.index('OPERATE_RET tuya_ai_input_stop_checked_s(');b=s.index('VOID tuya_ai_input_stop_s(',a)
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
typedef char CHAR_T;
typedef unsigned UINT_T;
typedef uint64_t UINT64_T;
typedef int OPERATE_RET;
typedef int BOOL_T;
enum{OPRT_OK=0,OPRT_TIMEOUT=-20,OPRT_RESOURCE_NOT_READY=-23,FALSE=0,TRUE=1,AI_INPUT_STOPPING=4};
typedef struct{int state;UINT64_T request_id;} AI_INPUT_COMMAND_T;
typedef struct{UINT64_T next_stop_id,completed_stop_id;int queue_sync,queue,lazy_input;} AI_INPUT_SESSION_CTX_T;
static struct{int mutex;} ai_input_ctx;
static AI_INPUT_SESSION_CTX_T ctx;
static int missing,mode,post_error,sleeps,locked;
static AI_INPUT_COMMAND_T posted;
static AI_INPUT_SESSION_CTX_T *__ai_get_sctx(char *code){(void)code;return missing?NULL:&ctx;}
static void tal_mutex_lock(int m){assert(m && !locked);locked=1;}
static void tal_mutex_unlock(int m){assert(m && locked);locked=0;}
static int tal_queue_post(int queue,AI_INPUT_COMMAND_T *cmd,int delay){(void)queue;(void)delay;assert(locked);posted=*cmd;return post_error;}
static void tal_system_sleep(unsigned ms){assert(ms==10 && !locked);sleeps++;ctx.queue_sync=TRUE;
 if(mode==1 && sleeps==3)ctx.completed_stop_id=posted.request_id;
 if(mode==2)ctx.completed_stop_id=posted.request_id+1;
 if(mode==3 && sleeps==60)ctx.completed_stop_id=posted.request_id;
}
'''
suffix=r'''
int main(void){
 assert(tuya_ai_input_stop_checked_s(NULL)==OPRT_RESOURCE_NOT_READY);
 ai_input_ctx.mutex=1;missing=1;assert(tuya_ai_input_stop_checked_s(NULL)==0);missing=0;
 post_error=-55;assert(tuya_ai_input_stop_checked_s(NULL)==-55 && sleeps==0);post_error=0;
 for(mode=0;mode<4;mode++) {
  sleeps=0;ctx.completed_stop_id=0;
  int got=tuya_ai_input_stop_checked_s(NULL);
  assert(got==((mode==1 || mode==3)?0:OPRT_TIMEOUT));
  assert(sleeps==(mode==1?3:60));
 }
 ctx.next_stop_id=UINT64_MAX;
 assert(tuya_ai_input_stop_checked_s(NULL)==OPRT_RESOURCE_NOT_READY);
 puts("PASS no-init/no-session/post-failure/start-only-ack/wrong-token/matching-token/final-wait/token-exhaustion");
}
'''
(e/'checked-test.c').write_text(prefix+s[a:b]+suffix)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(e/'checked-test.c'),'-o',str(e/'checked-test')],check=True)
subprocess.run([str(e/'checked-test')],check=True)
