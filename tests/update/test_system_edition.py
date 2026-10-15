"""Host edition baseline/metadata tests; no device provisioning or installation."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import test_baseline as baseline_fixtures
import test_prepare_release as preparation_fixtures
baseline=baseline_fixtures.baseline
import prepare_release as prepare
import release_gate as gate

ROOT=Path(__file__).resolve().parents[2]

class SystemProductEditionTests(unittest.TestCase):
    def sealed(self, edition):
        fixture=baseline_fixtures.BaselineSealTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        config=fixture.root/'etc/smartdesktopmimi'
        marker=config/'product-edition';marker.write_text(edition+'\n');marker.chmod(0o444)
        baseline.write(config/'system-device.json', {'product_edition':edition})
        fixture.seal['files']=baseline.inventory(fixture.root)
        baseline.write(fixture.path/'baseline-lock.json',fixture.seal)
        return fixture

    def reseal(self, fixture):
        fixture.seal['files']=baseline.inventory(fixture.root)
        baseline.write(fixture.path/'baseline-lock.json',fixture.seal)

    def test_sealed_edition_and_final_readback_both_profiles(self):
        for edition in ('public','private'):
            with self.subTest(edition=edition):
                f=self.sealed(edition);baseline.verify(f.path,edition)
                baseline.verify_rootfs(f.path,f.final_rootfs(),edition=edition)
                other='private' if edition=='public' else 'public'
                with self.assertRaisesRegex(ValueError,'edition'):baseline.verify(f.path,other)
                marker=f.root/'etc/smartdesktopmimi/product-edition';marker.chmod(0o644);marker.write_text(other+'\n');marker.chmod(0o444)
                self.reseal(f)
                with self.assertRaisesRegex(ValueError,'edition'):baseline.verify(f.path)

    def test_missing_unknown_or_partial_edition_is_never_defaulted(self):
        for mode in ('missing-marker','unknown','missing-declaration','multiple-lines','explicit-legacy'):
            with self.subTest(mode=mode):
                f=self.sealed('private');marker=f.root/'etc/smartdesktopmimi/product-edition';system=marker.with_name('system-device.json')
                if mode=='missing-marker':marker.unlink()
                elif mode=='missing-declaration':baseline.write(system,{})
                elif mode=='explicit-legacy':
                    marker.unlink();system.unlink()
                else:
                    marker.chmod(0o644);marker.write_text('unknown\n' if mode=='unknown' else 'private\n\n');marker.chmod(0o444)
                self.reseal(f)
                with self.assertRaisesRegex(ValueError,'edition'):baseline.verify(f.path,'private')

    def test_preparation_seals_selected_marker_and_system_descriptor(self):
        for edition in ('public','private'):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as temp:
                root=Path(temp);device={'identity':{'platform_id':'a133','architecture':'aarch64'},'enrollment_id':''}
                baseline.write(root/'device.json',device);baseline.write(root/'trust.json',{'fixture':'not a trust store'})
                baseline.write(root/'system.json',{'device':device,'storage':'emmc','boot_mode':'ab'})
                updater=root/'updater';updater.write_bytes(b'\x7fELF\x02\x01'+b'\x00'*12+b'\xb7\x00')
                def extract(command,check):
                    target=Path(command[command.index('--output')+1]);target.mkdir(parents=True)
                    baseline.write(target/'manifest.json',{'commit':'a'*40,'version':'1.0.0','package_sha256':'b'*64})
                args=SimpleNamespace(output=root/'baseline',device=root/'device.json',trust=root/'trust.json',
                    system_device=root/'system.json',updater=updater,application=root/'synthetic-application',
                    checker=Path('/mock-checker'),product_edition=edition)
                with patch.object(baseline.subprocess,'run',side_effect=extract):baseline.prepare(args)
                marker=args.output/'rootfs/etc/smartdesktopmimi/product-edition'
                self.assertEqual(marker.read_text(),edition+'\n');self.assertEqual(marker.stat().st_mode&0o777,0o444)
                self.assertEqual(json.loads(marker.with_name('system-device.json').read_text())['product_edition'],edition)
                baseline.verify(args.output,edition)

    def test_firmware_rejects_mismatched_baseline_before_sdk_mutation(self):
        # The earlier program/display gates are stubbed to isolate the real baseline CLI gate.
        for edition in ('public','private'):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as temp:
                root=Path(temp);product=root/'product';scripts=product/'scripts';scripts.mkdir(parents=True)
                for name in ('build_firmware.sh','lib.sh'):shutil.copy2(ROOT/'scripts'/name,scripts/name)
                (scripts/'check_product_edition.py').write_text('print("isolated prior program gate stub")\n')
                tools=product/'tools/update';tools.mkdir(parents=True);shutil.copy2(ROOT/'tools/update/baseline.py',tools/'baseline.py')
                (tools/'a133_display_gate.py').write_text('print("isolated prior display gate stub")\n')
                f=self.sealed('private' if edition=='public' else 'public')
                shutil.copytree(f.path,product/'build/mimi-baseline')
                sdk=root/'sdk';(sdk/'.repo').mkdir(parents=True);sentinel=sdk/'.config';sentinel.write_bytes(b'preserve SDK')
                env=dict(os.environ,MIMI_PRODUCT_EDITION=edition,AITVBOX_ENABLE_100ASK_CLOUD='ON' if edition=='private' else 'OFF',MIMI_APPLICATION_UPDATES='1')
                env.pop('MIMI_PLATFORM_PRODUCT_LOCK',None)
                result=subprocess.run(['bash',str(scripts/'build_firmware.sh'),str(sdk)],env=env,capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0);self.assertIn('BASELINE BLOCKED',result.stderr)
                self.assertIn('edition',result.stderr);self.assertEqual(sentinel.read_bytes(),b'preserve SDK')
                self.assertEqual(sorted(p.name for p in sdk.iterdir()),['.config','.repo'])

    def test_full_preparation_rejects_missing_or_wrong_signed_edition(self):
        fixture=preparation_fixtures.PreparationTests()
        for selection in ('missing','public'):
            with self.subTest(selection=selection), tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,plan=fixture.product_plan(root,'private')
                manifest=gate.load(root/'inputs/system-manifest.json')
                if selection=='missing':manifest.pop('product_edition')
                else:manifest['product_edition']=selection
                prepare.write(root/'inputs/system-manifest.json',manifest)
                plan['assets']['system-manifest.json']['sha256']=gate.sha(root/'inputs/system-manifest.json');prepare.write(path,plan)
                with patch.object(gate.subprocess,'run'), self.assertRaisesRegex(ValueError,'product source/rootfs/release'):
                    prepare.prepare(path,root/'candidate',Path('/mock-checker'),Path('/mock-trust'))
                self.assertFalse((root/'candidate').exists())

if __name__=='__main__':unittest.main()
