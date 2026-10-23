from pathlib import Path
import subprocess,sys
p=Path(__file__).resolve().parents[3] / 'third_party/TuyaOpen/apps/tuya.ai/your_chat_bot/src/app_chat_bot.c'
s=p.read_text(); e=Path(sys.argv[1]).resolve()
names=['static OPERATE_RET __main_ui_break_chat(void)','OPERATE_RET app_chat_bot_set_assistant_enabled(BOOL_T enabled)','static OPERATE_RET __main_ui_handle_exit_free_chat(void)']
functions=[]
for name in names:
 start=s.index(name+chr(10)+'{');end=s.index(chr(10)+'}',start)+2
 functions.append(s[start:end])
prefix=r'''
#include <assert.h>
#include <stdio.h>
typedef int OPERATE_RET;
typedef int BOOL_T;
enum {OPRT_OK=0,TRUE=1,FALSE=0,AI_AUDIO_PLAYER_ALL=3,AI_EVENT_CHAT_BREAK=9,AI_CHAT_MODE_WAKEUP=1,AI_CHAT_MODE_HOLD=2};
#define ENABLE_COMP_AI_AUDIO 1
#define PR_WARN(...) ((void)0)
static int sg_assistant_enabled,stop_result,input_result,switch_result,stop_calls,input_calls,break_calls,switch_calls,error_messages,mode_messages;
static int ai_audio_player_stop(int type){assert(type==AI_AUDIO_PLAYER_ALL);stop_calls++;return stop_result;}
static int tuya_ai_input_stop_checked(void){input_calls++;return input_result;}
static int tuya_ai_agent_event(int event,int arg){assert(event==AI_EVENT_CHAT_BREAK && arg==0);break_calls++;return 0;}
static int ai_mode_switch(int mode){assert(mode==AI_CHAT_MODE_WAKEUP || mode==AI_CHAT_MODE_HOLD);switch_calls++;return switch_result;}
static void __main_ui_bridge_send_state(const char *s){assert(s[0]=='e');error_messages++;}
static void __main_ui_bridge_send_mode(int mode){(void)mode;mode_messages++;}
'''
suffix=r'''
int main(void){
 for(int action=0;action<2;action++)for(int enabled=0;enabled<2;enabled++)for(int stop=0;stop<2;stop++)for(int mode=0;mode<2;mode++)for(int input=0;input<2;input++){
  sg_assistant_enabled=enabled;stop_result=stop?-7:0;switch_result=mode?-9:0;input_result=input?-20:0;
  stop_calls=input_calls=break_calls=switch_calls=error_messages=mode_messages=0;
  int result=action?app_chat_bot_set_assistant_enabled(!enabled):__main_ui_handle_exit_free_chat();
  assert(result==(stop?-7:input?-20:mode?-9:0));
  assert(stop_calls==1 && input_calls==1 && break_calls==1);
  assert(switch_calls==(!stop && !input));
  assert(mode_messages==(!stop && !input && !mode));
  assert(sg_assistant_enabled==((action && !stop && !input && !mode)?!enabled:enabled));
  if(stop || input)assert(error_messages==1);
 }
 puts("PASS 32 exit/enable/disable combinations; audio/input stop failure prevents mode success; remaining cleanup attempted");
}
'''
(e/'exit-test.c').write_text(prefix+chr(10).join(functions)+suffix)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(e/'exit-test.c'),'-o',str(e/'exit-test')],check=True)
subprocess.run([str(e/'exit-test')],check=True)
