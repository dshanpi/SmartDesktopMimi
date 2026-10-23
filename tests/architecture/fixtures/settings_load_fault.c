#define _GNU_SOURCE
#include <assert.h>
#include "settings.c"
static const char *fault;
FILE *__real_fopen(const char*,const char*);
FILE *__wrap_fopen(const char*p,const char*m){if(fault&&!strcmp(fault,"open")&&!strcmp(m,"rb")){errno=EACCES;return NULL;}return __real_fopen(p,m);}
int __real_fseek(FILE*,long,int);
int __wrap_fseek(FILE*f,long o,int w){if(fault&&!strcmp(fault,"seek")){errno=EIO;return -1;}return __real_fseek(f,o,w);}
size_t __real_fread(void*,size_t,size_t,FILE*);
size_t __wrap_fread(void*p,size_t s,size_t n,FILE*f){if(fault&&!strcmp(fault,"read")){errno=EIO;return 0;}return __real_fread(p,s,n,f);}
int main(int argc,char**argv){
 assert(argc==3);setenv("LV_SETTINGS_FILE",argv[2],1);
 sys_settings_t value={.version=8,.theme_color=0x5267b8,.brightness=37,.volume=63,.wifi_enabled=true,.bluetooth_enabled=true,.theme_follow_time=true,.tuya_ai_enabled=true,.led_enabled=true,.led_brightness=22};
 unsigned char raw[sizeof(value)+1];memcpy(raw,&value,sizeof value);raw[sizeof value]=0xcc;size_t length=sizeof value;
 if(!strcmp(argv[1],"short"))length--;
 if(!strcmp(argv[1],"extra"))length++;
 if(!strcmp(argv[1],"empty"))length=0;
 if(!strncmp(argv[1],"bool",4)){
  const size_t offsets[]={offsetof(sys_settings_t,wifi_enabled),offsetof(sys_settings_t,bluetooth_enabled),offsetof(sys_settings_t,hdmi_enabled),offsetof(sys_settings_t,theme_follow_time),offsetof(sys_settings_t,tuya_ai_enabled),offsetof(sys_settings_t,led_enabled)};
  int i=atoi(argv[1]+4);assert(i>=0&&i<6);raw[offsets[i]]=255;
 }
 FILE*f=fopen(argv[2],"wb");assert(f);assert(fwrite(raw,1,length,f)==length);fclose(f);
 fault=argv[1];sys_settings_init();fault=NULL;
 if(!strcmp(argv[1],"replace")){
  value.version=9;memcpy(raw,&value,sizeof value);f=fopen(argv[2],"wb");assert(f);assert(fwrite(raw,1,length,f)==length);fclose(f);
 }
 sys_settings_get()->volume=24;assert(sys_settings_save_checked()!=0);
 unsigned char after[sizeof(raw)+1];f=fopen(argv[2],"rb");assert(f);size_t got=fread(after,1,sizeof after,f);fclose(f);assert(got==length&&!memcmp(after,raw,length));
 printf("PASS preserved %s and rejected later save\n",argv[1]);return 0;
}
