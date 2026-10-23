#define _GNU_SOURCE
#include <assert.h>
#include "settings.c"
int main(int argc,char**argv){
 assert(argc==2);setenv("LV_SETTINGS_FILE",argv[1],1);sys_settings_init();
 printf("volume=%d last_save=%d\n",sys_settings_get()->volume,sys_settings_last_save_result());
 return sys_settings_get()->volume==50 && sys_settings_save_checked()!=0 ? 0:1;
}
