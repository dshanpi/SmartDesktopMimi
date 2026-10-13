import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
import pack_inputs

class PackInputs(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.sdk = Path(self.temp.name)
        self.out = self.sdk / 'out/a133/b6/openwrt'
        self.out.mkdir(parents=True)
        self.text = ''.join('export '+k+'='+v+'\n' for k,v in {
            'LICHEE_IC':'a133', 'LICHEE_BOARD':'b6', 'LICHEE_LINUX_DEV':'openwrt',
            'LICHEE_TOP_DIR':str(self.sdk), 'LICHEE_PLAT_OUT':str(self.out)}.items())
        (self.sdk / '.buildconfig').write_text(self.text)
        (self.out / '.buildconfig').write_text(self.text)
        (self.out / pack_inputs.GOLDEN_NAME).write_bytes(b'known baseline')
        guard = patch.object(pack_inputs, 'GOLDEN_SHA256', hashlib.sha256(b'known baseline').hexdigest())
        guard.start(); self.addCleanup(guard.stop)
    def test_valid_restored_inputs(self):
        self.assertEqual(pack_inputs.verify(self.sdk)['target'], 'a133/b6/openwrt')
    def test_missing_or_corrupt_baseline_rejected(self):
        p = self.out / pack_inputs.GOLDEN_NAME
        p.write_bytes(b'wrong')
        with self.assertRaisesRegex(ValueError,'hash mismatch'): pack_inputs.verify(self.sdk)
        p.unlink()
        with self.assertRaises(FileNotFoundError): pack_inputs.verify(self.sdk)
    def test_missing_or_stale_output_config_rejected(self):
        p = self.out / '.buildconfig'; p.write_text(self.text+'# stale\n')
        with self.assertRaisesRegex(ValueError,'differs'): pack_inputs.verify(self.sdk)
        p.unlink()
        with self.assertRaises(FileNotFoundError): pack_inputs.verify(self.sdk)
    def test_wrong_board_and_relocated_paths_rejected(self):
        for old,new in [('LICHEE_BOARD=b6','LICHEE_BOARD=other'),
                        ('LICHEE_TOP_DIR='+str(self.sdk),'LICHEE_TOP_DIR=/stale')]:
            (self.sdk / '.buildconfig').write_text(self.text.replace(old,new))
            with self.assertRaisesRegex(ValueError,'target/path'): pack_inputs.verify(self.sdk)
    def test_duplicate_target_cannot_override(self):
        (self.sdk / '.buildconfig').write_text(self.text+'export LICHEE_BOARD=b6\n')
        with self.assertRaisesRegex(ValueError,'duplicate'): pack_inputs.verify(self.sdk)
