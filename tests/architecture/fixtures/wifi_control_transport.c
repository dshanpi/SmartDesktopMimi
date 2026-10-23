#include "system/wifi_control_protocol.h"
#include "middleware/middleware.h"
#include "ipc/ipc_socket.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 int sockets[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,sockets));
 uint8_t command[64],response[128];
 for(unsigned i=0;i<2;i++) {
  wifi_wire_command r={.action=WIFI_WIRE_SET,.client={1},.server={2},.sequence=11+i,.revision=i,.target=i};
  assert(!wifi_wire_encode(&r,command,64));
  assert(!ipc_client_send(sockets[0],TOPIC_WIFI_CONTROL_V2,command,64,IPC_FLAG_REQUEST));
 }
 for(unsigned i=0;i<2;i++) {
  ipc_msg_t message;assert(ipc_server_recv(sockets[1],&message,sizeof(message),false)>0);
  assert(message.header.topic==TOPIC_WIFI_CONTROL_V2 && message.header.payload_len==64);
  wifi_wire_command r;assert(!wifi_wire_decode(message.payload,message.header.payload_len,&r));
  assert(r.sequence==11+i && r.revision==i && r.target==i);
  /* Transport fixture receipt only; does not claim a real service admitted it. */
  wifi_response ack={.kind=WIFI_REPLY_ACK,.client={1},.server={2},.sequence=r.sequence,.revision=i+1,.ack_generation=i+1};
  assert(!wifi_wire_response_encode(&ack,response,128));
  assert(!ipc_server_send(sockets[1],TOPIC_WIFI_CONTROL_REPLY_V2,response,128,IPC_FLAG_RESPONSE));
 }
 for(unsigned i=0;i<2;i++) {
  ipc_msg_t message;assert(ipc_client_recv(sockets[0],&message,sizeof(message))>0);
  assert(message.header.topic==TOPIC_WIFI_CONTROL_REPLY_V2 && message.header.payload_len==128);
  wifi_response ack;assert(!wifi_wire_response_decode(message.payload,message.header.payload_len,&ack));
  assert(ack.kind==WIFI_REPLY_ACK && ack.sequence==11+i && !ack.phase && ack.ack_generation==i+1);
 }
 close(sockets[0]);close(sockets[1]);puts("PASS actual IPC codec transports two correlated commands/replies without changing V1 topic numbers");
}
