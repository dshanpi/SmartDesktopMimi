/* Private Ubuntu V2 query endpoint. No board driver or mutating operation. */
#include "v2_server.h"
#include "ipc/ipc_v2.h"
#include "aitvbox_ports.h"
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define CLIENTS 8
struct peer {
    int fd;
    size_t used, size, sent, pending;
    int64_t last, started;
    unsigned char input[AITVBOX_IPC_V2_MAX_FRAME+4], output[2048];
};
static struct peer peers[CLIENTS];
static int server=-1;
static char endpoint[sizeof(((struct sockaddr_un*)0)->sun_path)];
static struct stat owned;
static bool bound;
static int64_t now(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void drop(struct peer *p) {
    if(p->fd>=0)close(p->fd);
    explicit_bzero(p,sizeof(*p));p->fd=-1;
}
/* json-c on Ubuntu 20.04 does not expose UTF-8 validation flags. */
static bool utf8(const unsigned char *s,size_t size) {
    for(size_t i=0;i<size;) {
        unsigned c=s[i++],n=0,minimum=0;
        if(c<0x80){if(!c)return false;continue;}
        if(c>=0xc2 && c<=0xdf){c&=31;n=1;minimum=0x80;}
        else if(c>=0xe0 && c<=0xef){c&=15;n=2;minimum=0x800;}
        else if(c>=0xf0 && c<=0xf4){c&=7;n=3;minimum=0x10000;}
        else return false;
        if(n>size-i)return false;
        while(n--){unsigned b=s[i++];if((b&0xc0)!=0x80)return false;c=(c<<6)|(b&63);}
        if(c<minimum || c>0x10ffff || (c>=0xd800 && c<=0xdfff))return false;
    }
    return true;
}
static json_object *field(json_object *o,const char *key) {
    json_object *v=NULL;json_object_object_get_ex(o,key,&v);return v;
}
static bool plain_string(json_object *v) {
    return v && json_object_is_type(v,json_type_string) &&
        strlen(json_object_get_string(v))==(size_t)json_object_get_string_len(v);
}
static int response(struct peer *p) {
    const unsigned char *body=p->input+4;
    if(!utf8(body,p->size))return -1;
    json_tokener *t=json_tokener_new();if(!t)return -1;
    json_tokener_set_flags(t,JSON_TOKENER_STRICT);
    json_object *request=json_tokener_parse_ex(t,(const char*)body,(int)p->size);
    bool valid=json_tokener_get_error(t)==json_tokener_success &&
        (size_t)t->char_offset==p->size;
    json_tokener_free(t);
    if(!valid || aitvbox_ipc_v2_validate(request,NULL,0) ||
       json_object_get_int64(field(request,"schemaVersion"))!=2 ||
       !plain_string(field(request,"messageType")) ||
       strcmp(json_object_get_string(field(request,"messageType")),"request") ||
       !plain_string(field(request,"topic")) || !plain_string(field(request,"requestId")) ||
       field(request,"error")) {
        if(request)json_object_put(request);
        return -1;
    }
    const char *topic=json_object_get_string(field(request,"topic"));
    json_object *r=json_object_new_object(),*payload=json_object_new_object();
    json_object_object_add(r,"schemaVersion",json_object_new_int(2));
    json_object_object_add(r,"messageType",json_object_new_string("response"));
    json_object_object_add(r,"topic",json_object_get(field(request,"topic")));
    json_object_object_add(r,"requestId",json_object_get(field(request,"requestId")));
    json_object_object_add(r,"payload",payload);
    const char *error=NULL;
    if(strcmp(topic,"system.ping") && strcmp(topic,"platform.describe"))error="UNKNOWN_TOPIC";
    else if(json_object_object_length(field(request,"payload")))error="INVALID_ARGUMENT";
    else if(!strcmp(topic,"system.ping"))json_object_object_add(payload,"alive",json_object_new_boolean(true));
    else {
        json_object_object_add(payload,"platformId",json_object_new_string("ubuntu-host"));
        json_object_object_add(payload,"soc",json_object_new_string("host"));
        json_object_object_add(payload,"architecture",json_object_new_string("x86_64"));
        json_object_object_add(payload,"providerVersion",json_object_new_string("0.2.0"));
        json_object_object_add(payload,"abiVersion",json_object_new_int(AITVBOX_PLATFORM_ABI_VERSION));
        /* SDL belongs to the application: this service provides no display,
         * board control, secure identity or updater capability. */
        json_object_object_add(payload,"capabilities",json_object_new_int64(0));
        json_object_object_add(payload,"localNetworkObservation",json_object_new_boolean(true));
        json_object_object_add(payload,"releaseQualified",json_object_new_boolean(false));
    }
    if(error) {
        json_object *e=json_object_new_object();json_object_object_add(r,"error",e);
        json_object_object_add(e,"code",json_object_new_string(error));
        json_object_object_add(e,"message",json_object_new_string(!strcmp(error,"UNKNOWN_TOPIC")?"the requested topic is not registered":"this query requires an empty payload"));
        json_object_object_add(e,"retryable",json_object_new_boolean(false));
    }
    const char *encoded=json_object_to_json_string_ext(r,JSON_C_TO_STRING_PLAIN);
    size_t size=strlen(encoded);int result=-1;
    if(size<=sizeof(p->output)-4) {
        uint32_t prefix=htonl((uint32_t)size);memcpy(p->output,&prefix,4);
        memcpy(p->output+4,encoded,size);p->pending=size+4;p->sent=0;p->started=now();result=0;
    }
    json_object_put(r);json_object_put(request);return result;
}
static int receive_frame(struct peer *p) {
    size_t need=p->used<4?4:4+p->size;
    ssize_t n=recv(p->fd,p->input+p->used,need-p->used,MSG_DONTWAIT);
    if(n<0)return errno==EAGAIN || errno==EINTR?0:-1;
    if(!n)return -1;
    if(!p->used)p->started=now();
    p->used+=(size_t)n;p->last=now();
    if(p->used==4) {
        uint32_t prefix;memcpy(&prefix,p->input,4);p->size=ntohl(prefix);
        if(!p->size || p->size>AITVBOX_IPC_V2_MAX_FRAME)return -1;
    }
    if(p->used==p->size+4) {
        int rc=response(p);explicit_bzero(p->input,sizeof(p->input));p->used=p->size=0;return rc;
    }
    return 0;
}
static int send_frame(struct peer *p) {
    ssize_t n=send(p->fd,p->output+p->sent,p->pending-p->sent,MSG_NOSIGNAL|MSG_DONTWAIT);
    if(n<0)return errno==EAGAIN || errno==EINTR?0:-1;
    if(!n)return -1;
    p->sent+=(size_t)n;p->last=now();
    if(p->sent==p->pending){explicit_bzero(p->output,sizeof(p->output));p->sent=p->pending=0;p->started=0;}
    return 0;
}
void ubuntu_v2_stop(void) {
    for(int i=0;i<CLIENTS;i++)drop(&peers[i]);
    if(server>=0)close(server);
    server=-1;
    struct stat s;if(bound && !lstat(endpoint,&s) && s.st_dev==owned.st_dev && s.st_ino==owned.st_ino)unlink(endpoint);
    bound=false;
}
int ubuntu_v2_start(const char *v1_path) {
    for(int i=0;i<CLIENTS;i++)peers[i].fd=-1;
    if(snprintf(endpoint,sizeof(endpoint),"%s.v2",v1_path)>=(int)sizeof(endpoint))return -1;
    /* Caller already holds the same-directory singleton lock and has checked
     * the private canonical parent. Never reclaim a file or another UID. */
    struct stat s;
    if(!lstat(endpoint,&s)){if(!S_ISSOCK(s.st_mode) || s.st_uid!=geteuid() || unlink(endpoint))return -1;}
    else if(errno!=ENOENT)return -1;
    server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    struct sockaddr_un address={.sun_family=AF_UNIX};memcpy(address.sun_path,endpoint,strlen(endpoint)+1);
    if(server<0 || bind(server,(struct sockaddr*)&address,sizeof(address)))goto fail;
    bound=true;
    if(lstat(endpoint,&owned) || chmod(endpoint,0600) || listen(server,CLIENTS))goto fail;
    return 0;
fail:ubuntu_v2_stop();return -1;
}
int ubuntu_v2_step(void) {
    struct pollfd fds[CLIENTS+1]={{.fd=server,.events=POLLIN}};
    for(int i=0;i<CLIENTS;i++){fds[i+1].fd=peers[i].fd;fds[i+1].events=peers[i].pending?POLLOUT:POLLIN;}
    if(poll(fds,CLIENTS+1,0)<0)return errno==EINTR?0:-1;
    if(fds[0].revents&(POLLERR|POLLHUP|POLLNVAL))return -1;
    for(int i=0;i<CLIENTS;i++)if(peers[i].fd>=0) {
        struct peer *p=&peers[i];short flags=fds[i+1].revents;
        if(flags&(POLLERR|POLLHUP|POLLNVAL) || now()-p->last>20000 ||
           (p->started && now()-p->started>5000) ||
           ((flags&POLLIN) && receive_frame(p)) || ((flags&POLLOUT) && send_frame(p)))drop(p);
    }
    if(fds[0].revents&POLLIN) {
        int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
        if(fd>=0) {
            struct ucred cred;socklen_t len=sizeof(cred);int slot=-1;
            if(!getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&cred,&len) && cred.uid==geteuid())
                for(int i=0;i<CLIENTS;i++)if(peers[i].fd<0){slot=i;break;}
            if(slot<0)close(fd);else {peers[slot].fd=fd;peers[slot].last=now();}
        }
    }
    return 0;
}
