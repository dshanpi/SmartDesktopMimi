"""Public product adapters must be usable without any licensing IO symbols."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PAIRING = ROOT / 'integrations/tuyaopen/pairing'


class PublicPairingTests(unittest.TestCase):
    def compile_run(self, body, sources=(), headers=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            if headers:
                (root / 'tuya_iot.h').write_text(headers)
            (root / 'probe.c').write_text(body)
            binary = root / 'probe'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I'+str(root),
                            '-I'+str(PAIRING), str(root / 'probe.c'), *map(str, sources), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=5)

    def test_public_runtime_requires_no_device_license_or_network_api(self):
        self.compile_run('''#include <assert.h>
#include "mimi_device_pairing.h"
int main(void) {
  tuya_iot_client_t client = {0}; tuya_iot_license_t license = {0}; tuya_iot_config_t config = {0};
  assert(mimi_tuya_pairing_enabled() == 0);
  assert(mimi_tuya_pairing_init(&client, &license, &config) == MIMI_PAIRING_DISABLED);
  return 0;
}
''', [PAIRING / 'mimi_device_pairing_stub.c'], '''#ifndef TUYA_IOT_H
#define TUYA_IOT_H
typedef struct {int unused;} tuya_iot_client_t;
typedef struct {const char *uuid, *authkey;} tuya_iot_license_t;
typedef struct {const char *uuid, *authkey;} tuya_iot_config_t;
#endif
''')

    def test_public_backend_reader_cannot_open_existing_factory_credentials(self):
        self.compile_run('''#include <assert.h>
#include <stdlib.h>
#include "mimi_tuya_license_provider_stub.h"
int open(const char *path, int flags, ...) { (void)path; (void)flags; abort(); }
int main(void) { assert(!tuya_license_file_valid()); return 0; }
''')
