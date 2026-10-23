"""Shared writer receipt rejects a changed build input or absent compile evidence."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import ui_settings_receipt as r

class ReceiptTests(unittest.TestCase):
    def fixture(self, root):
        for name in r.INPUTS:
            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'input')
        build=root/'build';(build/'bin').mkdir(parents=True)
        (build/'bin/lvglsim').write_bytes(b'\x7fELF\x02\x01'+b'\x00'*12+b'\xb7\x00fixture')
        (build/'compile_commands.json').write_text(json.dumps([dict(file=str(root/r.INPUTS[0]),command='fixture')]))
        r.prepare(root,build)
        return build
    def test_matching_input_and_binary_receipt(self):
        with tempfile.TemporaryDirectory() as value:
            root=Path(value);build=self.fixture(root)
            result=r.record(root,build)
            self.assertEqual(result['inputs'],r.inputs(root))
            self.assertEqual(result['binary_sha256'],r.sha(build/'bin/lvglsim'))
            self.assertEqual(json.loads((build/'settings-build-result.json').read_text()),result)
    def test_reject_changed_or_uncompiled_inputs(self):
        for case in ('changed','missing_prepare','missing_compile','duplicate_compile','wrong_arch','aliased_binary'):
            with self.subTest(case=case),tempfile.TemporaryDirectory() as value:
                root=Path(value);build=self.fixture(root)
                if case=='changed': (root/r.INPUTS[1]).write_bytes(b'changed')
                elif case=='missing_prepare': (build/'settings-inputs-before.json').unlink()
                elif case in ('missing_compile','duplicate_compile'):
                    path=build/'compile_commands.json';data=json.loads(path.read_text())
                    path.write_text(json.dumps([] if case=='missing_compile' else data*2))
                elif case=='wrong_arch': (build/'bin/lvglsim').write_bytes(b'not ELF')
                else:
                    path=build/'bin/lvglsim';path.rename(build/'saved');path.symlink_to(build/'saved')
                with self.assertRaises((ValueError,OSError)):r.record(root,build)
                self.assertFalse((build/'settings-build-result.json').exists())
if __name__=='__main__':unittest.main()
