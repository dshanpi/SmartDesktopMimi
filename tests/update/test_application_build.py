import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
import application_sdk as sdk
import application_build as build


class SDKPinTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        (self.root/'toolchain').mkdir();(self.root/'sysroot').mkdir()
        (self.root/'toolchain/compiler').write_text('compiler fixture')
        self.record=dict(schema_version=1,platform_id='a133',architecture='aarch64',system_baseline='host-unvalidated',files=sdk.inventory(self.root))
        self.save()
    def save(self):
        (self.root/'sdk-lock.json').write_text(json.dumps(self.record))
        self.pin=sdk.digest(self.root/'sdk-lock.json')
    def test_pin_accepts_exact_inputs_rejects_resealed_change(self):
        sdk.verify_sdk(self.root,self.pin)
        (self.root/'toolchain/compiler').write_text('changed')
        with self.assertRaisesRegex(ValueError,'differs'):sdk.verify_sdk(self.root,self.pin)
        self.record['files']=sdk.inventory(self.root)
        old=self.pin;self.save()
        with self.assertRaisesRegex(ValueError,'externally pinned'):sdk.verify_sdk(self.root,old)
    def test_wrong_baseline_and_target_rejected(self):
        for field,value in [('system_baseline',''),('platform_id','t527'),('architecture','arm')]:
            with self.subTest(field=field):
                original=self.record[field];self.record[field]=value;self.save()
                with self.assertRaises(ValueError):sdk.verify_sdk(self.root,self.pin)
                self.record[field]=original
    def test_missing_root_and_external_root_link_rejected(self):
        (self.root/'sysroot').rmdir()
        with self.assertRaisesRegex(ValueError,'real toolchain'):sdk.inventory(self.root)
        (self.root/'sysroot').symlink_to(self.root/'toolchain',target_is_directory=True)
        with self.assertRaisesRegex(ValueError,'real toolchain'):sdk.inventory(self.root)
    def test_special_files_and_duplicate_keys_rejected(self):
        os.mkfifo(self.root/'sysroot/fifo')
        with self.assertRaisesRegex(ValueError,'special file'):sdk.inventory(self.root)
        (self.root/'sysroot/fifo').unlink()
        (self.root/'sdk-lock.json').write_text('{"schema_version":1,"schema_version":2}')
        with self.assertRaisesRegex(ValueError,'duplicate'):sdk.verify_sdk(self.root)


class BuildReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);(self.root/'docs/update').mkdir(parents=True)
        (self.root/'api.h').write_text('interface fixture')
        (self.root/'docs/update/platform-contract-lock.json').write_text(json.dumps(dict(schema_version=1,contract_id='host-v1',headers={'api.h':build.digest(self.root/'api.h')})))
        self.desktop=self.root/'desktop';self.desktop.write_bytes(b'host fixture binary')
        self.receipt=self.root/'application-build.json'
        self.record=dict(schema_version=1,kind='application-build',platform_id='a133',architecture='aarch64',system_baseline='host-unvalidated',
                         sdk_lock_sha256='a'*64,source=dict(commit='b'*40,dirty=True),interface=build.contract(self.root),
                         build=dict(target='lvglsim',application_only=True,cloud_provider=False,compile_commands_sha256='c'*64),desktop_sha256=build.digest(self.desktop))
        self.save()
    def save(self):self.receipt.write_text(json.dumps(self.record))
    def test_exact_binary_and_interface_required(self):
        build.verify_receipt(self.receipt,self.desktop,self.root)
        self.desktop.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'desktop differs'):build.verify_receipt(self.receipt,self.desktop,self.root)
    def test_stale_interface_refused(self):
        (self.root/'api.h').write_text('incompatible')
        with self.assertRaisesRegex(ValueError,'interface input'):build.verify_receipt(self.receipt,self.desktop,self.root)
    def test_mixed_backend_build_and_invalid_metadata_refused(self):
        for field,value in [('target','lv_backend'),('application_only',False),('cloud_provider',True)]:
            with self.subTest(field=field):
                old=self.record['build'][field];self.record['build'][field]=value;self.save()
                with self.assertRaisesRegex(ValueError,'independent application'):build.verify_receipt(self.receipt,self.desktop,self.root)
                self.record['build'][field]=old
    def test_receipt_not_emitted_for_backend_compile_inventory(self):
        out=self.root/'build';out.mkdir()
        (out/'compile_commands.json').write_text(json.dumps([{'file':'/src/app/src/backend/backend_main.c'}]))
        with patch.object(build,'source_state',return_value=self.record['source']):
            with self.assertRaisesRegex(ValueError,'backend entered'):
                build.create_receipt(self.root,{},'a'*64,out,self.record['source'],self.record['interface'])
        self.assertFalse((out/'application-build.json').exists())

if __name__=='__main__':unittest.main()
