import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import unittest
from tests.sdk_source import services_source


class ControlReleaseTests(unittest.TestCase):
    def test_release_errors_timeout_and_next_request(self):
        source = services_source('src/controld.c')
        safe = services_source('src/safe_io.c')
        with tempfile.TemporaryDirectory(prefix='control-release-') as directory:
            root = Path(directory)
            endpoint, marker = root / 'control.sock', root / 'stop'
            helper, binary, log = root / 'hid', root / 'controld', root / 'calls'
            helper_source = root / 'helper.c'
            helper_source.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    (void)argc;
    FILE *file = fopen(getenv("LOG"), "a");
    if (!file) return 2;
    fprintf(file, "%s\n", argv[1]);
    fclose(file);
    const char *failure = getenv("FAIL");
    if (!strcmp(failure, "hang") && !strcmp(argv[1], "key")) {
        alarm(15);
        pause();
    }
    return !strcmp(failure, argv[1]) || !strcmp(failure, "both");
}
''')
            subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', str(helper_source), '-o', str(helper)], check=True)
            subprocess.run(['gcc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-DCONTROL_SOCKET="' + str(endpoint) + '"',
                            '-DSTOP_FILE="' + str(marker) + '"',
                            '-DHIDCTL_PATH="' + str(helper) + '"',
                            str(source), str(safe), '-o', str(binary), '-ljson-c'], check=True)
            def request(command):
                with socket.socket(socket.AF_UNIX) as client:
                    client.settimeout(3)
                    client.connect(str(endpoint))
                    client.sendall(json.dumps({'command': command}).encode() + b'\n')
                    return json.loads(client.recv(65536))
            for failure in ('key', 'mouse', 'both', 'none', 'marker', 'hang'):
                with self.subTest(failure=failure):
                    log.write_text('')
                    if marker.is_file(): marker.unlink()
                    if failure == 'marker': marker.mkdir()
                    child = subprocess.Popen([str(binary)], env=dict(os.environ, LOG=str(log), FAIL=failure),
                                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
                    try:
                        for _ in range(100):
                            if endpoint.exists(): break
                            self.assertIsNone(child.poll())
                            time.sleep(.01)
                        started = time.monotonic()
                        reply = request('stop')
                        self.assertEqual(reply['ok'], failure == 'none')
                        self.assertEqual(log.read_text().splitlines(), ['key', 'mouse'])
                        self.assertEqual(marker.is_file(), failure != 'marker')
                        if failure == 'hang': self.assertLess(time.monotonic() - started, 3)
                        self.assertIsInstance(request('status'), dict)
                    finally:
                        if child.poll() is None: child.terminate()
                        try: child.communicate(timeout=1)
                        except subprocess.TimeoutExpired:
                            os.killpg(child.pid, signal.SIGKILL)
                            child.communicate(timeout=1)
                            raise
                        if marker.is_dir(): marker.rmdir()
