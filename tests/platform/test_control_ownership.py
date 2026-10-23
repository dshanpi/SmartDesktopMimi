import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
from tests.sdk_source import services_source


class ControlOwnershipTests(unittest.TestCase):
    def setUp(self):
        source = services_source('src/controld.c')
        safe = services_source('src/safe_io.c')
        self.temporary = tempfile.TemporaryDirectory(prefix='control-owner-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.endpoint = self.root / 'control.sock'
        self.lock = self.root / 'control.sock.lock'
        self.binary = self.root / 'controld'
        subprocess.run(['gcc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-DCONTROL_SOCKET="' + str(self.endpoint) + '"', str(source),
                        str(safe), '-o', str(self.binary), '-ljson-c'], check=True)

    def start(self):
        child = subprocess.Popen([str(self.binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=1)
        self.addCleanup(cleanup)
        return child

    def ready(self, child):
        for _ in range(100):
            try:
                with socket.socket(socket.AF_UNIX) as client:
                    client.settimeout(.2)
                    client.connect(str(self.endpoint))
                    client.sendall(b'{"command":"status"}\n')
                    self.assertIsInstance(json.loads(client.recv(65536)), dict)
                    return
            except OSError:
                time.sleep(.01)
            self.assertIsNone(child.poll())
        self.fail('daemon did not become reachable')

    def stop(self, child):
        child.terminate()
        child.communicate(timeout=1)
        self.assertEqual(child.returncode, 0)

    def test_duplicate_restart_and_foreign_endpoint(self):
        first = self.start()
        self.ready(first)
        inode = self.endpoint.stat().st_ino
        duplicate = self.start()
        duplicate.communicate(timeout=1)
        self.assertEqual(duplicate.returncode, 1)
        self.assertEqual(self.endpoint.stat().st_ino, inode)
        self.ready(first)
        self.stop(first)
        self.assertFalse(self.endpoint.exists())
        self.assertTrue(self.lock.exists())
        second = self.start()
        self.ready(second)
        self.endpoint.unlink()
        with socket.socket(socket.AF_UNIX) as foreign:
            foreign.bind(str(self.endpoint))
            inode = self.endpoint.stat().st_ino
            self.stop(second)
            self.assertEqual(self.endpoint.stat().st_ino, inode)

    def test_unsafe_locks_and_crash_recovery(self):
        target = self.root / 'target'
        for kind in ('symlink', 'hardlink', 'fifo', 'directory', 'writable'):
            with self.subTest(kind=kind):
                target.write_bytes(b'preserve')
                target.chmod(0o600)
                if kind == 'symlink': self.lock.symlink_to(target)
                elif kind == 'hardlink': os.link(target, self.lock)
                elif kind == 'fifo': os.mkfifo(str(self.lock), 0o600)
                elif kind == 'directory': self.lock.mkdir()
                else:
                    self.lock.touch()
                    self.lock.chmod(0o666)
                invalid = self.start()
                invalid.communicate(timeout=1)
                self.assertEqual(invalid.returncode, 1)
                self.assertFalse(self.endpoint.exists())
                self.assertEqual(target.read_bytes(), b'preserve')
                if kind == 'directory': self.lock.rmdir()
                else: self.lock.unlink()
                target.unlink()
        first = self.start()
        self.ready(first)
        lock_inode = self.lock.stat().st_ino
        first.kill()
        first.communicate(timeout=1)
        self.assertTrue(self.endpoint.exists())
        second = self.start()
        self.ready(second)
        self.assertEqual(self.lock.stat().st_ino, lock_inode)
        self.stop(second)
        self.assertFalse(self.endpoint.exists())
