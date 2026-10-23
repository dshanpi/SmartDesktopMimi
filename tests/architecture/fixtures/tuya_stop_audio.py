from pathlib import Path
import subprocess,sys
here=Path(sys.argv[1]).resolve()
src=(Path(__file__).resolve().parents[3] / 'third_party/TuyaOpen/src/ai_components/ai_audio/src/ai_audio_player.c').read_text()
start=src.index('OPERATE_RET ai_audio_player_stop(')
end=src.index('/**',start)
body=src[start:end]
prefix=r'''
#include <assert.h>
#include <stdio.h>
typedef int OPERATE_RET;
typedef int AI_AUDIO_PLAYER_TYPE_E;
enum {OPRT_OK=0,OPRT_INVALID_PARM=-99,AI_AUDIO_PLAYER_FG,AI_AUDIO_PLAYER_BG,AI_AUDIO_PLAYER_ALL};
#define PR_DEBUG(...) ((void)0)
#define PR_ERR(...) ((void)0)
#define TUYA_CALL_ERR_LOG(func) do { rt=(func); if(OPRT_OK!=rt) {PR_ERR("ret:%d",rt);} } while(0)
static int __s_tone_playlist=0,__s_tone_player=1,__s_music_playlist=2,__s_music_player=3;
static int results[4],calls[4];
static int tuya_ai_playlist_clear(int id){calls[id]++;return results[id];}
static int tuya_ai_player_stop(int id){calls[id]++;return results[id];}
'''
suffix=r'''
int main(void) {
 for(int type=AI_AUDIO_PLAYER_FG;type<=AI_AUDIO_PLAYER_ALL;type++) {
  for(int mask=0;mask<16;mask++) {
   int first=0;
   for(int i=0;i<4;i++) {results[i]=(mask&(1<<i))?(-10-i):0;calls[i]=0;}
   for(int i=0;i<4;i++) if(type==AI_AUDIO_PLAYER_ALL || (type==AI_AUDIO_PLAYER_FG ? i<2 : i>=2)) {
    if(!first && results[i]) first=results[i];
   }
   int got=ai_audio_player_stop(type);
   if(got!=first) {fprintf(stderr,"type=%d mask=%d returned=%d expected=%d\n",type,mask,got,first);return 1;}
   for(int i=0;i<4;i++) assert(calls[i]==(type==AI_AUDIO_PLAYER_ALL || (type==AI_AUDIO_PLAYER_FG ? i<2:i>=2)));
  }
 }
 assert(ai_audio_player_stop(999)==OPRT_INVALID_PARM);
 puts("PASS 48 failure combinations, all selected cleanup attempted, invalid type rejected");
}
'''
label='audio'
(here/(label+'.c')).write_text(prefix+body+suffix)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(here/(label+'.c')),'-o',str(here/label)],check=True)
raise SystemExit(subprocess.run([str(here/label)]).returncode)
