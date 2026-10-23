#include "system/wifi_control_client.h"
#include <assert.h>
#include <stdio.h>
static wifi_response snapshot(wifi_control_client *c,uint32_t now) {
 wifi_wire_command q;assert(!wifi_client_query(c,&q));
 wifi_response r={.kind=WIFI_REPLY_SNAPSHOT,.client={1},.server={2},.sequence=q.sequence,.ready=1};
 (void)now;return r;
}
static void ready(wifi_control_client *c,uint32_t now) {
 *c=(wifi_control_client){.client={1}};wifi_client_open(c,now);
 wifi_response r=snapshot(c,now);assert(!wifi_client_receive(c,&r,now));assert(wifi_client_can_set(c,now));
}
int main(void) {
 wifi_control_client c;ready(&c,100);wifi_wire_command set;
 wifi_response old=snapshot(&c,100);assert(!wifi_client_set(&c,1,100,&set));
 assert(!wifi_client_receive(&c,&old,101) && c.pending);
 wifi_response ack={.kind=WIFI_REPLY_ACK,.client={1},.server={2},.sequence=set.sequence,.revision=1,.ack_generation=1};
 assert(!wifi_client_receive(&c,&ack,102) && c.pending);
 wifi_response r=snapshot(&c,103);r.revision=1;r.enabled=1;r.busy=1;r.phase=1;r.target=1;
 r.owner[0]=1;r.operation_sequence=set.sequence;r.generation=1;
 assert(!wifi_client_receive(&c,&r,103) && c.pending);
 wifi_client_open(&c,104);assert(c.pending && !c.known); /* close/reopen retains operation */
 wifi_response stale=r;r=snapshot(&c,105);r.revision=1;r.enabled=1;r.phase=2;r.target=1;
 r.owner[0]=1;r.operation_sequence=set.sequence;r.generation=1;
 assert(!wifi_client_receive(&c,&stale,105) && c.pending);
 assert(wifi_client_receive(&c,&r,106)==WIFI_CLIENT_SAVE && !c.pending);
 assert(!wifi_client_receive(&c,&r,107)); /* exactly one save */
 ready(&c,0);assert(!wifi_client_set(&c,1,0,&set));wifi_client_tick(&c,30000);
 assert(c.pending && c.view==WIFI_CLIENT_UNCONFIRMED);
 r=snapshot(&c,30001);r.revision=0;assert(!wifi_client_receive(&c,&r,30001) && c.pending);
 r=snapshot(&c,30002);r.server[0]=3;assert(!wifi_client_receive(&c,&r,30002) && !c.pending && c.view==WIFI_CLIENT_CHANGED);
 ready(&c,0);assert(!wifi_client_set(&c,1,0,&set));
 ack.sequence=set.sequence;ack.ack_error=-12;ack.ack_generation=0;
 assert(!wifi_client_receive(&c,&ack,1) && !c.pending && !c.known && c.view==WIFI_CLIENT_REJECTED);
 ready(&c,0);assert(!wifi_client_set(&c,1,0,&set));
 r=snapshot(&c,1);r.revision=1;r.phase=2;r.target=1;r.owner[0]=1;r.operation_sequence=set.sequence;r.generation=1;r.error=-2001;
 assert(!wifi_client_receive(&c,&r,1) && !c.pending && c.view==WIFI_CLIENT_FAILED);
 ready(&c,0);assert(!wifi_client_set(&c,1,0,&set));
 r=snapshot(&c,1);r.client[0]=9;assert(!wifi_client_receive(&c,&r,1) && c.pending);
 r.client[0]=1;r.revision=2;r.phase=1;r.busy=1;r.owner[0]=8;r.operation_sequence=22;r.generation=2;
 assert(!wifi_client_receive(&c,&r,1) && !c.pending && c.view==WIFI_CLIENT_CHANGED && !wifi_client_can_set(&c,1));
 ready(&c,UINT32_MAX-100);assert(!wifi_client_set(&c,1,UINT32_MAX-100,&set));wifi_client_tick(&c,10000);
 assert(c.view==WIFI_CLIENT_UNCONFIRMED && c.pending);
 ready(&c,0);c.counter=UINT64_MAX;assert(wifi_client_set(&c,1,1,&set)==-1);
 wifi_client_open(&c,0);wifi_client_tick(&c,5000);assert(c.view==WIFI_CLIENT_UNAVAILABLE);
 puts("PASS UI controller: no ACK save, matched completion once, stale query/client/epoch, pending reopen, no-reply uncertainty, rejection/failure/supersession, freshness and counter/tick boundaries");
}
