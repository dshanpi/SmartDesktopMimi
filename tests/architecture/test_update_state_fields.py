import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class UpdateStateFields(unittest.TestCase):
    def test_actual_helpers_reject_malformed_fields_and_preserve_valid_states(self):
        source = (ROOT / 'apps/lv_port_linux/src/ui/desktop_update.c').read_text()
        helpers = source[source.index('static const char *text_field'):source.index('static void refresh')]
        cases = [
            ({'busy': True, 'phase': None}, '', True, False),
            ({'busy': 'false', 'phase': 'download'}, 'download', False, False),
            ({'busy': 1, 'phase': 'download'}, 'download', False, False),
            ({'busy': True, 'phase': 'check'}, 'check', True, True),
            ({'busy': True, 'phase': 'download'}, 'download', True, True),
            ({'busy': False, 'phase': 'download'}, 'download', False, False),
            ({'busy': True, 'phase': 'install'}, 'install', True, False),
            ({'busy': True, 'phase': 'future-operation'}, 'future-operation', True, False),
            ({'busy': True, 'phase': []}, '', True, False),
            ({'busy': True}, '', True, False),
            ([], '', False, False),
            (None, '', False, False),
        ]
        code = ['#include <assert.h>', '#include <stdbool.h>', '#include <string.h>', '#include <json-c/json.h>', helpers, 'int main(void) {']
        for value, phase, busy, cancel in cases:
            code += ['{ json_object *s = json_tokener_parse(' + json.dumps(json.dumps(value)) + ');',
                     'assert(strcmp(text_field(s, "phase"), ' + json.dumps(phase) + ') == 0);',
                     'assert(bool_field(s,"busy") == ' + str(int(busy)) + ');',
                     'assert(can_cancel(s) == ' + str(int(cancel)) + ');',
                     'json_object_put(s); }']
        code += ['return 0; }']
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp); (p/'test.c').write_text(chr(10).join(code))
            subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all', str(p/'test.c'), '-ljson-c', '-o', str(p/'test')], check=True)
            subprocess.run([str(p/'test')], check=True)

if __name__ == '__main__':
    unittest.main()
