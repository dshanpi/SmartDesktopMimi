#include "system/wifi_control_protocol.h"
#include <assert.h>
#include <stdio.h>
static unsigned rejected;
static void bad(const uint8_t *p,size_t n) {
    wifi_wire_command c,before;
    memset(&c,0xa5,sizeof(c));memcpy(&before,&c,sizeof(c));
    assert(wifi_wire_decode(p,n,&c)==-1);
    assert(!memcmp(&c,&before,sizeof(c)));rejected++;
}
static int command_checks(void) {
    const uint8_t golden[64]={2,0,0,0,2,0,0,0,
      1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
      17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
      8,7,6,5,4,3,2,1,1,0,0,0,0,0,0,0,
      0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11};
    uint8_t p[65],b[65];wifi_wire_command c;
    assert(wifi_wire_decode(golden,64,&c)==0);
    assert(c.action==2 && c.sequence==UINT64_C(0x0102030405060708) && c.target==1);
    assert(c.revision==UINT64_C(0x1122334455667788));
    assert(c.client[0]==1 && c.client[15]==16 && c.server[0]==17 && c.server[15]==32);
    assert(wifi_wire_encode(&c,p,64)==0 && !memcmp(golden,p,64));
    memcpy(b+1,golden,64);assert(wifi_wire_decode(b+1,64,&c)==0);
    for(size_t n=0;n<64;n++) bad(golden,n);
    memcpy(b,golden,64);b[64]=0;bad(b,65);bad(NULL,64);
    const unsigned offsets[]={0,4,48,52};
    for(unsigned k=0;k<4;k++) {
      memcpy(b,golden,64);wifi_wire_put32(b+offsets[k],UINT32_MAX);bad(b,64);
    }
    memcpy(b,golden,64);memset(b+8,0,16);bad(b,64);
    memcpy(b,golden,64);memset(b+24,0,16);bad(b,64);
    memcpy(b,golden,64);memset(b+40,0,8);bad(b,64);
    memcpy(b,golden,64);wifi_wire_put32(b+4,1);bad(b,64);
    memset(b+24,0,16);bad(b,64); /* Query target must also be zero. */
    wifi_wire_put32(b+48,0);bad(b,64);memset(b+56,0,8);assert(wifi_wire_decode(b,64,&c)==0);
    assert(wifi_wire_encode(&c,p,64)==0 && !memcmp(b,p,64));
    c.sequence=UINT64_MAX;assert(wifi_wire_encode(&c,p,64)==0);
    assert(wifi_wire_decode(p,64,&c)==0 && c.sequence==UINT64_MAX);
    c.sequence=0;memset(p,0xa5,sizeof(p));memcpy(b,p,sizeof(b));
    assert(wifi_wire_encode(&c,p,64)==-1 && !memcmp(p,b,sizeof(p)));
    assert(wifi_wire_decode(golden,64,NULL)==-1);
    printf("PASS golden bytes/query/max-sequence/unaligned decode; %u malformed messages rejected without output mutation\n",rejected);
 return 0;
}

