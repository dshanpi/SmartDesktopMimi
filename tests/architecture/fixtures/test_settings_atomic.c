#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "settings.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
static int fault, writes;
ssize_t __real_pwrite(int,const void*,size_t,off_t);
int __real_fsync(int);
int __real_rename(const char*,const char*);
ssize_t __wrap_pwrite(int fd,const void*b,size_t n,off_t off) {
 if(fault==1){errno=ENOSPC;return -1;}
 if(fault==2){if(writes++==0)return __real_pwrite(fd,b,8,off);errno=EIO;return -1;}
 return __real_pwrite(fd,b,n,off);
}
int __wrap_fsync(int fd){
 struct stat st;assert(fstat(fd,&st)==0);int dir=S_ISDIR(st.st_mode);
 if((fault==3&&!dir)||(fault==5&&dir)){errno=EIO;return -1;}
 if(fault==7&&dir)_exit(77);
 return __real_fsync(fd);
}
int __wrap_rename(const char*a,const char*b){
 if(fault==4){errno=EIO;return -1;}
 if(fault==6)_exit(76);
 return __real_rename(a,b);
}
static void read_record(const char*path,sys_settings_t*out){
 int fd=open(path,O_RDONLY);assert(fd>=0);assert(read(fd,out,sizeof(*out))==sizeof(*out));char extra;assert(read(fd,&extra,1)==0);close(fd);
}
static void cleanup(const char*dir){
 DIR*d=opendir(dir);assert(d);struct dirent*de;char p[512];
 while((de=readdir(d))){if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;snprintf(p,sizeof(p),"%s/%s",dir,de->d_name);assert(unlink(p)==0);}closedir(d);assert(rmdir(dir)==0);
}
static void setup(char*dir,char*path){
 assert(mkdtemp(dir));snprintf(path,256,"%s/settings",dir);assert(setenv("LV_SETTINGS_FILE",path,1)==0);fault=0;writes=0;
 sys_settings_init();sys_settings_get()->brightness=67;sys_settings_get()->volume=35;assert(sys_settings_save_checked()==0);
}
static void failed_save(int mode){
 char dir[]="/tmp/mimi-save-atomic-XXXXXX",path[256];setup(dir,path);sys_settings_t before,after;read_record(path,&before);
 sys_settings_get()->brightness=42;fault=mode;assert(sys_settings_save_checked()<0);assert(sys_settings_last_save_result()<0);fault=0;read_record(path,&after);
 if(mode==5){assert(after.brightness==42);assert(sys_settings_get()->brightness==42);}else assert(memcmp(&before,&after,sizeof(before))==0);
 assert(sys_settings_save_checked()==0);assert(sys_settings_last_save_result()==0);read_record(path,&after);assert(after.brightness==42 && after.volume==35);
 cleanup(dir);
}
static void interrupted_save(int mode){
 char dir[]="/tmp/mimi-save-exit-XXXXXX",path[256];setup(dir,path);sys_settings_t before,after;read_record(path,&before);
 pid_t child=fork();assert(child>=0);if(child==0){fault=mode;sys_settings_get()->brightness=42;(void)sys_settings_save_checked();_exit(99);}
 int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==70+mode);read_record(path,&after);
 if(mode==6)assert(memcmp(&before,&after,sizeof(before))==0);else assert(after.brightness==42 && after.volume==35);
 cleanup(dir);
}
static void concurrent_merge(void){
 char dir[]="/tmp/mimi-save-merge-XXXXXX",path[256];setup(dir,path);int ready[2],go[2];assert(pipe(ready)==0&&pipe(go)==0);pid_t children[2];
 for(int i=0;i<2;i++){children[i]=fork();assert(children[i]>=0);if(children[i]==0){close(ready[0]);close(go[1]);
  sys_settings_init();if(i==0)sys_settings_get()->brightness=82;else sys_settings_get()->volume=23;
  assert(write(ready[1],"r",1)==1);char c;assert(read(go[0],&c,1)==1);assert(sys_settings_save_checked()==0);_exit(0);
 }}
 close(ready[1]);close(go[0]);char c;for(int i=0;i<2;i++)assert(read(ready[0],&c,1)==1);assert(write(go[1],"gg",2)==2);close(ready[0]);close(go[1]);
 for(int i=0;i<2;i++){int status;assert(waitpid(children[i],&status,0)==children[i]);assert(WIFEXITED(status)&&WEXITSTATUS(status)==0);}
 sys_settings_t result;read_record(path,&result);assert(result.brightness==82 && result.volume==23);cleanup(dir);
}
int main(void){for(int i=1;i<=5;i++)failed_save(i);interrupted_save(6);interrupted_save(7);for(int i=0;i<20;i++)concurrent_merge();puts("PASS five faults, two process-exit boundaries, twenty two-process merges");}
