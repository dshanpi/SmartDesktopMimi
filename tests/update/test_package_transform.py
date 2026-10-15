"""SYSTEM070 actual SDK packaging and historical lock compatibility."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import lock_product_sources as p
from test_product_lock import lock_fixture
import test_product_provenance as provenance


class PackageTransformContractTests(unittest.TestCase):
    def test_v2_requires_exact_pipeline_while_v1_stays_legacy(self):
        legacy = lock_fixture()
        p.validate(legacy)
        for field in ('algorithm', 'strip_arguments', 'schema', 'missing', 'tool', 'resolved', 'extra', 'strip_digest'):
            with self.subTest(field=field):
                value = copy.deepcopy(legacy)
                value.update(schema_version=2, package_transform=copy.deepcopy(p.PACKAGE_TRANSFORM))
                p.validate(value)
                if field == 'algorithm': value['package_transform']['algorithm'] = 'strip-only'
                elif field == 'strip_arguments': value['package_transform']['strip_arguments'] = []
                elif field == 'schema': value['package_transform']['schema'] = True
                elif field == 'missing': del value['package_transform']
                elif field == 'tool': value['package_transform']['inputs']['patchelf']['sha256'] = '0'*64
                elif field == 'resolved': value['package_transform']['inputs']['rstrip']['resolved'] = '../escape'
                elif field == 'extra': value['package_transform']['allow_observed_hash'] = True
                else: value['strip_tool_sha256'] = '0'*64
                with self.assertRaises(ValueError): p.validate(value)
        legacy['package_transform'] = copy.deepcopy(p.PACKAGE_TRANSFORM)
        with self.assertRaises(ValueError): p.validate(legacy)

    def test_missing_tools_refused_before_program_is_read(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises((ValueError, OSError)):
                p.package_program(Path(directory), Path(directory)/'missing-program')

    def test_v2_is_bound_into_both_system_release_provenance_chains(self):
        for edition in ('public', 'private'):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as directory:
                fixture = provenance.ProductProvenanceTests()
                fixture.setup_product(Path(directory), edition)
                fixture.product.update(schema_version=2, package_transform=copy.deepcopy(p.PACKAGE_TRANSFORM))
                fixture.refresh();fixture.check()
                fixture.product['package_transform']['inputs']['patchelf']['sha256'] = 'f'*64
                fixture.refresh()
                with self.assertRaisesRegex(ValueError, 'transformation provenance'):fixture.check()


@unittest.skipUnless(os.environ.get('MIMI_PACKAGE_TRANSFORM_FIXTURES'), 'requires actual saved A133 SDK/programs')
class ActualSDKPackageTransformTests(unittest.TestCase):
    def setUp(self):
        self.delivery = Path(os.environ['MIMI_PACKAGE_TRANSFORM_FIXTURES']).resolve()
        self.sdk = self.delivery/'sdk-restored'

    def test_actual_both_editions_and_r2_rootfs_match(self):
        raw = {
            ('public', 'lv_port_linux/lv_backend'): '0075ec883ee892327272146f42a7e2e31b8cadf9049291c826ce8b3ce62e0111',
            ('private', 'lv_port_linux/lv_backend'): '8a8b3a45cc8eb17f212288b149157e529891e7fd045dfe2a7c7979191514c3f3',
            ('private', 'tuyaopen/your_chat_bot_QIO_1.0.1.bin'): 'fd96df76df02372fe5122bfd94ed11921531a3b3d41ae2ebf9802a9221cdcf59'}
        for (edition, name), expected in raw.items():
            with self.subTest(edition=edition, name=name):
                source = self.delivery/('edition066-build-proof-'+edition)/'build'/name
                before = source.read_bytes()
                self.assertEqual(hashlib.sha256(before).hexdigest(), expected)
                actual = p.package_program(self.sdk, source)
                self.assertEqual(source.read_bytes(), before)
                self.assertEqual(actual['mode'], 0o755)
                self.assertGreater(actual['bytes'], 0)
                if edition == 'public':
                    real = self.delivery/'system070-inputs/backend-r2-readback.bin'
                    self.assertEqual(actual['sha256'], hashlib.sha256(real.read_bytes()).hexdigest())
                    self.assertEqual(actual['bytes'], real.stat().st_size)
                    plain = self.delivery/'system070-inputs/backend-r2-expected-strip.bin'
                    self.assertNotEqual(actual['sha256'], hashlib.sha256(plain.read_bytes()).hexdigest())

    def test_corrupted_or_aliased_sdk_input_refused(self):
        # Copy only the three pinned transformation tools, not the SDK/cache.
        with tempfile.TemporaryDirectory() as directory:
            sdk = Path(directory).resolve()
            for record in p.PACKAGE_TRANSFORM['inputs'].values():
                target = sdk/record['resolved'];target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes((self.sdk/record['path']).read_bytes());target.chmod(0o755)
                link = sdk/record['path']
                if link != target:
                    link.parent.mkdir(parents=True, exist_ok=True);link.symlink_to(target)
            p.package_tools(sdk)
            script = sdk/p.PACKAGE_TRANSFORM['inputs']['rstrip']['path']
            saved = script.read_bytes();script.write_bytes(saved+b'\n# changed\n')
            with self.assertRaisesRegex(ValueError, 'transformation input differs'):p.package_tools(sdk)
            script.write_bytes(saved)
            target = sdk/p.PACKAGE_TRANSFORM['inputs']['patchelf']['resolved']
            alias = target.with_name('alias');alias.write_bytes(target.read_bytes())
            target.unlink();target.symlink_to(alias)
            with self.assertRaisesRegex(ValueError, 'transformation input differs'):p.package_tools(sdk)

    def test_non_elf_or_wrong_architecture_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory)/'program'
            for content in (b'not ELF', b'\x7fELF\x02\x01'+bytes(58)):
                source.write_bytes(content)
                with self.assertRaisesRegex(ValueError, 'AArch64 ELF'):p.package_program(self.sdk, source)

    def test_saved_schema1_locks_remain_readable(self):
        for edition in ('public', 'private'):
            path=self.delivery/('edition067-complete-'+edition)/'platform-product-lock.json'
            value=json.loads(path.read_text())
            self.assertEqual(value['schema_version'],1)
            p.validate(value)

    def test_script_zero_cannot_hide_missing_transform_or_tool_failure(self):
        source = self.delivery/'edition066-build-proof-public/build/lv_port_linux/lv_backend'
        real_run = subprocess.run
        def masked_noop(command, **kwargs):
            if command[0] == '/bin/bash':
                return subprocess.CompletedProcess(command, 0, stdout=b'', stderr=b'')
            return real_run(command, **kwargs)
        with mock.patch.object(p.subprocess, 'run', side_effect=masked_noop):
            with self.assertRaisesRegex(ValueError, 'strict transformation replay'):p.package_program(self.sdk, source)
        def broken_strip(command, **kwargs):
            if command[0].endswith('strip'):
                raise subprocess.CalledProcessError(23, command, stderr=b'injected tool failure')
            return real_run(command, **kwargs)
        with mock.patch.object(p.subprocess, 'run', side_effect=broken_strip):
            with self.assertRaises(subprocess.CalledProcessError):p.package_program(self.sdk, source)


if __name__ == '__main__': unittest.main()
