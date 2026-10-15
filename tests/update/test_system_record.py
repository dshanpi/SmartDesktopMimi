"""Synthetic host build receipts, never flashable system images."""
import gzip
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/update'))
import system_record as record


class SystemRecordTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); self.build = self.root/'build'; self.build.mkdir()
        self.output = self.root/'output'; self.plan_path = self.root/'plan.json'
        self.factory = dict(commit='a'*40, version='1.2.0', package_sha256='b'*64)
        self.device = dict(identity=dict(platform_id='a133', architecture='aarch64'),
                           runtime=dict(system='2.0.0'), enrollment_id='')
        for name, value in {
            'BUILD-COMPLETE': '', 'firmware/a133_linux_b6_uart0.img': 'HOST FIXTURE ONLY',
            'metadata/factory-manifest.json': json.dumps(self.factory),
            'metadata/factory-manifest.sig': 'synthetic signature; checker mocked',
            'metadata/factory-device.json': json.dumps(self.device),
            'logs/50-bootchain-gate.log': 'host fixture; not a bootchain test',
        }.items():
            p = self.build/name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(value)
        self.seal = dict(schema_version=1, policy='MIMI-UPDATE-001/1.0.0',
                         factory_commit=self.factory['commit'], factory_version='1.2.0',
                         factory_package_sha256=self.factory['package_sha256'], files={})
        for name, sealed in [('factory-manifest.json', 'usr/lib/smartdesktopmimi/factory/manifest.json'),
                             ('factory-manifest.sig', 'usr/lib/smartdesktopmimi/factory/manifest.sig'),
                             ('factory-device.json', 'etc/smartdesktopmimi/device.json')]:
            self.seal['files'][sealed] = dict(sha256=record.digest(self.build/'metadata'/name))
        record.write(self.build/'metadata/baseline-lock.json', self.seal)
        self.manifest = dict(AITVBOX_RELEASE_FORMAT='1', PRODUCT_TRACKED_DIRTY='0', PRODUCT_DIRTY='0',
                             APPLICATION_UPDATES='1', TARGET_SOC='a133', TARGET_BOARD='b6', PACK_VARIANT='uart0',
                             PRODUCT_COMMIT='c'*40, SYSTEM_VERSION='2.0.0', SYSTEM_TAG='system-a133-v2.0.0',
                             SDK_MANIFEST_SHA256='d'*64)
        sdk = dict(schema_version=1, sdk_baseline='host-sdk-only', manifest_sha256='d'*64,
                   components=[dict(path='kernel/linux', commit='e'*40, local_inputs_sha256='f'*64)])
        record.write(self.root/'sdk-components.json', sdk)
        record.write(self.root/'dependency-lock.json', {'host_fixture_only': True})
        self.plan = dict(schema_version=1, hardware_commit='d'*40, sdk_baseline='host-sdk-only',
                         sdk_local_inputs_sha256='1'*64, toolchain_sha256='2'*64, sysroot_sha256='3'*64,
                         partition_sha256='4'*64, inputs={name: dict(path=name, sha256=record.digest(self.root/name))
                         for name in ['sdk-components.json', 'dependency-lock.json']})
        self.refresh()

    def refresh(self):
        (self.build/'BUILD-MANIFEST.txt').write_text(''.join(k+'='+v+'\n' for k,v in self.manifest.items())+'\n')
        self.plan['build_manifest_sha256'] = record.digest(self.build/'BUILD-MANIFEST.txt')
        record.write(self.plan_path, self.plan)
        (self.build/'SHA256SUMS').write_text(''.join(record.digest(p)+'  '+p.relative_to(self.build).as_posix()+'\n'
            for p in sorted(self.build.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))

    def prepare(self):
        return record.prepare(self.build, self.plan_path, self.output, Path('/checker'), Path('/external-trust'))

    def product(self):
        from test_product_lock import lock_fixture
        product = lock_fixture()
        product['repositories']['application']['commit'] = self.manifest['PRODUCT_COMMIT']
        product['repositories']['hardware']['commit'] = self.plan['hardware_commit']
        product['sdk_manifest_sha256'] = self.manifest['SDK_MANIFEST_SHA256']
        record.write(self.build/'metadata/platform-product-lock.json', product)
        check = dict(schema_version=1, status='passed', edition='public', physical='not_run', release_qualified=False,
                     product_lock_sha256=record.digest(self.build/'metadata/platform-product-lock.json'),
                     rootfs_image_sha256='8'*64)
        record.write(self.build/'metadata/product-rootfs-check.json', check)
        self.manifest.update(PRODUCT_EDITION='public', PLATFORM_PRODUCT_LOCK_SHA256=check['product_lock_sha256'],
                             PRODUCT_ROOTFS_IMAGE_SHA256=check['rootfs_image_sha256'])
        self.refresh()
        return product, check

    def test_product_lock_and_rootfs_readback_are_bound_to_system_record(self):
        product, check = self.product()
        with patch.object(record.subprocess, 'run'):
            result = self.prepare()
        self.assertEqual(result['product_edition'], 'public')
        self.assertEqual(result['platform_product_lock_sha256'], self.manifest['PLATFORM_PRODUCT_LOCK_SHA256'])
        self.assertEqual(result['rootfs_image_sha256'], check['rootfs_image_sha256'])
        self.assertEqual(record.load(self.output/'platform-product-lock.json'), product)
        self.assertEqual(record.load(self.output/'product-rootfs-check.json'), check)

    def test_mismatched_product_sources_or_rootfs_cannot_create_system_record(self):
        import shutil
        for change in ('application', 'hardware', 'sdk', 'edition', 'rootfs', 'qualified', 'missing'):
            with self.subTest(change=change):
                product, check = self.product()
                if change in ('application', 'hardware'):
                    product['repositories'][change]['commit'] = 'f'*40
                elif change == 'sdk':product['sdk_manifest_sha256'] = '7'*64
                elif change == 'edition':self.manifest['PRODUCT_EDITION'] = 'private'
                elif change == 'rootfs':check['rootfs_image_sha256'] = 'wrong'
                elif change == 'qualified':check['release_qualified'] = True
                record.write(self.build/'metadata/platform-product-lock.json', product)
                check['product_lock_sha256'] = record.digest(self.build/'metadata/platform-product-lock.json')
                self.manifest['PLATFORM_PRODUCT_LOCK_SHA256'] = check['product_lock_sha256']
                record.write(self.build/'metadata/product-rootfs-check.json', check)
                if change == 'missing':(self.build/'metadata/product-rootfs-check.json').unlink()
                self.refresh()
                with patch.object(record.subprocess, 'run'), self.assertRaises(ValueError):self.prepare()
                self.assertFalse(self.output.exists())

    def test_record_binds_actual_image_and_factory_and_is_deterministic(self):
        with patch.object(record.subprocess, 'run') as checker:
            result = self.prepare()
            self.assertEqual(checker.call_args.args[0][1], 'verify-manifest')
        self.assertEqual(gzip.decompress((self.output/record.image_name('2.0.0')).read_bytes()), b'HOST FIXTURE ONLY')
        self.assertEqual(result['image_sha256'], record.digest(self.output/record.image_name('2.0.0')))
        self.assertEqual(result['source_commit'], 'c'*40)
        self.assertEqual(result['system_version'], '2.0.0')
        self.assertEqual(result['factory_application'], dict(commit='a'*40, version='1.2.0', sha256='b'*64))
        self.assertFalse((self.output/'validation.json').exists())
        self.assertFalse((self.output/'factory-device.json').exists())
        previous = (self.output/record.image_name('2.0.0')).read_bytes(); self.output = self.root/'second'
        with patch.object(record.subprocess, 'run'): self.prepare()
        self.assertEqual(previous, (self.output/record.image_name('2.0.0')).read_bytes())

    def test_new_product_build_archive_and_raw_pin_binding(self):
        from system_image import archive
        raw = self.build/'firmware/a133_linux_b6_uart0.img'
        packed = archive(raw, self.build/'firmware', '2.0.0', record.digest(raw))
        raw.unlink()
        record.write(self.build/'metadata/image-archive.json', packed)
        self.manifest.update(SYSTEM_IMAGE_FILE=packed['filename'], SYSTEM_IMAGE_RAW_SHA256=packed['raw_sha256'],
                             SYSTEM_IMAGE_RAW_BYTES=str(packed['raw_bytes']), SYSTEM_IMAGE_ARCHIVE_SHA256=packed['archive_sha256'])
        self.refresh()
        with patch.object(record.subprocess, 'run'):
            result = self.prepare()
        self.assertEqual(result['image_asset'], record.image_name('2.0.0'))
        self.assertEqual((self.output/result['image_asset']).read_bytes(), (self.build/'firmware'/packed['filename']).read_bytes())
        self.output = self.root/'bad-output'
        self.manifest['SYSTEM_IMAGE_RAW_SHA256'] = '0'*64
        self.refresh()
        with patch.object(record.subprocess, 'run'), self.assertRaisesRegex(ValueError, 'pinned build record'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_incomplete_dirty_wrong_platform_tag_and_missing_baseline_rejected(self):
        for key, value in [('PRODUCT_DIRTY','1'), ('PRODUCT_TRACKED_DIRTY','1'),
                           ('TARGET_SOC','rk3576'), ('SYSTEM_TAG','v2.0.0'), ('APPLICATION_UPDATES','0')]:
            old = self.manifest[key]; self.manifest[key] = value; self.refresh()
            with self.subTest(key=key), self.assertRaises(ValueError): self.prepare()
            self.assertFalse(self.output.exists()); self.manifest[key] = old
        self.refresh(); (self.build/'BUILD-INCOMPLETE').touch()
        with self.assertRaisesRegex(ValueError, 'incomplete'): self.prepare()

    def test_new_manifest_wrong_product_filename_is_rejected(self):
        self.manifest['SYSTEM_IMAGE_FILE']='SmartDesktopMimi-SystemImage-v99.0.0.img.gz'
        self.refresh()
        with self.assertRaisesRegex(ValueError,'name/version mismatch'):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_changed_image_and_external_pin_are_rejected(self):
        (self.build/'firmware/a133_linux_b6_uart0.img').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'readback'): self.prepare()
        self.refresh(); self.plan['build_manifest_sha256'] = '0'*64; record.write(self.plan_path,self.plan)
        with self.assertRaisesRegex(ValueError, 'external pin'): self.prepare()

    def test_private_inventory_fields_are_not_exported(self):
        sdk = record.load(self.root/'sdk-components.json'); sdk['components'][0]['patch'] = 'private'
        record.write(self.root/'sdk-components.json',sdk)
        self.plan['inputs']['sdk-components.json']['sha256'] = record.digest(self.root/'sdk-components.json')
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'private/unknown'): self.prepare()

    def test_sdk_inventory_from_another_build_is_rejected(self):
        self.manifest['SDK_MANIFEST_SHA256'] = '0'*64; self.refresh()
        with self.assertRaisesRegex(ValueError, 'SDK inventory does not match'): self.prepare()

    def test_factory_mismatch_and_bad_signature_leave_no_output(self):
        self.seal['factory_commit'] = '0'*40; record.write(self.build/'metadata/baseline-lock.json',self.seal)
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'baseline mismatch'): self.prepare()
        self.seal['factory_commit'] = 'a'*40; record.write(self.build/'metadata/baseline-lock.json',self.seal)
        self.refresh()
        with patch.object(record.subprocess, 'run', side_effect=subprocess.CalledProcessError(1,'checker')):
            with self.assertRaises(subprocess.CalledProcessError): self.prepare()
        self.assertFalse(self.output.exists())

    def test_seal_missing_attachment_symlink_and_duplicate_checksum_rejected(self):
        path = self.build/'metadata/factory-manifest.sig'; original = path.read_bytes()
        path.write_text('other signature'); self.refresh()
        with self.assertRaisesRegex(ValueError, 'sealed'): self.prepare()
        path.unlink()
        with self.assertRaisesRegex(ValueError, 'missing regular'): self.prepare()
        elsewhere = self.root/'signature'; elsewhere.write_bytes(original); path.symlink_to(elsewhere)
        with self.assertRaisesRegex(ValueError, 'linked'): self.prepare()
        path.unlink(); path.write_bytes(original); self.refresh()
        sums = self.build/'SHA256SUMS'; sums.write_text(sums.read_text()*2)
        with self.assertRaisesRegex(ValueError, 'duplicate'): self.prepare()

    def test_midflight_input_change_and_existing_output_rejected(self):
        def mutate(*args,**kwargs): (self.build/'firmware/a133_linux_b6_uart0.img').write_text('changed')
        with patch.object(record.subprocess, 'run', side_effect=mutate):
            with self.assertRaisesRegex(ValueError, 'raw image differs'): self.prepare()
        self.assertFalse(self.output.exists()); self.output.mkdir()
        with self.assertRaisesRegex(ValueError, 'already exists'): self.prepare()


if __name__ == '__main__': unittest.main()
