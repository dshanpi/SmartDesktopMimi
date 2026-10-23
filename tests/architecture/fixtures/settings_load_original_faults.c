#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
int main(int argc,char**argv){
 if(argc!=3)return 2;
 setenv("LV_SETTINGS_FILE",argv[2],1);
 sys_settings_t disk={.version=8,.theme_color=0x5267B8,.brightness=20,.volume=50,.wifi_enabled=true,.bluetooth_enabled=true,.theme_follow_time=true,.tuya_ai_enabled=true,.led_enabled=true,.led_brightness=22};
 if(!strcmp(argv[1],"future")) disk.version=9;
 if(!strcmp(argv[1],"brightness")) disk.brightness=0;
 if(!strcmp(argv[1],"bool")) ((unsigned char*)&disk)[offsetof(sys_settings_t,wifi_enabled)]=2;
 unsigned char original[sizeof disk];memcpy(original,&disk,sizeof disk);
 FILE*f=fopen(argv[2],"wb");if(!f)return 3;fwrite(&disk,sizeof disk,1,f);fclose(f);
 sys_settings_init();
 sys_settings_t*current=sys_settings_get();
 if(!strcmp(argv[1],"future")) { current->volume=27; if(sys_settings_save_checked()==0)return 5; }
 if(!strcmp(argv[1],"bool")){printf("wifi=%d blocked_save=%d\n",current->wifi_enabled,sys_settings_save_checked());return current->wifi_enabled && sys_settings_last_save_result()!=0?0:1;}
 f=fopen(argv[2],"rb");if(!f)return 4;fread(&disk,sizeof disk,1,f);fclose(f);
 if(!strcmp(argv[1],"future")){int preserved=!memcmp(&disk,original,sizeof disk);printf("future_original_preserved=%d version=%u\n",preserved,disk.version);return preserved?0:1;}
 printf("runtime_brightness=%d disk_brightness=%d\n",current->brightness,disk.brightness);
 return current->brightness==20 && disk.brightness==20?0:1;
}
