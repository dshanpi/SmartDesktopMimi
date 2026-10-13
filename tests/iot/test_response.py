"""The production JSON boundary must not turn HTTP success into command success."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ResponseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.base = Path(cls.tmp.name)
        source = cls.base / 'probe.c'
        source.write_text('''#include <stdio.h>
#include "iot_json.h"
int main(int argc, char **argv) {
 json_object *o=mimi_iot_json_object(argv[1],strlen(argv[1]));
 printf("%d %d",o!=NULL,mimi_iot_command_accepted(o));
 if(o)json_object_put(o);
 return 0;
}
''')
        subprocess.run(['cc', '-I'+str(ROOT/'apps/lv_port_linux/src/apps/iot'),
                        str(source), '-ljson-c', '-o', str(cls.base/'probe')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def probe(self, data):
        return subprocess.check_output([str(self.base/'probe'), data], text=True)

    def test_explicit_acceptance_without_claiming_device_state(self):
        self.assertEqual(self.probe(' {"accepted":true,"state_confirmed":false} \n'), '1 1')

    def test_invalid_json_and_non_objects_never_accepted(self):
        for value in ['', 'null', '[]', 'true', '{} {}', '{} garbage', '{/*comment*/}',
                      '{"accepted":true,"state_confirmed":false}false']:
            with self.subTest(value=value):
                self.assertEqual(self.probe(value), '0 0')

    def test_missing_false_mistyped_or_conflicting_results_never_accepted(self):
        for value in ['{}', '{"accepted":true}', '{"accepted":false,"state_confirmed":false}',
                      '{"accepted":"true","state_confirmed":false}',
                      '{"accepted":1,"state_confirmed":false}',
                      '{"accepted":true,"state_confirmed":true}',
                      '{"accepted":true,"state_confirmed":"false"}',
                      '{"accepted":true,"state_confirmed":false,"error":"denied"}']:
            with self.subTest(value=value):
                self.assertEqual(self.probe(value), '1 0')


if __name__ == '__main__':
    unittest.main()
