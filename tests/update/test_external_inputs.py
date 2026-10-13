import unittest, tempfile, sys, json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
from external_inputs import inventory
class ExternalInputs(unittest.TestCase):
    def test_relocatable_and_changes_detected(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            x,y=Path(a),Path(b)
            for root in (x,y):
                (root/'lib.so.1').write_bytes(b'library');(root/'lib.so').symlink_to('lib.so.1')
            self.assertEqual(inventory(x),inventory(y))
            (y/'lib.so.1').write_bytes(b'changed')
            self.assertNotEqual(inventory(x),inventory(y))
    def test_external_link_rejected(self):
        with tempfile.TemporaryDirectory() as a:
            p=Path(a);(p/'bad').symlink_to('/etc/passwd')
            with self.assertRaises(ValueError):inventory(p)
    def test_mode_change_detected(self):
        with tempfile.TemporaryDirectory() as a:
            p=Path(a);f=p/'library';f.write_bytes(b'x');f.chmod(0o644);before=inventory(p);f.chmod(0o755)
            self.assertNotEqual(before,inventory(p))
    def test_cli_expected_hash_refuses_changed_input(self):
        with tempfile.TemporaryDirectory() as a:
            p=Path(a);(p/'lib').write_bytes(b'x');cmd=[sys.executable,str(ROOT/'tools/update/external_inputs.py'),str(p)]
            digest=subprocess.check_output(cmd,text=True).strip();(p/'lib').write_bytes(b'y')
            result=subprocess.run(cmd+['--expect',digest],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0);self.assertIn('inputs changed',result.stderr)
