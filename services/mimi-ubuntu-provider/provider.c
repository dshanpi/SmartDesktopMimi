/* Ubuntu session service. It never loads a board driver or changes host settings.
 * V1 message definitions are shared with the unchanged desktop contract. */
#include "ipc/ipc_socket.h"
#include "v2_server.h"
#include "middleware/middleware.h"
#include "system/backend_types.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>

#define CLIENTS 8
#define FRAME_DEADLINE 5000
#define IDLE_DEADLINE 20000
_Static_assert(sizeof(ipc_header_t)==16, "Frozen V1 header changed");
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping=1; }
static int64_t ticks(void) {
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) return 0;
    return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;
}
struct client {int fd; size_t used; ipc_msg_t msg; int64_t last, started;};
static void disconnect(struct client *c) {
    if(c->fd>=0) close(c->fd);
    explicit_bzero(c,sizeof(*c)); c->fd=-1;
}
static int reply(int fd,unsigned topic,const void *data,size_t size) {
    return ipc_server_send(fd,topic,data,(uint32_t)size,IPC_FLAG_NONE);
}
#define REPLY(fd,topic,value) reply(fd,topic,&value,sizeof(value))

static int network(int fd) {
    network_info_t status={0}; struct ifaddrs *list=NULL;
    if(getifaddrs(&list)==0) {
        for(struct ifaddrs *i=list;i;i=i->ifa_next) {
            if(!i->ifa_addr || i->ifa_addr->sa_family!=AF_INET ||
               !(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_RUNNING) ||
               (i->ifa_flags & IFF_LOOPBACK) || strlen(i->ifa_name)>=sizeof(status.ifname)) continue;
            if(status.has_ip && strcmp(i->ifa_name,status.ifname)>=0) continue;
            const struct sockaddr_in *address=(const struct sockaddr_in*)i->ifa_addr;
            if(!inet_ntop(AF_INET,&address->sin_addr,status.ip_addr,sizeof(status.ip_addr))) continue;
            snprintf(status.ifname,sizeof(status.ifname),"%s",i->ifa_name); status.has_ip=true;
        }
        freeifaddrs(list);
    }
    /* An address is a local observation, not a connectivity/cloud claim. */
    return REPLY(fd,TOPIC_NETWORK_INFO,status);
}
static int cloud(int fd) {
    cloud_status_t s={.state=CLOUD_STATE_UNAVAILABLE,.bind_token_error=-ENOTSUP};
    return REPLY(fd,TOPIC_CLOUD_STATUS,s);
}
static int wifi(int fd) {
    wifi_runtime_status_t s={.last_error_code=-ENOTSUP}; return REPLY(fd,TOPIC_WIFI_RUNTIME,s);
}
static int ai(int fd) {
    ai_status_t s={.state=AI_STATE_ERROR,.last_error_code=-ENOTSUP};return REPLY(fd,TOPIC_AI_STATUS,s);
}
static int hdmi(int fd) {
    hdmi_preview_status_t s={.state=HDMI_PREVIEW_STATE_ERROR,.exit_code=-ENOTSUP};return REPLY(fd,TOPIC_HDMI_PREVIEW_STATUS,s);
}
static int video(int fd) {
    video_status_t s={.current_index=-1};
    snprintf(s.status_text,sizeof(s.status_text),"Video provider unavailable on this system");
    return REPLY(fd,TOPIC_VIDEO_STATUS,s);
}
static int bt(int fd) {
    bt_runtime_t s={0};return REPLY(fd,TOPIC_BT_RUNTIME,s);
}
static int ota(int fd) {
    ota_status_t s={.state=OTA_STATE_ERROR,.error_code=-ENOTSUP};return REPLY(fd,TOPIC_OTA_STATUS,s);
}
static int snapshot(int fd) {
    sensor_status_t s={.err_code=-ENOTSUP};
    return network(fd) || cloud(fd) || wifi(fd) || ai(fd) || hdmi(fd) ||
           video(fd) || bt(fd) || ota(fd) || REPLY(fd,TOPIC_SENSOR_STATUS,s);
}

