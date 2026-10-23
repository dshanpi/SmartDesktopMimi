import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ControlClientTests(unittest.TestCase):
    def test_exact_transport_framing_and_total_deadline(self):
        source = (ROOT / 'apps/lv_port_linux/src/apps/hdmi_mcp/hdmi_mcp_app.c').read_text()
        transport = source[source.index('static int64_t control_now_ms'):source.index('static json_object *control_request(const')]
        worker = source[source.index('static void *stop_worker'):]
        predicate = worker[worker.index('    json_object *ok = NULL;'):worker.index('    if (reply) json_object_put(reply);')]
        with tempfile.TemporaryDirectory(prefix='control-client-') as directory:
            root = Path(directory)
            code, binary, endpoint = root / 'client.c', root / 'client', root / 'socket'
            code.write_text('''#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <json-c/json.h>
static const char *endpoint;
#define AGENT_CONTROL_SOCKET endpoint
#define AGENT_RESPONSE_MAX (96 * 1024)
static bool desktop_hdmi_supported(void) { return true; }
''' + transport + '''
int main(int argc, char **argv) {
    if (argc != 3) return 3;
    endpoint = argv[1];
    json_object *reply = control_request_timeout("{\\"command\\":\\"stop\\"}", atoi(argv[2]));
''' + predicate + '''
    printf("{\\"parsed\\":%s,\\"success\\":%s}\\n", reply ? "true" : "false", success ? "true" : "false");
    if (reply) json_object_put(reply);
    return 0;
}
''')
            subprocess.run(['gcc', '-std=gnu99', '-O2', '-Wall', '-Wextra', '-Werror', str(code), '-o', str(binary), '-ljson-c'], check=True)
            cases = [
                ('true', b'{"ok":true}\n', True, True, 250),
                ('false', b'{"ok":false}\n', True, False, 250),
                ('string', b'{"ok":"true"}\n', True, False, 250),
                ('array', b'[true]\n', False, False, 250),
                ('trailing', b'{"ok":true}junk\n', False, False, 250),
                ('comma', b'{"ok":true,}\n', False, False, 250),
                ('comment', b'{/*x*/"ok":true}\n', False, False, 250),
                ('nul', b'{"ok":true}\0junk\n', False, False, 250),
                ('no_newline', b'{"ok":true}', False, False, 250),
                ('drip', None, False, False, 250),
                ('silent', None, False, False, 6000),
            ]
            for name, payload, parsed, success, deadline in cases:
                with self.subTest(case=name), socket.socket(socket.AF_UNIX) as server:
                    endpoint.unlink(missing_ok=True)
                    server.bind(str(endpoint)); server.listen(1); server.settimeout(2)
                    finished = threading.Event()
                    def serve():
                        with server.accept()[0] as peer:
                            peer.settimeout(2); data = b''
                            while b'\n' not in data: data += peer.recv(1024)
                            if payload is not None:
                                peer.sendall(payload); return
                            while not finished.wait(.05):
                                if name == 'drip':
                                    try: peer.sendall(b' ')
                                    except OSError: return
                    thread = threading.Thread(target=serve)
                    thread.start(); started = time.monotonic()
                    try:
                        completed = subprocess.run([str(binary), str(endpoint), str(deadline)], capture_output=True, text=True, timeout=8, check=True)
                        elapsed = time.monotonic() - started
                        self.assertEqual(json.loads(completed.stdout), {'parsed': parsed, 'success': success})
                        if name in ('drip', 'silent'):
                            self.assertGreaterEqual(elapsed, deadline / 1000 - .05)
                            self.assertLess(elapsed, deadline / 1000 + .8)
                    finally:
                        finished.set(); thread.join(timeout=2)
                        self.assertFalse(thread.is_alive())
