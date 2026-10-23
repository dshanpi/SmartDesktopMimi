#include "ipc/ipc_socket.h"
#include "system/config/app_config.h"
#include "system/log/app_log.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
static app_config_t cfg;
const app_config_t *app_config_get(void){return &cfg;}
void app_log_write(app_log_level_t l,const char *m,const char *f,...){(void)l;(void)m;(void)f;}
static int raw_socket(void){int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);assert(fd>=0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,cfg.ipc_socket_path);assert(!bind(fd,(struct sockaddr*)&a,sizeof(a)));return fd;}
static void unchanged(struct stat before){struct stat after;assert(!lstat(cfg.ipc_socket_path,&after));assert(before.st_dev==after.st_dev && before.st_ino==after.st_ino);}
int main(int argc,char **argv){cfg.ipc_socket_mode=0600;
 if(argc==3){snprintf(cfg.ipc_socket_path,sizeof(cfg.ipc_socket_path),"%s",argv[2]);int fd=123;int rc=ipc_server_start(&fd);return rc<0 && fd==-1?0:1;}
 char dir[]="/tmp/mimi-owned-test-XXXXXX";assert(mkdtemp(dir));snprintf(cfg.ipc_socket_path,sizeof(cfg.ipc_socket_path),"%s/socket",dir);char path[108],lock[114];strcpy(path,cfg.ipc_socket_path);snprintf(lock,sizeof(lock),"%s.lock",path);
 int a=-1,b=-1;assert(!ipc_server_start(&a));struct stat first;assert(!lstat(path,&first));assert(ipc_server_start(&b)<0 && b==-1);unchanged(first);
 pid_t child=fork();assert(child>=0);if(!child){execl("/proc/self/exe","test","--try",path,(char*)NULL);_exit(99);}int status;waitpid(child,&status,0);assert(WIFEXITED(status) && WEXITSTATUS(status)==0);unchanged(first);
 int client,accepted;assert(!ipc_client_connect(&client));assert(!ipc_server_accept(a,&accepted));close(client);close(accepted);ipc_server_stop(a);assert(access(path,F_OK)<0);
 puts("PASS same-process and separate-process active collision preserves live endpoint");
 int raw=raw_socket();assert(!listen(raw,5));assert(!lstat(path,&first));assert(ipc_server_start(&a)<0 && errno==EADDRINUSE);unchanged(first);close(raw);unlink(path);
 raw=raw_socket();close(raw);assert(!ipc_server_start(&a));ipc_server_stop(a);assert(access(path,F_OK)<0);
 puts("PASS live legacy listener preserved and owned stale socket recovered");
 int file=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(file>=0);assert(write(file,"keep",4)==4);close(file);assert(!lstat(path,&first));assert(ipc_server_start(&a)<0);unchanged(first);unlink(path);
 char target[128];snprintf(target,sizeof(target),"%s/target",dir);file=open(target,O_WRONLY|O_CREAT|O_EXCL,0600);assert(file>=0);close(file);assert(!symlink(target,path));assert(!lstat(path,&first));assert(ipc_server_start(&a)<0);unchanged(first);unlink(path);
 assert(!ipc_server_start(&a));unlink(path);file=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);assert(file>=0);close(file);assert(!lstat(path,&first));ipc_server_stop(a);unchanged(first);unlink(path);
 assert(!unlink(lock));assert(!symlink(target,lock));assert(ipc_server_start(&a)<0);struct stat st;assert(!lstat(lock,&st) && S_ISLNK(st.st_mode));unlink(lock);unlink(target);rmdir(dir);
 puts("PASS regular file/symlink/replacement and lock symlink protection");return 0;}
