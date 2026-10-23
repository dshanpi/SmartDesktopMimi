#ifndef MIMI_WIFI_CONTROL_CLIENT_H
#define MIMI_WIFI_CONTROL_CLIENT_H
#include "wifi_control_protocol.h"
/* UI-thread owned. No I/O or preference writes; callers render view and act on
 * WIFI_CLIENT_SAVE exactly once. Transport errors never prove non-execution. */
typedef enum {
 WIFI_CLIENT_CHECKING, WIFI_CLIENT_READY, WIFI_CLIENT_PENDING,
 WIFI_CLIENT_UNCONFIRMED, WIFI_CLIENT_REJECTED, WIFI_CLIENT_FAILED,
 WIFI_CLIENT_CHANGED, WIFI_CLIENT_UNAVAILABLE
} wifi_client_view;
typedef struct {
 uint8_t client[16], server[16], operation_server[16];
 uint64_t counter, query_sequence, operation_sequence, revision, operation_revision, generation;
 uint32_t observed_at, started_at, opened_at;
 uint32_t enabled, busy, ready, target;
 int known, pending;
 wifi_client_view view;
} wifi_control_client;
#define WIFI_CLIENT_SAVE 1
static inline void wifi_client_open(wifi_control_client *c,uint32_t now) {
 c->known=0;c->query_sequence=0;c->opened_at=now;
 c->view=c->pending ? WIFI_CLIENT_UNCONFIRMED:WIFI_CLIENT_CHECKING;
}
static inline int wifi_client_next(wifi_control_client *c,uint64_t *next) {
 if(c->counter==UINT64_MAX || !wifi_wire_nonzero(c->client)) {c->view=WIFI_CLIENT_UNAVAILABLE;return -1;}
 *next=++c->counter;return 0;
}
static inline int wifi_client_query(wifi_control_client *c,wifi_wire_command *out) {
 wifi_wire_command r={.action=WIFI_WIRE_QUERY};
 if(!out || wifi_client_next(c,&r.sequence))return -1;
 memcpy(r.client,c->client,16);c->query_sequence=r.sequence;*out=r;return 0;
}
static inline int wifi_client_can_set(const wifi_control_client *c,uint32_t now) {
 return c->known && c->ready && !c->busy && !c->pending &&
        (uint32_t)(now-c->observed_at)<=3000 && c->counter!=UINT64_MAX;
}
static inline int wifi_client_set(wifi_control_client *c,uint32_t target,uint32_t now,wifi_wire_command *out) {
 if(!out || target>1 || !wifi_client_can_set(c,now))return -1;
 wifi_wire_command r={.action=WIFI_WIRE_SET,.revision=c->revision,.target=target};
 if(wifi_client_next(c,&r.sequence))return -1;
 memcpy(r.client,c->client,16);memcpy(r.server,c->server,16);
 c->operation_sequence=r.sequence;c->operation_revision=c->revision;
 memcpy(c->operation_server,c->server,16);c->target=target;c->generation=0;
 c->pending=1;c->started_at=now;c->query_sequence=0;c->view=WIFI_CLIENT_PENDING;
 *out=r;return 0;
}
static inline void wifi_client_tick(wifi_control_client *c,uint32_t now) {
 if(c->pending && (uint32_t)(now-c->started_at)>=10000)c->view=WIFI_CLIENT_UNCONFIRMED;
 if(c->known && (uint32_t)(now-c->observed_at)>3000) {
  c->known=0;c->view=c->pending ? WIFI_CLIENT_UNCONFIRMED:WIFI_CLIENT_UNAVAILABLE;
 }
 if(!c->known && !c->pending && (uint32_t)(now-c->opened_at)>=5000)c->view=WIFI_CLIENT_UNAVAILABLE;
}
static inline int wifi_client_receive(wifi_control_client *c,const wifi_response *r,uint32_t now) {
 if(!wifi_wire_response_valid(r) || memcmp(c->client,r->client,16))return 0;
 if(r->kind==WIFI_REPLY_ACK) {
  if(!c->pending || r->sequence!=c->operation_sequence)return 0;
  if(memcmp(r->server,c->operation_server,16)) {c->known=0;c->view=WIFI_CLIENT_UNCONFIRMED;return 0;}
  if(r->ack_error) {c->pending=0;c->known=0;c->view=WIFI_CLIENT_REJECTED;}
  else c->generation=r->ack_generation;
  return 0;
 }
 if(!c->query_sequence || r->sequence!=c->query_sequence)return 0;
 c->query_sequence=0;c->observed_at=now;c->known=1;c->ready=r->ready;
 c->enabled=r->enabled;c->busy=r->busy;c->revision=r->revision;memcpy(c->server,r->server,16);
 if(!r->ready) {c->view=c->pending ? WIFI_CLIENT_UNCONFIRMED:WIFI_CLIENT_UNAVAILABLE;return 0;}
 if(!c->pending) {c->view=r->busy ? WIFI_CLIENT_PENDING:WIFI_CLIENT_READY;return 0;}
 if(memcmp(c->operation_server,r->server,16)) {c->pending=0;c->view=WIFI_CLIENT_CHANGED;return 0;}
 int ours=r->phase && r->operation_sequence==c->operation_sequence && !memcmp(r->owner,c->client,16);
 if(ours) {
  if(r->target!=c->target || (c->generation && r->generation!=c->generation)) {
   c->known=0;c->view=WIFI_CLIENT_UNCONFIRMED;return 0;
  }
  if(r->phase==1) {c->view=(uint32_t)(now-c->started_at)>=10000 ? WIFI_CLIENT_UNCONFIRMED:WIFI_CLIENT_PENDING;return 0;}
  c->pending=0;
  if(!r->error && !r->busy && r->enabled==c->target) {c->view=WIFI_CLIENT_READY;return WIFI_CLIENT_SAVE;}
  c->view=WIFI_CLIENT_FAILED;return 0;
 }
 /* A later authoritative attempt displaced this operation. Its outcome is
  * unknown, not success; a new explicit user action may use the fresh revision. */
 if(r->revision>c->operation_revision) {c->pending=0;c->view=WIFI_CLIENT_CHANGED;}
 else c->view=WIFI_CLIENT_UNCONFIRMED;
 return 0;
}
#endif
