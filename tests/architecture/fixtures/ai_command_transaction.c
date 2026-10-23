#include <assert.h>
#include <stdio.h>
#include "integrations/tuyaopen/protocol/ai_command_transaction.h"
int main(void) {
 mimi_ai_transaction tx={0};
 const char *bad[]={NULL,"","quote\"","a b","a/b","a.b","a\n"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(!mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,bad[i],0,1));
 char id[66]; memset(id,'x',65); id[65]=0; assert(!mimi_ai_id_valid(id)); id[64]=0; assert(mimi_ai_id_valid(id));
 assert(!mimi_ai_tx_begin(NULL,MIMI_AI_EXIT,"a",0,1));
 assert(!mimi_ai_tx_begin(&tx,(mimi_ai_command)7,"a",0,1));
 assert(!mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"a",-1,1));
 assert(!mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"a",0,0));
 assert(!mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"a",INT64_MAX,1));
 assert(mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"boot-1",100,30));
 assert(!mimi_ai_tx_begin(&tx,MIMI_AI_ENTER,"boot-2",101,30));
 assert(mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"boot-1",0,129));
 assert(!mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"boot-1",130,30));
 assert(mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"boot-2",130,30));
 assert(!mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"boot-2",0,160));
 assert(tx.state==MIMI_AI_TX_FAILED && tx.error==-ETIMEDOUT);
 assert(!mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"boot-2",0,161));
 assert(mimi_ai_tx_begin(&tx,MIMI_AI_ENTER,"boot-3",170,30));
 assert(!mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"boot-2",0,171));
 assert(mimi_ai_tx_accept(&tx,MIMI_AI_ENTER,"boot-3",-7,172));
 assert(tx.state==MIMI_AI_TX_FAILED && tx.error==-7);
 for(unsigned sequence=0;sequence<46656;sequence++) {
  tx=(mimi_ai_transaction){0};
  assert(mimi_ai_tx_begin(&tx,MIMI_AI_EXIT,"sequence",100,30));
  unsigned events=sequence;
  for(unsigned step=0;step<6;step++) {
   unsigned event=events%6; events/=6;
   mimi_ai_tx_state before=tx.state; int error=tx.error;
   switch(event) {
    case 0: mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"sequence",0,101+step); break;
    case 1: mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"sequence",-7,101+step); break;
    case 2: mimi_ai_tx_accept(&tx,MIMI_AI_EXIT,"stale",0,101+step); break;
    case 3: mimi_ai_tx_timeout(&tx,130); break;
    case 4: mimi_ai_tx_accept(&tx,MIMI_AI_ENTER,"sequence",0,101+step); break;
    case 5: mimi_ai_tx_send_failed(&tx,-EIO); break;
   }
   if(before!=MIMI_AI_TX_PENDING) assert(tx.state==before && tx.error==error);
   else if(event==0) assert(tx.state==MIMI_AI_TX_SUCCEEDED && tx.error==0);
   else if(event==2 || event==4) assert(tx.state==MIMI_AI_TX_PENDING);
   else assert(tx.state==MIMI_AI_TX_FAILED && tx.error!=0);
  }
 }
 puts("PASS: boundary/retry cases and 46656 ordered event sequences");
 return 0;
}
