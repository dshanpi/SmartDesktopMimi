"""Compile and execute the exact root parser used by the platform OTA service."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class OtaSlotTests(unittest.TestCase):
    def test_real_c_parser_rejects_ambiguous_and_unknown_roots(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'slot.c'
            binary = Path(temp) / 'slot'
            source.write_text('''#include "ota_slot.h"
#include <stdio.h>
int main(int argc, char **argv) {
 if (argc != 2) return 2;
 char slot = ota_slot_from_cmdline(argv[1]);
 if (slot) putchar(slot);
 return 0;
}
''')
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT/'apps/lv_port_linux/src/system'), str(source), '-o', str(binary)], check=True)
            cases = {
                'root=/dev/mmcblk0p4 rw': 'A',
                'root=/dev/mmcblk0p6 partitions=rootfsA@mmcblk0p4': 'B',
                'root=/dev/mmcblk0p40': '',
                'xroot=/dev/mmcblk0p4': '',
                'root=/dev/mmcblk0p4 root=/dev/mmcblk0p6': '',
                'root=/dev/mmcblk0p4 root=': '',
                'root=PARTUUID=unknown': '',
                '': '',
            }
            for line, expected in cases.items():
                with self.subTest(line=line):
                    self.assertEqual(subprocess.check_output([str(binary), line], text=True), expected)
