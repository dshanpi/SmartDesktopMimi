/* QUALITY076 optional WiFi control protocol. All offsets are little endian.
 * Commands: QUERY is read-only discovery/reconciliation; SET requires the
 * server epoch and expected revision from a fresh snapshot. Replayed SET bytes
 * must not be reinterpreted using a newer revision. Nonces identify sessions;
 * they are not authentication credentials.
 * Replies: ACK records queue admission only, never hardware completion.
 * SNAPSHOT is authoritative for readiness and correlated operation phase.
 * Consumers must match request client/sequence and current server epoch.
 * Save a preference only for this client's matching completed operation with
 * error=0, busy=0 and actual enabled matching its requested target.
 * Old providers ignore appended topics: UI must show unsupported/unavailable,
 * never silently treat V1 enabled snapshots as completion of a V2 request.
 * C structs below are host representations; only codecs define the wire ABI.
 */
#ifndef MIMI_WIFI_CONTROL_PROTOCOL_H
#define MIMI_WIFI_CONTROL_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define WIFI_WIRE_COMMAND_SIZE 64u
#define WIFI_WIRE_QUERY 1u
#define WIFI_WIRE_SET 2u
/* Host representation only. Never transmit this struct or its padding. */
typedef struct {
    uint32_t action;
    uint8_t client[16], server[16];
    uint64_t sequence, revision;
    uint32_t target;
} wifi_wire_command;
static inline uint32_t wifi_wire_get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static inline void wifi_wire_put32(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
static inline int wifi_wire_nonzero(const uint8_t *p) {
    uint8_t v=0; for(unsigned i=0;i<16;i++) v|=p[i]; return v!=0;
}
static inline int wifi_wire_valid(const wifi_wire_command *c) {
    if (!c || !wifi_wire_nonzero(c->client) || !c->sequence) return 0;
    if(c->action==WIFI_WIRE_QUERY) return !wifi_wire_nonzero(c->server) && c->target==0 && c->revision==0;
    return c->action==WIFI_WIRE_SET && wifi_wire_nonzero(c->server) && c->target<=1;
}
static inline int wifi_wire_decode(const void *data,size_t len,wifi_wire_command *out) {
    if(!data || !out || len!=WIFI_WIRE_COMMAND_SIZE) return -1;
    const uint8_t *p=data;
    if(wifi_wire_get32(p)!=2 || wifi_wire_get32(p+52)!=0) return -1;
    wifi_wire_command c={0};
    c.action=wifi_wire_get32(p+4);
    memcpy(c.client,p+8,16); memcpy(c.server,p+24,16);
    c.sequence=(uint64_t)wifi_wire_get32(p+40)|((uint64_t)wifi_wire_get32(p+44)<<32);
    c.target=wifi_wire_get32(p+48);
    c.revision=(uint64_t)wifi_wire_get32(p+56)|((uint64_t)wifi_wire_get32(p+60)<<32);
    if(!wifi_wire_valid(&c)) return -1;
    *out=c; return 0;
}
static inline int wifi_wire_encode(const wifi_wire_command *c,void *data,size_t len) {
    if(!data || len!=WIFI_WIRE_COMMAND_SIZE || !wifi_wire_valid(c)) return -1;
    uint8_t p[WIFI_WIRE_COMMAND_SIZE]={0};
    wifi_wire_put32(p,2); wifi_wire_put32(p+4,c->action);
    memcpy(p+8,c->client,16); memcpy(p+24,c->server,16);
    wifi_wire_put32(p+40,(uint32_t)c->sequence);
    wifi_wire_put32(p+44,(uint32_t)(c->sequence>>32));
    wifi_wire_put32(p+48,c->target);
    wifi_wire_put32(p+56,(uint32_t)c->revision);
    wifi_wire_put32(p+60,(uint32_t)(c->revision>>32)); memcpy(data,p,sizeof(p));return 0;
}
#define WIFI_REPLY_SIZE 128u
#define WIFI_REPLY_ACK 1u
#define WIFI_REPLY_SNAPSHOT 2u
typedef struct {
 uint32_t kind; uint8_t client[16],server[16];
 uint64_t sequence,revision,ack_generation; int32_t ack_error;
 uint32_t ready,enabled,busy; uint8_t owner[16];
 uint64_t operation_sequence,generation; uint32_t phase,target; int32_t error;
} wifi_response;
static inline uint64_t wifi_wire_get64(const uint8_t *p) {
 return (uint64_t)wifi_wire_get32(p)|((uint64_t)wifi_wire_get32(p+4)<<32);
}
static inline void wifi_wire_put64(uint8_t *p,uint64_t v) {wifi_wire_put32(p,(uint32_t)v);wifi_wire_put32(p+4,(uint32_t)(v>>32));}
static inline int32_t wifi_wire_get_signed(const uint8_t *p) {
 uint32_t v=wifi_wire_get32(p);return v<=INT32_MAX ? (int32_t)v:-1-(int32_t)(UINT32_MAX-v);
}
static inline int wifi_wire_response_valid(const wifi_response *r) {
 if(!r || !wifi_wire_nonzero(r->client) || !r->sequence)return 0;
 int has_owner=wifi_wire_nonzero(r->owner);
 if(r->kind==WIFI_REPLY_ACK) {
  if(r->ready || r->enabled || r->busy || has_owner || r->operation_sequence || r->generation || r->phase || r->target || r->error || r->ack_error>0)return 0;
  return r->ack_error ? r->ack_generation==0 : r->ack_generation!=0 && r->revision!=0 && wifi_wire_nonzero(r->server);
 }
 if(r->kind!=WIFI_REPLY_SNAPSHOT || r->ack_generation || r->ack_error || r->ready>1 || r->enabled>1 || r->busy>1 || r->phase>2 || r->target>1 || r->error>0)return 0;
 if(r->ready && !wifi_wire_nonzero(r->server))return 0;
 if(!r->phase)return !has_owner && !r->operation_sequence && !r->generation && !r->target && !r->error;
 if(!r->ready || !has_owner || !r->operation_sequence || !r->generation)return 0;
 return r->phase==1 ? r->busy && !r->error : !r->busy;
}
static inline int wifi_wire_response_encode(const wifi_response *r,void *data,size_t n) {
 if(!data || n!=WIFI_REPLY_SIZE || !wifi_wire_response_valid(r))return -1;
 uint8_t p[WIFI_REPLY_SIZE]={0};wifi_wire_put32(p,2);wifi_wire_put32(p+4,r->kind);
 memcpy(p+8,r->client,16);memcpy(p+24,r->server,16);wifi_wire_put64(p+40,r->sequence);wifi_wire_put64(p+48,r->revision);wifi_wire_put64(p+56,r->ack_generation);
 wifi_wire_put32(p+64,(uint32_t)r->ack_error);wifi_wire_put32(p+68,r->ready);wifi_wire_put32(p+72,r->enabled);wifi_wire_put32(p+76,r->busy);
 memcpy(p+80,r->owner,16);wifi_wire_put64(p+96,r->operation_sequence);wifi_wire_put64(p+104,r->generation);
 wifi_wire_put32(p+112,r->phase);wifi_wire_put32(p+116,r->target);wifi_wire_put32(p+120,(uint32_t)r->error);
 memcpy(data,p,sizeof(p));return 0;
}
static inline int wifi_wire_response_decode(const void *data,size_t n,wifi_response *out) {
 if(!data || !out || n!=WIFI_REPLY_SIZE)return -1;
 const uint8_t *p=data;if(wifi_wire_get32(p)!=2 || wifi_wire_get32(p+124))return -1;
 wifi_response r={0};r.kind=wifi_wire_get32(p+4);memcpy(r.client,p+8,16);memcpy(r.server,p+24,16);
 r.sequence=wifi_wire_get64(p+40);r.revision=wifi_wire_get64(p+48);r.ack_generation=wifi_wire_get64(p+56);r.ack_error=wifi_wire_get_signed(p+64);
 r.ready=wifi_wire_get32(p+68);r.enabled=wifi_wire_get32(p+72);r.busy=wifi_wire_get32(p+76);memcpy(r.owner,p+80,16);
 r.operation_sequence=wifi_wire_get64(p+96);r.generation=wifi_wire_get64(p+104);r.phase=wifi_wire_get32(p+112);r.target=wifi_wire_get32(p+116);r.error=wifi_wire_get_signed(p+120);
 if(!wifi_wire_response_valid(&r))return -1;
 *out=r;return 0;
}

#endif