#include "system/wifi_control_protocol.h"
#include <assert.h>
#include <stdio.h>
static unsigned rejects;
static void bad_reply(const void *p,size_t n) {
 wifi_response r,before;memset(&r,0xa5,sizeof(r));memcpy(&before,&r,sizeof(r));
 assert(wifi_wire_response_decode(p,n,&r)==-1 && !memcmp(&r,&before,sizeof(r)));rejects++;
}
static int response_checks(void) {
 const uint8_t golden[128]={[0]=2,[4]=1,[8]=1,[24]=2,[40]=3,[48]=4,[56]=5};
 uint8_t out[129];wifi_response r;
 assert(!wifi_wire_response_decode(golden,128,&r));
 assert(r.kind==1 && r.client[0]==1 && r.server[0]==2 && r.sequence==3 && r.revision==4 && r.ack_generation==5);
 assert(!wifi_wire_response_encode(&r,out,128) && !memcmp(out,golden,128));
 memcpy(out+1,golden,128);assert(!wifi_wire_response_decode(out+1,128,&r));
 for(size_t n=0;n<128;n++)bad_reply(golden,n);
 memcpy(out,golden,128);bad_reply(out,129);bad_reply(NULL,128);
 unsigned offsets[]={0,4,64,68,72,76,112,116,120,124};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){memcpy(out,golden,128);wifi_wire_put32(out+offsets[i],3);bad_reply(out,128);}
 memcpy(out,golden,128);memset(out+8,0,16);bad_reply(out,128);
 memcpy(out,golden,128);memset(out+40,0,8);bad_reply(out,128);
 r=(wifi_response){.kind=2,.client={1},.server={2},.sequence=7,.revision=8,.ready=1,.enabled=1,.busy=1,.owner={3},.operation_sequence=4,.generation=5,.phase=1,.target=1};
 assert(!wifi_wire_response_encode(&r,out,128));assert(!wifi_wire_response_decode(out,128,&r));
 r.phase=2;r.busy=0;r.error=INT32_MIN;
 assert(!wifi_wire_response_encode(&r,out,128));assert(!wifi_wire_response_decode(out,128,&r) && r.error==INT32_MIN);
 out[68]=0;bad_reply(out,128);out[68]=1;out[76]=1;bad_reply(out,128);
 puts("PASS fixed ACK golden bytes, unaligned, pending/completed snapshot, signed error extremes");
 printf("PASS %u malformed replies rejected without output mutation\n",rejects);
 return 0;
}

#include "middleware/middleware.h"
#include "system/backend_types.h"
_Static_assert(TOPIC_SYSTEM_STATUS==0,"frozen V1 topic");
_Static_assert(TOPIC_WIFI_COMMAND==1,"frozen V1 topic");
_Static_assert(TOPIC_WIFI_STATUS==2,"frozen V1 topic");
_Static_assert(TOPIC_WIFI_RUNTIME==3,"frozen V1 topic");
_Static_assert(TOPIC_SENSOR_STATUS==4,"frozen V1 topic");
_Static_assert(TOPIC_NETWORK_INFO==5,"frozen V1 topic");
_Static_assert(TOPIC_VIDEO_CONTROL==6,"frozen V1 topic");
_Static_assert(TOPIC_VIDEO_STATUS==7,"frozen V1 topic");
_Static_assert(TOPIC_VIDEO_PLAYLIST==8,"frozen V1 topic");
_Static_assert(TOPIC_BT_COMMAND==9,"frozen V1 topic");
_Static_assert(TOPIC_BT_RUNTIME==10,"frozen V1 topic");
_Static_assert(TOPIC_BT_AVRCP_INFO==11,"frozen V1 topic");
_Static_assert(TOPIC_AI_COMMAND==12,"frozen V1 topic");
_Static_assert(TOPIC_AI_STATUS==13,"frozen V1 topic");
_Static_assert(TOPIC_AI_TEXT==14,"frozen V1 topic");
_Static_assert(TOPIC_HDMI_COMMAND==15,"frozen V1 topic");
_Static_assert(TOPIC_HDMI_PREVIEW_STATUS==16,"frozen V1 topic");
_Static_assert(TOPIC_OTA_COMMAND==17,"frozen V1 topic");
_Static_assert(TOPIC_OTA_STATUS==18,"frozen V1 topic");
_Static_assert(TOPIC_CLOUD_COMMAND==19,"frozen V1 topic");
_Static_assert(TOPIC_CLOUD_STATUS==20,"frozen V1 topic");
_Static_assert(TOPIC_LED_COMMAND==21,"frozen V1 topic");
_Static_assert(TOPIC_CLOUD_PAIRING_STATUS==22,"frozen V1 topic");
_Static_assert(TOPIC_WIFI_CONTROL_V2==23 && TOPIC_WIFI_CONTROL_REPLY_V2==24,"appended topics");
_Static_assert(sizeof(wifi_runtime_status_t)==40,"unchanged supported V1 layout");
int main(void){command_checks();response_checks();return 0;}
