"""Real Ubuntu provider process tests. No socket sink or cloud/hardware success mocks."""
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]

class UbuntuProviderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='mimi-provider-build-')
        b=Path(cls.build.name)
        try:
            subprocess.run(['cmake','-S',str(ROOT/'services/mimi-ubuntu-provider'),'-B',str(b/'build'),'-DCMAKE_BUILD_TYPE=Release'],check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            subprocess.run(['cmake','--build',str(b/'build'),'-j4'],check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            cls.binary=b/'build/mimi-ubuntu-provider'
            # Read native frozen C sizes/offsets and enum values, not a guessed
            # ctypes copy that could silently drift from the actual ABI.
            types={'wifi':'wifi_cmd_t','cloud':'cloud_cmd_t','ai':'ai_cmd_t','hdmi':'hdmi_cmd_t','bt':'bt_cmd_t','ota':'ota_cmd_t','video':'video_cmd_t','led':'led_cmd_t'}
            constants=['TOPIC_'+x for x in ['NETWORK_INFO','CLOUD_STATUS','WIFI_RUNTIME','AI_STATUS','HDMI_PREVIEW_STATUS','VIDEO_STATUS','BT_RUNTIME','OTA_STATUS','SENSOR_STATUS','CLOUD_COMMAND','WIFI_COMMAND','AI_COMMAND','HDMI_COMMAND','BT_COMMAND','OTA_COMMAND','VIDEO_CONTROL','LED_COMMAND','VIDEO_PLAYLIST']]
            lines=['#include <stdio.h>','#include <stddef.h>','#include "middleware/middleware.h"','#include "system/backend_types.h"','int main(void){']
            for k,v in types.items():lines.append('printf("%s %%zu\\n", sizeof(%s));'%(k,v))
            for k in constants:lines.append('printf("%s %%d\\n", %s);'%(k,k))
            for k,t,f in [('cloud_error','cloud_status_t','bind_token_error'),('wifi_error','wifi_runtime_status_t','last_error_code'),('ai_error','ai_status_t','last_error_code'),('hdmi_error','hdmi_preview_status_t','exit_code'),('ota_error','ota_status_t','error_code')]:
                lines.append('printf("%s %%zu\\n", offsetof(%s,%s));'%(k,t,f))
            lines+=['return 0;}'];(b/'abi.c').write_text('\n'.join(lines))
            subprocess.run(['gcc','-I'+str(ROOT/'apps/lv_port_linux/src'),str(b/'abi.c'),'-o',str(b/'abi')],check=True)
            cls.abi={k:int(v) for k,v in (s.split() for s in subprocess.check_output([str(b/'abi')],text=True).splitlines())}
        except Exception:cls.build.cleanup();raise
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='mimi-provider-');self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.endpoint=self.root/'ipc.sock';self.log=(self.root/'provider.log').open('w+');self.addCleanup(self.log.close)
        self.p=None;self.addCleanup(self.stop)
    def stop(self):
        if self.p and self.p.poll() is None:
            self.p.terminate();self.p.wait(timeout=3)
    def start(self):
        offset=(self.root/'provider.log').stat().st_size
        self.p=subprocess.Popen([str(self.binary),'--socket',str(self.endpoint)],stdout=self.log,stderr=subprocess.STDOUT)
        for _ in range(100):
            if self.endpoint.exists() and 'Ubuntu session provider ready' in (self.root/'provider.log').read_text()[offset:]:return
            if self.p.poll() is not None:self.fail('Provider exited early')
            time.sleep(.02)
        self.fail('Provider did not start')
    def connect(self):
        s=socket.socket(socket.AF_UNIX);s.settimeout(3);self.addCleanup(s.close);s.connect(str(self.endpoint))
        frames=[self.read(s) for _ in range(9)];return s,{t:p for t,p,f in frames}
    @staticmethod
    def exact(s,n):
        result=b''
        while len(result)<n:
            part=s.recv(n-len(result))
            if not part:raise EOFError()
            result+=part
        return result
    def read(self,s):
        magic,t,n,f=struct.unpack('<IIII',self.exact(s,16));self.assertEqual(magic,0x4c56474c);self.assertLessEqual(n,512)
        return t,self.exact(s,n),f
    def frame(self,name,action=0):
        topic=self.abi['TOPIC_'+{'cloud':'CLOUD_COMMAND','wifi':'WIFI_COMMAND','ai':'AI_COMMAND','hdmi':'HDMI_COMMAND','bt':'BT_COMMAND','ota':'OTA_COMMAND','video':'VIDEO_CONTROL','led':'LED_COMMAND'}[name]]
        return struct.pack('<IIII',0x4c56474c,topic,self.abi[name],0)+struct.pack('<i',action)+bytes(self.abi[name]-4)
    def refused(self):
        p=subprocess.run([str(self.binary),'--socket',str(self.endpoint)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=3)
        self.assertNotEqual(p.returncode,0);return p
    def test_real_network_and_unavailable_snapshot(self):
        self.start();s,frames=self.connect()
        n=frames[self.abi['TOPIC_NETWORK_INFO']]
        if n[0]:
            ip=n[1:17].split(b'\0')[0].decode();iface=n[17:33].split(b'\0')[0].decode()
            actual=json.loads(subprocess.check_output(['ip','-j','-4','address','show','dev',iface],text=True))
            self.assertIn(ip,[x['local'] for i in actual for x in i['addr_info']])
        cloud=frames[self.abi['TOPIC_CLOUD_STATUS']];self.assertEqual(struct.unpack_from('<i',cloud)[0],3)
        for k,topic in [('cloud','CLOUD_STATUS'),('wifi','WIFI_RUNTIME'),('ai','AI_STATUS'),('hdmi','HDMI_PREVIEW_STATUS'),('ota','OTA_STATUS')]:
            self.assertEqual(struct.unpack_from('<i',frames[self.abi['TOPIC_'+topic]],self.abi[k+'_error'])[0],-95)
        self.assertEqual(frames[self.abi['TOPIC_SENSOR_STATUS']][0],0)
        self.assertEqual(os.stat(self.endpoint).st_mode&0o777,0o600)
    def test_fragmented_and_coalesced_requests(self):
        self.start();s,_=self.connect();f=self.frame('cloud')
        for chunk in (f[:3],f[3:14],f[14:17],f[17:]):s.sendall(chunk);time.sleep(.01)
        self.assertEqual(self.read(s)[0],self.abi['TOPIC_CLOUD_STATUS'])
        s.sendall(self.frame('ai')+self.frame('hdmi'))
        self.assertEqual([self.read(s)[0],self.read(s)[0]],[self.abi['TOPIC_AI_STATUS'],self.abi['TOPIC_HDMI_PREVIEW_STATUS']])
    def test_unsupported_mutations_never_succeed_or_log_payload(self):
        self.start();s,_=self.connect()
        s.sendall(self.frame('wifi',1)[:-10]+b'SECRETXYZ!')
        t,p,f=self.read(s);self.assertEqual(t,self.abi['TOPIC_WIFI_RUNTIME']);self.assertEqual(struct.unpack_from('<i',p,self.abi['wifi_error'])[0],-95)
        s.sendall(self.frame('cloud',1));self.assertEqual(self.read(s)[0],self.abi['TOPIC_CLOUD_STATUS'])
        self.stop();self.log.flush();self.assertNotIn('SECRETXYZ!',(self.root/'provider.log').read_text())
    def test_malformed_client_isolated(self):
        self.start();bad,_=self.connect();good,_=self.connect()
        bad.sendall(struct.pack('<IIII',0,0,0,0));self.assertEqual(bad.recv(1),b'')
        good.sendall(self.frame('cloud'));self.assertEqual(self.read(good)[0],self.abi['TOPIC_CLOUD_STATUS']);self.assertIsNone(self.p.poll())
    def test_oversize_wrong_shape_and_flags_rejected(self):
        self.start()
        for header,payload in [(struct.pack('<IIII',0x4c56474c,1,513,0),b''),(struct.pack('<IIII',0x4c56474c,self.abi['TOPIC_CLOUD_COMMAND'],1,0),b'x'),(struct.pack('<IIII',0x4c56474c,0,0,8),b'')]:
            s,_=self.connect();s.sendall(header+payload);self.assertEqual(s.recv(1),b'');s.close()
    def test_heartbeat(self):
        self.start();s,_=self.connect();s.sendall(struct.pack('<IIII',0x4c56474c,0,0,1));self.assertEqual(self.read(s),(0,b'',1))
    def test_partial_frame_timeout_does_not_block_other_clients(self):
        self.start();bad,_=self.connect();good,_=self.connect();bad.sendall(b'L')
        good.sendall(self.frame('cloud'));self.assertEqual(self.read(good)[0],self.abi['TOPIC_CLOUD_STATUS'])
        bad.settimeout(7)
        try:
            while True:self.read(bad)
        except EOFError:pass
        self.assertIsNone(self.p.poll())
    def test_duplicate_process_does_not_steal_socket(self):
        self.start();s,_=self.connect();inode=self.endpoint.stat().st_ino;self.refused();self.assertEqual(self.endpoint.stat().st_ino,inode)
        s.sendall(self.frame('cloud'));self.assertEqual(self.read(s)[0],self.abi['TOPIC_CLOUD_STATUS'])
    def test_client_limit_does_not_displace_existing_clients(self):
        self.start();connections=[self.connect()[0] for _ in range(8)]
        extra=socket.socket(socket.AF_UNIX);self.addCleanup(extra.close);extra.settimeout(3);extra.connect(str(self.endpoint));self.assertEqual(extra.recv(1),b'')
        connections[0].sendall(self.frame('cloud'));self.assertEqual(self.read(connections[0])[0],self.abi['TOPIC_CLOUD_STATUS'])

    def test_symlink_parent_is_rejected(self):
        real=self.root/'real';real.mkdir(mode=0o700);alias=self.root/'alias';alias.symlink_to(real,target_is_directory=True)
        self.endpoint=alias/'ipc.sock';self.refused();self.assertFalse((real/'ipc.sock').exists())

    def test_refuse_shared_directory(self):
        self.root.chmod(0o755);self.refused();self.assertFalse(self.endpoint.exists())
    def test_refuse_regular_file_and_symlink(self):
        self.endpoint.write_text('preserve');self.refused();self.assertEqual(self.endpoint.read_text(),'preserve')
        self.endpoint.unlink();target=self.root/'target';target.write_text('preserve');self.endpoint.symlink_to(target)
        self.refused();self.assertTrue(self.endpoint.is_symlink());self.assertEqual(target.read_text(),'preserve')
    def test_refuse_lock_symlink(self):
        target=self.root/'target';target.write_text('preserve');(self.root/'provider.lock').symlink_to(target)
        self.refused();self.assertEqual(target.read_text(),'preserve');self.assertFalse(self.endpoint.exists())
    def test_signal_cleanup_and_stale_socket_recovery(self):
        self.start();self.stop();self.assertEqual(self.p.returncode,0);self.assertFalse(self.endpoint.exists())
        self.start();self.p.kill();self.p.wait(timeout=3);self.assertTrue(self.endpoint.exists())
        self.start();s,_=self.connect();self.assertIsNone(self.p.poll())

if __name__=='__main__':unittest.main()
