import json
import socket
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from tests.sdk_source import services_source

class ControlSocketTests(unittest.TestCase):
    def test_deadline_stop_and_next_client(self):
        source = services_source('src/controld.c')
        safe = services_source('src/safe_io.c')
        with tempfile.TemporaryDirectory(prefix='control-io-') as directory:
            p = Path(directory)
            sock = str(p / 'test.sock')
            binary = p / 'controld'
            subprocess.run(['gcc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-DCONTROL_SOCKET="' + sock + '"', str(source), str(safe),
                            '-o', str(binary), '-ljson-c'], check=True)
            results=[]
            for case in ['idle_stop','partial_stop','silent_timeout','drip_timeout','status']:
             proc=subprocess.Popen([str(binary)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
             c=None
             try:
              for _ in range(100):
               if Path(sock).exists():break
               time.sleep(.01)
              if case!='idle_stop':
               c=socket.socket(socket.AF_UNIX);c.settimeout(7);c.connect(sock)
              if case=='partial_stop':c.sendall(b'{');time.sleep(.05)
              began=time.monotonic()
              if case.endswith('_stop'):
               proc.terminate();proc.wait(timeout=.5);assert proc.returncode==0
              elif case=='status':
               c.sendall(b'{"command":"status"}\n');data=c.recv(65536);assert isinstance(json.loads(data),dict)
              else:
               if case=='drip_timeout':
                def drip():
                 for _ in range(40):
                  try:c.sendall(b' ')
                  except OSError:return
                  time.sleep(.2)
                t=threading.Thread(target=drip);t.start()
               assert c.recv(1)==b''
               elapsed=time.monotonic()-began;assert 4.5<elapsed<6.5,elapsed
               if case=='drip_timeout':t.join(timeout=1);assert not t.is_alive()
               # Next client must still receive a complete reply after expiration.
               with socket.socket(socket.AF_UNIX) as retry:
                retry.settimeout(1);retry.connect(sock);retry.sendall(b'{"command":"status"}\n');assert isinstance(json.loads(retry.recv(65536)),dict)
              results.append(dict(case=case,status='passed',seconds=round(time.monotonic()-began,3)))
             finally:
              if c:c.close()
              if proc.poll() is None:
               proc.terminate()
               try:proc.wait(timeout=1)
               except subprocess.TimeoutExpired:proc.kill();proc.wait();raise
              proc.communicate(timeout=1)
              Path(sock).unlink(missing_ok=True)
