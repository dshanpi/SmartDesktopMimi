#ifndef MIMI_CLOUD_PAIRING_STATUS_H
#define MIMI_CLOUD_PAIRING_STATUS_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Optional topic appended after all frozen V1 topics. No account or secret. */
#define CLOUD_PAIRING_STATUS_VERSION 1U
#define CLOUD_PAIRING_UNKNOWN 0U
#define CLOUD_PAIRING_UNBOUND 1U
#define CLOUD_PAIRING_BOUND 2U
typedef struct {
    uint32_t version;
    uint32_t bytes;
    uint32_t ownership;
    int32_t error;
    uint32_t refreshing;
    uint32_t expires_in;
    char code[8];
} cloud_pairing_status_v1_t;

static inline bool cloud_pairing_code_valid(const char *s)
{
    if (!s) return false;
    for (unsigned i=0;i<6;i++) if (s[i]<'0'||s[i]>'9') return false;
    return s[6]=='\0';
}
static inline bool cloud_pairing_status_valid(const cloud_pairing_status_v1_t *s)
{
    if (s->version!=1 || s->bytes!=sizeof(*s) || s->ownership>2 ||
        s->refreshing>1 || !memchr(s->code,0,sizeof(s->code)) || s->expires_in>600) return false;
    if (s->error || s->refreshing || s->ownership!=CLOUD_PAIRING_UNBOUND)
        return !s->code[0] && !s->expires_in;
    return (!s->code[0] && !s->expires_in) ||
           (s->expires_in && cloud_pairing_code_valid(s->code));
}

/* Same-code polls may shorten a deadline, never renew it. Only an explicit
 * refresh clears the previous grant. tick subtraction also handles wrap. */
typedef struct { uint32_t tick, seconds; char code[128]; } cloud_pairing_countdown_t;
static inline uint32_t cloud_pairing_remaining(const cloud_pairing_countdown_t *s, uint32_t tick)
{
    uint32_t elapsed=(tick-s->tick)/1000U;
    return elapsed<s->seconds ? s->seconds-elapsed : 0;
}
static inline void cloud_pairing_observe(cloud_pairing_countdown_t *s,
                                         const char *code,uint32_t ttl,uint32_t tick)
{
    if (!cloud_pairing_code_valid(code) || ttl>86400U) { memset(s,0,sizeof(*s)); return; }
    if (!strcmp(code,s->code)) {
        uint32_t old=cloud_pairing_remaining(s,tick);
        if (old<ttl) ttl=old;
    }
    memset(s->code,0,sizeof(s->code)); memcpy(s->code,code,6);
    s->tick=tick;s->seconds=ttl;
}
#endif
