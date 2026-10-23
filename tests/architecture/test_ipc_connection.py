"""Transport regressions using actual source and explicitly injected syscall schedules.

These host tests do not qualify physical provider or device behavior.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / 'apps/lv_port_linux/src'
FIXTURES = ROOT / 'tests/architecture/fixtures'

@unittest.skipUnless(shutil.which('gcc'), 'gcc required')
class IpcConnectionTests(unittest.TestCase):
    def run_fixture(self, name, sources, wrappers):
        with tempfile.TemporaryDirectory() as folder:
            binary = str(pathlib.Path(folder) / 'test')
            command = ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
                       '-Werror', '-fsanitize=address,undefined', '-pthread',
                       '-I', str(SRC), str(FIXTURES / (name + '.c'))]
            command += [str(SRC / source) for source in sources]
            command += ['-Wl,--wrap=' + wrapper for wrapper in wrappers]
            command += ['-o', binary]
            build = subprocess.run(command, capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([binary], capture_output=True, text=True, timeout=20)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertNotIn('runtime error:', run.stderr)
            self.assertNotIn('AddressSanitizer', run.stderr)
            return run.stdout

    def test_receive_boundaries(self):
        self.assertIn('PASS 529', self.run_fixture('ipc_connection_receive', [], ['clock_gettime']))

    def test_ui_lifetime(self):
        self.assertIn('without lock/UAF', self.run_fixture('ipc_middleware_ui',
            ['ipc/connection.c', 'ipc/ipc_socket.c'], ['send', 'ipc_client_connect']))

    def test_backend_lifetime(self):
        self.assertIn('without new POLLIN', self.run_fixture('ipc_middleware_backend',
            ['ipc/connection.c', 'ipc/ipc_socket.c'], ['send', 'clock_gettime']))

    def test_endpoint_owner(self):
        self.run_fixture("ipc_endpoint_owner", ["ipc/ipc_socket.c"], [])

    def test_actual_connect(self):
        self.run_fixture("ipc_connect_accept", ["ipc/ipc_socket.c", "ipc/connection.c"], [])

    def test_connect_deadline(self):
        output = self.run_fixture('ipc_connect_deadline', ['ipc/ipc_socket.c'],
                                 ['connect', 'poll', 'clock_gettime', 'getsockopt'])
        self.assertEqual(output.count('check1'), 13)

if __name__ == '__main__':
    unittest.main()
