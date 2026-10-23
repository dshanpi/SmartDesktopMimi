#define _GNU_SOURCE
#include <assert.h>
#include "settings.c"
#define COMMON .theme_color=0x5267B8,.brightness=37,.volume=63
#define FLAGS .wifi_enabled=false,.bluetooth_enabled=false,.hdmi_enabled=true
#define WRITE(type,...) do{type value={__VA_ARGS__};assert(fwrite(&value,sizeof(value),1,f)==1);}while(0)
int main(int argc,char**argv){
 assert(argc==3);int version=atoi(argv[1]);setenv("LV_SETTINGS_FILE",argv[2],1);
 FILE*f=fopen(argv[2],"wb");assert(f);
 switch(version){
 case 0:WRITE(legacy_sys_settings_t,COMMON);break;
 case 1:WRITE(settings_v1_t,COMMON,.wifi_enabled=false);break;
 case 2:WRITE(settings_v2_t,.version=2,COMMON,.wifi_enabled=false,.bluetooth_enabled=false);break;
 case 3:WRITE(settings_v3_t,.version=3,COMMON,FLAGS);break;
 case 4:WRITE(settings_v4_t,.version=4,COMMON,FLAGS,.theme_preset_id=0);break;
 case 5:WRITE(settings_v5_t,.version=5,COMMON,FLAGS,.theme_preset_id=0,.theme_follow_time=false);break;
 case 6:WRITE(settings_v6_t,.version=6,COMMON,FLAGS,.theme_preset_id=0,.theme_follow_time=false,.bluetooth_name="Mimi test");break;
 case 7:WRITE(settings_v7_t,.version=7,COMMON,FLAGS,.theme_preset_id=0,.theme_follow_time=false,.bluetooth_name="Mimi test",.tuya_ai_enabled=false,.led_enabled=false,.led_brightness=47);break;
 case 8:WRITE(sys_settings_t,.version=8,COMMON,FLAGS,.theme_preset_id=0,.theme_follow_time=false,.bluetooth_name="Mimi test",.tuya_ai_enabled=false,.led_enabled=false,.led_brightness=47,.locale_mode=2,.appearance_mode=2);break;
 default:return 2;
 }
 fclose(f);sys_settings_init();sys_settings_t*s=sys_settings_get();
 assert(s->version==8 && s->brightness==37 && s->volume==63);
 assert(s->wifi_enabled==(version==0));assert(s->bluetooth_enabled==(version<2));assert(s->hdmi_enabled==(version>=3));
 assert(s->theme_follow_time==(version<5));assert(s->tuya_ai_enabled==(version<7));assert(s->led_enabled==(version<7));
 assert(s->led_brightness==(version<7?22:47));assert(s->locale_mode==(version<8?0U:2U));assert(s->appearance_mode==(version<8?0U:2U));
 if(version>=6)assert(!strcmp(s->bluetooth_name,"Mimi test"));
 sys_settings_t persisted;f=fopen(argv[2],"rb");assert(f);assert(fread(&persisted,sizeof persisted,1,f)==1);assert(fgetc(f)==EOF);fclose(f);
 assert(!memcmp(&persisted,s,sizeof persisted));printf("PASS migration v%d\n",version);return 0;
}
