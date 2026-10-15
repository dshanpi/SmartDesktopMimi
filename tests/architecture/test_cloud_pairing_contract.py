import subprocess
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
class CloudPairingContractTests(unittest.TestCase):
    def test_frozen_abi_and_nonrenewing_countdown(self):
        code=r"""
#include "system/backend_types.h"
#include "system/cloud_pairing_status.h"
#include "middleware/middleware.h"
#include <stddef.h>
#include <assert.h>
_Static_assert(sizeof(cloud_status_t)==276,"frozen V1 status");
_Static_assert(sizeof(cloud_cmd_t)==4,"frozen V1 command");
_Static_assert(offsetof(cloud_status_t,bind_token)==80,"frozen token offset");
_Static_assert(offsetof(cloud_status_t,bind_token_error)==72,"frozen error offset");
_Static_assert(TOPIC_CLOUD_COMMAND==19 && TOPIC_CLOUD_STATUS==20 && TOPIC_LED_COMMAND==21,"frozen topic IDs");
_Static_assert(CLOUD_CMD_GET_STATUS==0 && CLOUD_CMD_REFRESH_BIND_TOKEN==1 && CLOUD_CMD_DISMISS_OTA==2,"frozen command IDs");
_Static_assert(TOPIC_CLOUD_PAIRING_STATUS==22,"append only extension");
_Static_assert(sizeof(cloud_pairing_status_v1_t)==32,"extension layout");
int main(void){
 cloud_pairing_countdown_t s={0};
 cloud_pairing_observe(&s,"123456",60,1000);
 cloud_pairing_observe(&s,"123456",60,11000);
 assert(cloud_pairing_remaining(&s,21000)==40);
 cloud_pairing_observe(&s,"123456",60,81000);
 assert(cloud_pairing_remaining(&s,81000)==0);
 memset(&s,0,sizeof(s));
 cloud_pairing_observe(&s,"123456",60,UINT32_MAX-5000);
 assert(cloud_pairing_remaining(&s,4999)==50);
 cloud_pairing_status_v1_t p={.version=1,.bytes=sizeof(p),.ownership=CLOUD_PAIRING_BOUND};
 assert(cloud_pairing_status_valid(&p));
 strcpy(p.code,"123456");p.expires_in=60;
 assert(!cloud_pairing_status_valid(&p));
 p.ownership=CLOUD_PAIRING_UNBOUND;assert(cloud_pairing_status_valid(&p));
 p.version=99;assert(!cloud_pairing_status_valid(&p));
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as d:
            source=Path(d)/'contract.c';source.write_text(code)
            binary=Path(d)/'contract'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT/'apps/lv_port_linux/src'),str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