static int dispatch(struct client *c) {
    ipc_msg_t *m=&c->msg;size_t size=m->header.payload_len;
    if(m->header.flags==IPC_FLAG_HEARTBEAT) {
        if(size || m->header.topic!=0) return -1;
        return ipc_server_send(c->fd,0,NULL,0,IPC_FLAG_HEARTBEAT);
    }
    if(m->header.flags!=IPC_FLAG_NONE && m->header.flags!=IPC_FLAG_REQUEST) return -1;
    /* Payloads are only shape-checked, never interpreted as a command or URL.
     * Unsupported requests return an authoritative unavailable snapshot. */
    switch(m->header.topic) {
    case TOPIC_WIFI_COMMAND: return size==sizeof(wifi_cmd_t)?wifi(c->fd):-1;
    case TOPIC_CLOUD_COMMAND: return size==sizeof(cloud_cmd_t)?cloud(c->fd):-1;
    case TOPIC_AI_COMMAND: return size==sizeof(ai_cmd_t)?ai(c->fd):-1;
    case TOPIC_HDMI_COMMAND: return size==sizeof(hdmi_cmd_t)?hdmi(c->fd):-1;
    case TOPIC_BT_COMMAND: return size==sizeof(bt_cmd_t)?bt(c->fd):-1;
    case TOPIC_OTA_COMMAND: return size==sizeof(ota_cmd_t)?ota(c->fd):-1;
    case TOPIC_VIDEO_CONTROL:
        if(size!=sizeof(video_cmd_t)) return -1;
        {video_playlist_item_t s={.total_count=0};
         if(REPLY(c->fd,TOPIC_VIDEO_PLAYLIST,s)) return -1;}
        return video(c->fd);
    case TOPIC_LED_COMMAND:
        /* This frozen V1 topic has no reply payload. Signal unsupported at
         * transport level without echoing any command body. */
        return size==sizeof(led_cmd_t)?ipc_server_send(c->fd,TOPIC_LED_COMMAND,NULL,0,IPC_FLAG_ERROR):-1;
    default:return -1;
    }
}
static int read_client(struct client *c) {
    /* Bound per-loop work; a noisy client cannot monopolize the service. */
    for(int frame=0;frame<8;frame++) {
        size_t need=sizeof(ipc_header_t);
        if(c->used>=need) need+=c->msg.header.payload_len;
        if(c->used<need) {
            ssize_t n=recv(c->fd,(char*)&c->msg+c->used,need-c->used,MSG_DONTWAIT);
            if(n<0) return (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)?0:-1;
            if(n==0) return -1;
            if(!c->used)c->started=ticks();
            c->used+=(size_t)n; c->last=ticks();
        }
        if(c->used<sizeof(ipc_header_t)) continue;
        if(c->msg.header.magic!=IPC_MAGIC || c->msg.header.payload_len>IPC_MAX_PAYLOAD_SIZE || c->msg.header.topic>=TOPIC_MAX) return -1;
        if(c->used<sizeof(ipc_header_t)+c->msg.header.payload_len) continue;
        if(dispatch(c)) return -1;
        /* Clear any sensitive command contents before accepting the next one. */
        explicit_bzero(&c->msg,sizeof(c->msg));c->used=0;c->started=0;
    }
    return 0;
}
static int endpoint_lock(const char *path) {
    char parent[PATH_MAX],real[PATH_MAX],lock[PATH_MAX];struct stat s;
    if(!path || path[0]!='/' || strlen(path)>=sizeof(((struct sockaddr_un*)0)->sun_path)) return -1;
    snprintf(parent,sizeof(parent),"%s",path);char *slash=strrchr(parent,'/');*slash=0;
    if(!*parent || !realpath(parent,real) || strcmp(real,parent) || lstat(parent,&s) || !S_ISDIR(s.st_mode) ||
       s.st_uid!=geteuid() || (s.st_mode&0777)!=0700) return -1;
    if(snprintf(lock,sizeof(lock),"%s/provider.lock",parent)>=(int)sizeof(lock))return -1;
    int fd=open(lock,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)return -1;
    if(fstat(fd,&s) || !S_ISREG(s.st_mode) || s.st_uid!=geteuid() || (s.st_mode&0777)!=0600 || flock(fd,LOCK_EX|LOCK_NB)) {close(fd);return -1;}
    if(lstat(path,&s)==0) {
        if(!S_ISSOCK(s.st_mode) || s.st_uid!=geteuid() || unlink(path)) {close(fd);return -1;}
    } else if(errno!=ENOENT) {close(fd);return -1;}
    return fd;
}
int main(int argc,char **argv) {
    if(argc!=3 || strcmp(argv[1],"--socket")) {fputs("usage: mimi-ubuntu-provider --socket PRIVATE_DIRECTORY/ipc.sock\n",stderr);return 2;}
    umask(0077);signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGPIPE,SIG_IGN);
    int lock=endpoint_lock(argv[2]);if(lock<0) {fputs("Provider refused: private directory, endpoint or single-instance lock invalid\n",stderr);return 1;}
    int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);int rc=1;bool bound=false, v2_started=false;struct stat owned={0};
    struct sockaddr_un addr={.sun_family=AF_UNIX};snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",argv[2]);
    if(server<0 || bind(server,(struct sockaddr*)&addr,sizeof(addr)))goto done;
    bound=true;if(lstat(argv[2],&owned) || chmod(argv[2],0600) || listen(server,CLIENTS))goto done;
    if(ubuntu_v2_start(argv[2]))goto done;
    v2_started=true;
    struct client clients[CLIENTS];for(int i=0;i<CLIENTS;i++){memset(&clients[i],0,sizeof(clients[i]));clients[i].fd=-1;}
    fputs("Ubuntu session provider ready (local network observation; board and cloud controls unavailable)\n",stdout);fflush(stdout);
    int64_t next=ticks()+2000;
    while(!stopping) {
        if(ubuntu_v2_step())break;
        struct pollfd fds[CLIENTS+1]={{.fd=server,.events=POLLIN}};
        for(int i=0;i<CLIENTS;i++){fds[i+1].fd=clients[i].fd;fds[i+1].events=POLLIN;}
        int polled=poll(fds,CLIENTS+1,100);
        if(polled<0){if(errno==EINTR)continue;break;}
        for(int i=0;i<CLIENTS;i++)if(clients[i].fd>=0) {
            short flags=fds[i+1].revents;int64_t now=ticks();
            if(flags&(POLLERR|POLLHUP|POLLNVAL) || now-clients[i].last>IDLE_DEADLINE ||
               (clients[i].used && now-clients[i].started>FRAME_DEADLINE) ||
               ((flags&POLLIN) && read_client(&clients[i])))disconnect(&clients[i]);
        }
        if(fds[0].revents&POLLIN) {
            int fd=accept4(server,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);
            if(fd>=0) {
                struct ucred peer;socklen_t len=sizeof(peer);int slot=-1;
                if(!getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&len) && peer.uid==geteuid())
                    for(int i=0;i<CLIENTS;i++)if(clients[i].fd<0){slot=i;break;}
                if(slot<0)close(fd);
                else {clients[slot].fd=fd;clients[slot].last=ticks();if(snapshot(fd))disconnect(&clients[slot]);}
            }
        }
        if(ticks()>=next){for(int i=0;i<CLIENTS;i++)if(clients[i].fd>=0 && snapshot(clients[i].fd))disconnect(&clients[i]);next=ticks()+2000;}
    }
    for(int i=0;i<CLIENTS;i++)disconnect(&clients[i]);
    rc=stopping?0:1;
done:
    if(v2_started)ubuntu_v2_stop();
    if(server>=0)close(server);
    if(bound){struct stat s;if(!lstat(argv[2],&s) && s.st_dev==owned.st_dev && s.st_ino==owned.st_ino)unlink(argv[2]);}
    close(lock);return rc;
}
