"""Real service V2 requests through the existing Python contract codec."""
import importlib.util
import json
import os
import socket
import struct
import sys
import subprocess
import time
import unittest
import test_ubuntu_provider as foundation
ROOT=foundation.ROOT

spec=importlib.util.spec_from_file_location('mimi_v2_reference',ROOT/'core/contracts/v2/aitvbox_v2.py')
codec=importlib.util.module_from_spec(spec);sys.modules[spec.name]=codec;spec.loader.exec_module(codec)

class UbuntuV2Tests(unittest.TestCase):
    setUpClass=classmethod(foundation.UbuntuProviderTests.setUpClass.__func__)
    tearDownClass=classmethod(foundation.UbuntuProviderTests.tearDownClass.__func__)
    setUp=foundation.UbuntuProviderTests.setUp
    stop=foundation.UbuntuProviderTests.stop
    start=foundation.UbuntuProviderTests.start
    refused=foundation.UbuntuProviderTests.refused
    connect=foundation.UbuntuProviderTests.connect
    read=foundation.UbuntuProviderTests.read
    exact=staticmethod(foundation.UbuntuProviderTests.exact)
    frame=foundation.UbuntuProviderTests.frame

    def v1_heartbeat(self,s):
        s.sendall(struct.pack("<IIII",0x4c56474c,0,0,1))
        for _ in range(50):
            if self.read(s)==(0,b"",1):return
        self.fail("V1 heartbeat missing among periodic status frames")
    def v2(self):
        s=socket.socket(socket.AF_UNIX);s.settimeout(3);self.addCleanup(s.close)
        s.connect(str(self.endpoint)+'.v2');return s
    def request(self,topic='system.ping',request_id='request-测试',payload=None):
        return dict(schemaVersion=2,messageType='request',topic=topic,requestId=request_id,payload={} if payload is None else payload)
    def receive(self,s):
        header=self.exact(s,4);size=struct.unpack('>I',header)[0];self.assertLessEqual(size,65536)
        return codec.Decoder().feed(header+self.exact(s,size))[0]
    def exchange(self,s,request):
        s.sendall(codec.encode(request));return self.receive(s)
    def closed(self,s):
        try:self.assertEqual(s.recv(1),b'')
        except ConnectionResetError:pass
    def test_reference_codec_queries_and_truthful_descriptor(self):
        self.start();s=self.v2();r=self.exchange(s,self.request())
        self.assertEqual(r['payload'],{'alive':True});self.assertEqual(r['requestId'],'request-测试');self.assertEqual(r['messageType'],'response')
        r=self.exchange(s,self.request('platform.describe'))
        self.assertEqual(r['payload'],dict(platformId='ubuntu-host',soc='host',architecture='x86_64',providerVersion='0.2.0',abiVersion=2,capabilities=0,localNetworkObservation=True,releaseQualified=False))
        self.assertEqual(os.stat(str(self.endpoint)+'.v2').st_mode&0o777,0o600)
    def test_fragmentation_coalescing_and_unknown_extension(self):
        self.start();s=self.v2();q=self.request();q['future']={'field':True};f=codec.encode(q)
        for chunk in (f[:1],f[1:4],f[4:37],f[37:]):s.sendall(chunk);time.sleep(.02)
        self.assertTrue(self.receive(s)['payload']['alive'])
        s.sendall(codec.encode(self.request(request_id='1'))+codec.encode(self.request('platform.describe','2')))
        self.assertEqual([self.receive(s)['requestId'],self.receive(s)['requestId']],['1','2'])
    def test_unknown_and_nonempty_query_payload_never_execute(self):
        self.start();s=self.v2()
        for topic,payload,error in [('wifi.connect',{'password':'V2_PRIVATE_PAYLOAD'},'UNKNOWN_TOPIC'),('system.ping',{'execute':'touch /tmp/should-not-run'},'INVALID_ARGUMENT')]:
            r=self.exchange(s,self.request(topic,payload=payload));self.assertEqual(r['error']['code'],error);self.assertFalse(r['error']['retryable']);self.assertEqual(r['payload'],{})
        self.stop();self.assertNotIn('V2_PRIVATE_PAYLOAD',(self.root/'provider.log').read_text())
    def test_invalid_envelopes_isolated_from_v1_and_v2(self):
        self.start();good=self.v2();v1,_=self.connect()
        cases=[dict(schemaVersion=v) for v in (1,2.0,4294967298,True)]+[{'messageType':'response'},{'messageType':'event','sequence':1},{'topic':'system.ping\0evil'},{'requestId':'x\0y'},{'payload':[]},{'error':{'code':'X','message':'X','retryable':False}},{'requestId':''}]
        for change in cases:
            q=self.request();q.update(change);body=json.dumps(q).encode();bad=self.v2();bad.sendall(struct.pack('>I',len(body))+body);self.closed(bad);bad.close()
        self.assertTrue(self.exchange(good,self.request())['payload']['alive'])
        self.v1_heartbeat(v1)
    def test_bad_json_utf8_lengths_and_trailing_bytes(self):
        self.start()
        valid=codec.encode(self.request())[4:]
        for body in (b'{}',b'[]',b'null',b'{/*comment*/}',valid+b'{}',valid+b'\0',valid.replace(b'system.ping',b'\xff'),valid.replace(b'system.ping',b'\xc0\xaf'),valid.replace(b'system.ping',b'\xed\xa0\x80')):
            s=self.v2();s.sendall(struct.pack('>I',len(body))+body);self.closed(s);s.close()
        for size in (0,65537,0xffffffff):
            s=self.v2();s.sendall(struct.pack('>I',size));self.closed(s);s.close()
        good=self.v2();self.assertTrue(self.exchange(good,self.request())['payload']['alive'])
    def test_maximum_frame_and_trailing_whitespace(self):
        self.start();s=self.v2();q=self.request();q['extension']='';body=json.dumps(q).encode()
        q['extension']='x'*(65536-len(body));body=json.dumps(q).encode();self.assertEqual(len(body),65536)
        s.sendall(struct.pack('>I',len(body))+body);self.assertTrue(self.receive(s)['payload']['alive'])
        body=codec.encode(self.request())[4:]+b' \n\t';s.sendall(struct.pack('>I',len(body))+body);self.assertTrue(self.receive(s)['payload']['alive'])
    def test_partial_frame_deadline_while_other_clients_work(self):
        self.start();bad=self.v2();bad.sendall(b'\0');good=self.v2();v1,_=self.connect()
        self.assertTrue(self.exchange(good,self.request())['payload']['alive'])
        self.v1_heartbeat(v1)
        bad.settimeout(7);self.closed(bad);self.assertIsNone(self.p.poll())
    def test_v2_capacity_independent_of_v1(self):
        self.start();clients=[self.v2() for _ in range(8)]
        for s in clients:self.assertTrue(self.exchange(s,self.request())['payload']['alive'])
        extra=self.v2();self.closed(extra);v1,_=self.connect();self.v1_heartbeat(v1)
        self.assertTrue(self.exchange(clients[0],self.request())['payload']['alive'])
    def test_different_uid_rejected_even_when_root_can_access_directory(self):
        if os.geteuid()==0 or subprocess.run(['sudo','-n','true'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
            self.skipTest('Requires non-root service user with noninteractive sudo for real SO_PEERCRED test')
        self.start()
        script="import socket,sys; s=socket.socket(socket.AF_UNIX);s.settimeout(3);s.connect(sys.argv[1]);assert s.recv(1)==b''"
        p=subprocess.run(['sudo','-n','python3','-c',script,str(self.endpoint)+'.v2'],capture_output=True,timeout=5)
        self.assertEqual(p.returncode,0,p.stderr.decode())
        same=self.v2();self.assertTrue(self.exchange(same,self.request())['payload']['alive'])
    def test_shutdown_preserves_replacement_file(self):
        self.start();v2=self.root/'ipc.sock.v2';v2.rename(self.root/'original.sock');v2.write_text('preserve replacement')
        self.stop();self.assertEqual(v2.read_text(),'preserve replacement');self.assertEqual(self.p.returncode,0)
    def test_v2_file_or_symlink_conflict_preserved(self):
        p=self.root/'ipc.sock.v2';p.write_text('keep');self.refused();self.assertEqual(p.read_text(),'keep');self.assertFalse(self.endpoint.exists())
        p.unlink();target=self.root/'target';target.write_text('keep');p.symlink_to(target);self.refused();self.assertTrue(p.is_symlink());self.assertEqual(target.read_text(),'keep')
    def test_duplicate_shutdown_and_crash_recovery_owns_both_endpoints(self):
        self.start();v2=self.root/'ipc.sock.v2';inode=v2.stat().st_ino;self.refused();self.assertEqual(v2.stat().st_ino,inode)
        self.stop();self.assertEqual(self.p.returncode,0);self.assertFalse(v2.exists());self.assertFalse(self.endpoint.exists())
        self.start();self.p.kill();self.p.wait(timeout=3);self.assertTrue(v2.exists());self.start()
        s=self.v2();self.assertTrue(self.exchange(s,self.request())['payload']['alive'])

if __name__=='__main__':unittest.main()
