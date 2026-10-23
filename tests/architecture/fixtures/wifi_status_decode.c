#include "system/wifi_status_decode.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 wifi_ap_info_t ap={.ssid="ok",.encrypted=true}, out={0}, before=out;
 wifi_runtime_status_t rt={0}, rout={0}, rbefore=rout;
 unsigned char a[sizeof(ap)+1], r[sizeof(rt)+1];
 memcpy(a+1,&ap,sizeof(ap));memcpy(r+1,&rt,sizeof(rt));
 assert(wifi_ap_decode(a+1,sizeof(ap),&out));
 assert(wifi_runtime_decode(r+1,sizeof(rt),&rout));
 for(size_t n=0;n<sizeof(ap);n++){out=before;assert(!wifi_ap_decode(a+1,n,&out));assert(!memcmp(&out,&before,sizeof(out)));}
 for(size_t n=0;n<sizeof(rt);n++)assert(!wifi_runtime_decode(r+1,n,&rout));
 size_t ab[]={offsetof(wifi_ap_info_t,encrypted),offsetof(wifi_ap_info_t,connected)};
 size_t rb[]={offsetof(wifi_runtime_status_t,enabled),offsetof(wifi_runtime_status_t,scanning),offsetof(wifi_runtime_status_t,connected)};
 for(unsigned v=2;v<256;v++){
  for(unsigned i=0;i<2;i++){memcpy(a+1,&ap,sizeof(ap));a[1+ab[i]]=v;out=before;assert(!wifi_ap_decode(a+1,sizeof(ap),&out));assert(!memcmp(&out,&before,sizeof(out)));}
  for(unsigned i=0;i<3;i++){memcpy(r+1,&rt,sizeof(rt));r[1+rb[i]]=v;rout=rbefore;assert(!wifi_runtime_decode(r+1,sizeof(rt),&rout));assert(!memcmp(&rout,&rbefore,sizeof(rout)));}
 }
 memset(ap.ssid,'a',33);assert(!wifi_ap_decode(&ap,sizeof(ap),&out));ap.ssid[32]=0;assert(wifi_ap_decode(&ap,sizeof(ap),&out));
 memset(rt.connected_ssid,'a',33);assert(!wifi_runtime_decode(&rt,sizeof(rt),&rout));rt.connected_ssid[32]=0;assert(wifi_runtime_decode(&rt,sizeof(rt),&rout));
 assert(!wifi_ap_decode(NULL,sizeof(ap),&out));assert(!wifi_runtime_decode(NULL,sizeof(rt),&rout));
 puts("PASS 1270 invalid booleans, truncation, unaligned and bounded names");
}
