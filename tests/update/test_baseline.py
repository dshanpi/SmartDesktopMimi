import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('baseline',ROOT/'tools/update/baseline.py')
baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline)

class BaselineSealTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name);self.root=self.path/'rootfs'
        config=self.root/'etc/smartdesktopmimi';config.mkdir(parents=True)
        baseline.write(config/'device.json',{'enrollment_id':''})
        factory=self.root/'usr/lib/smartdesktopmimi/factory';factory.mkdir(parents=True)
        baseline.write(factory/'manifest.json',{'commit':'a'*40,'package_sha256':'b'*64})
        self.seal={'schema_version':1,'policy':'MIMI-UPDATE-001/1.0.0','factory_commit':'a'*40,'factory_package_sha256':'b'*64,'files':baseline.inventory(self.root)}
        baseline.write(self.path/'baseline-lock.json',self.seal)
    def test_rejects_modified_content_and_modes(self):
        baseline.verify(self.path)
        device=self.root/'etc/smartdesktopmimi/device.json'
        device.chmod(0o666)
        with self.assertRaises(ValueError):baseline.verify(self.path)
        device.chmod(0o644);device.write_text('{}')
        with self.assertRaises(ValueError):baseline.verify(self.path)
    def test_rejects_credential_partition_even_if_resealed(self):
        p=self.root/'overlay/factory';p.mkdir(parents=True)
        self.seal['files']=baseline.inventory(self.root);baseline.write(self.path/'baseline-lock.json',self.seal)
        with self.assertRaisesRegex(ValueError,'factory data'):baseline.verify(self.path)
    def test_rejects_unit_enrollment_in_generic_image(self):
        baseline.write(self.root/'etc/smartdesktopmimi/device.json',{'enrollment_id':'must-remain-device-local'})
        self.seal['files']=baseline.inventory(self.root);baseline.write(self.path/'baseline-lock.json',self.seal)
        with self.assertRaisesRegex(ValueError,'unit-specific'):baseline.verify(self.path)
    def test_rejects_symbolic_link_to_external_data(self):
        (self.root/'etc/escape').symlink_to('/etc/passwd')
        with self.assertRaisesRegex(ValueError,'symbolic'):baseline.verify(self.path)

if __name__=='__main__':unittest.main()
