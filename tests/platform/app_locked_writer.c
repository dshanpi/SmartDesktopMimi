#include "app_lock.h"

#include "safe_io.h"
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
int __real_renameat(int,const char*,int,const char*);
int __wrap_renameat(int a,const char*b,int c,const char*d) {
 int fd=open(getenv("READY"),O_CREAT|O_WRONLY,0600); if(fd<0) _exit(90); close(fd);
 for(int n=0;access(getenv("CONTINUE"),F_OK);n++){if(n>10000)_exit(91);usleep(1000);}
 return __real_renameat(a,b,c,d);
}
int main(int argc,char**argv){if(argc!=3)return 2;int lock=app_lock_open(argv[2]);if(lock<0)return 3;int rc=aitvbox_atomic_write_file(argv[1],"new",3,0600);printf("write_rc=%d\n",rc);return rc?1:0;}
