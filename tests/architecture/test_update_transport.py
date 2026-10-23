import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]

class UpdateTransport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.tmp.name)
        cls.socket_path = str(cls.path / 'update.sock')
        source = Path(os.environ.get('MIMI_UPDATE_SOURCE', str(ROOT/'apps/lv_port_linux/src/ui/desktop_update.c'))).read_text()
        start = source.find('static long long request_now_ms')
        if start < 0: start = source.index('static int request(')
        body = source[start:source.index('static void *worker')]
        body = body.replace(json.dumps('/run/smartdesktopmimi/update.sock'), json.dumps(cls.socket_path))
        includes = ['errno.h', 'stdio.h', 'stdlib.h', 'string.h', 'sys/socket.h', 'sys/time.h', 'sys/un.h', 'unistd.h', 'poll.h', 'time.h']
        code = [f'#include <{h}>' for h in includes] + [body, 'int main(void) { char out[65536] = {0}; int rc=request("status",out,sizeof(out)); printf("%d",rc); return 0; }']
        (cls.path/'client.c').write_text(chr(10).join(code))
        subprocess.run(['gcc','-Wall','-Wextra','-Werror',str(cls.path/'client.c'),'-o',str(cls.path/'client')],check=True)

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def exchange(self, chunks, interval=0, hold=0):
        server = socket.socket(socket.AF_UNIX)
        server.bind(self.socket_path); server.listen(1)
        stop = threading.Event()
        def serve():
            try:
                conn,_ = server.accept()
                with conn:
                    conn.settimeout(2); conn.recv(4096)
                    for chunk in chunks:
                        if stop.wait(interval): break
                        conn.sendall(chunk)
                    stop.wait(hold)
            except (BrokenPipeError, ConnectionResetError, OSError): pass
        thread=threading.Thread(target=serve); thread.start()
        started=time.monotonic()
        try:
            result=subprocess.run([str(self.path/'client')],capture_output=True,text=True,timeout=4.5)
            return int(result.stdout),time.monotonic()-started
        finally:
            stop.set(); thread.join(3); server.close(); Path(self.socket_path).unlink()

    def test_fragmented_success(self):
        response=b'HTTP/1.1 200 OK'+bytes([13,10,13,10])+b'{}'
        rc,_=self.exchange([response[:12],response[12:]],0.01)
        self.assertEqual(rc,0)

    def test_slow_trickle_has_total_deadline(self):
        rc,elapsed=self.exchange([b'H']*100,0.2)
        self.assertEqual(rc,-1); self.assertLess(elapsed,4.2)

    def test_silence_is_bounded(self):
        rc,elapsed=self.exchange([],hold=10)
        self.assertEqual(rc,-1); self.assertLess(elapsed,4.2)

    def test_peer_disconnect(self):
        rc,_=self.exchange([])
        self.assertNotEqual(rc,0)

    def test_missing_service(self):
        result=subprocess.run([str(self.path/'client')],capture_output=True,text=True,timeout=2)
        self.assertEqual(int(result.stdout),-1)

if __name__ == '__main__': unittest.main()
