"""Stale/private selection must fail before clean, SDK bootstrap or collection."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import tuya_pairing_sources as t


class TuyaPairingSelectionTests(unittest.TestCase):
    def fixture(self, directory):
        product = directory / 'product'
        product.mkdir()
        with patch.object(t, 'ROOT', ROOT):
            selected, _ = t.selection('public')
        # Minimal real product inputs, no SDK, cloud or runtime credential files.
        for source in selected.values():
            target = product / source.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        scripts = product / 'scripts'
        scripts.mkdir()
        for name in ('tuya_pairing_sources.py', 'build_a133_provider.py', 'prepare_tuyaopen_build_root.sh', 'lib.sh'):
            shutil.copyfile(ROOT / 'scripts' / name, scripts / name)
        source_root = product / 'third_party/TuyaOpen'
        for name in ('tos.py', 'export.sh'):
            (source_root / name).touch()
        (source_root / 'platform/LINUX/tuyaos_adapter').mkdir(parents=True)
        (source_root / '.tools/python').mkdir(parents=True)
        (product / 'vendor/tuyaopen-a133-b6-libs').mkdir(parents=True)
        readme = product / 'integrations/tuyaopen/README_A133_B6.md'
        readme.touch()
        build = directory / 'staged'
        build.mkdir()
        (build / 'tos.py').touch()
        private = directory / 'private'
        private.mkdir()
        files = {}
        for relative in ('adapters/tuya/mimi_device_pairing.c', 'adapters/tuya/mimi_tuya_license_provider.h'):
            source = private / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text('/* fake proprietary source; no credentials */')
            source.chmod(0o644)
            files[relative] = dict(sha256=t.sha(source), bytes=source.stat().st_size, git_mode='100644')
        lock = private / t.TUYA_LOCK
        lock.write_text(json.dumps(dict(schema=1, component='tuya-device-pairing', api_version=1,
                                       interface_sha256=t.ABI_SHA, files=files)))
        return product, build, private, lock

    def test_stage_switch_and_verify_against_explicit_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            product, build, private, lock = self.fixture(Path(directory).resolve())
            with patch.object(t, 'ROOT', product), patch.object(t, 'TUYA_LOCK_HASH', t.sha(lock)):
                t.stage(str(build), 'private', str(private))
                with self.assertRaisesRegex(ValueError, 'stale'):
                    t.verify(str(build), 'public')
                t.stage(str(build), 'public')
                result = t.verify(str(build), 'public')
                self.assertIsNone(result['private_source_lock_sha256'])
                self.assertEqual((build / t.APP / 'src/mimi_device_pairing.c').read_bytes(),
                                 (product / 'integrations/tuyaopen/pairing/mimi_device_pairing_stub.c').read_bytes())
                with self.assertRaisesRegex(ValueError, 'stale'):
                    t.verify(str(build), 'private', str(private))

    def test_stale_source_receipt_or_duplicate_adapter_rejected(self):
        for changed in ('main', 'runtime', 'receipt', 'extra', 'alias'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as directory:
                product, build, private, lock = self.fixture(Path(directory).resolve())
                with patch.object(t, 'ROOT', product):
                    t.stage(str(build), 'public')
                    runtime = build / t.APP / 'src/mimi_device_pairing.c'
                    if changed == 'main':
                        (build / t.APP / 'src/tuya_main.c').write_text('old unguarded main')
                    elif changed == 'runtime':
                        runtime.write_text('private leftover')
                    elif changed == 'receipt':
                        (build / t.RECEIPT).write_text('{}')
                    elif changed == 'extra':
                        runtime.with_name('mimi_device_pairing_old.c').touch()
                    else:
                        runtime.unlink()
                        runtime.symlink_to(product / 'integrations/tuyaopen/pairing/mimi_device_pairing_stub.c')
                    with self.assertRaises(ValueError):
                        t.verify(str(build), 'public')

    def test_changed_private_or_unguarded_main_cannot_stage(self):
        for changed in ('lock', 'runtime', 'main'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as directory:
                product, build, private, lock = self.fixture(Path(directory).resolve())
                with patch.object(t, 'ROOT', product), patch.object(t, 'TUYA_LOCK_HASH', t.sha(lock)):
                    target = {'lock': lock, 'runtime': private / 'adapters/tuya/mimi_device_pairing.c',
                              'main': product / 'third_party/TuyaOpen' / t.APP / 'src/tuya_main.c'}[changed]
                    target.write_text('tampered')
                    with self.assertRaises(ValueError):
                        t.stage(str(build), 'private', str(private))
                    self.assertFalse((build / t.RECEIPT).exists())

    def test_public_private_input_and_overlapping_source_rejected(self):
        with self.assertRaisesRegex(ValueError, 'forbids'):
            t.selection('public', '/missing-private')
        with self.assertRaisesRegex(ValueError, 'explicit private'):
            t.selection('private')
        for protected in (ROOT, ROOT.parent, ROOT / 'third_party/TuyaOpen'):
            with self.assertRaisesRegex(ValueError, 'overwrite source'):
                t.staged_root(str(protected))

    def test_prepare_missing_private_refuses_before_clean(self):
        with tempfile.TemporaryDirectory() as directory:
            product, build, private, lock = self.fixture(Path(directory).resolve())
            sentinel = build / 'old-program'
            sentinel.write_bytes(b'old candidate preserved')
            env = dict(os.environ, MIMI_PRODUCT_EDITION='private', MIMI_PRIVATE_PRODUCT_ROOT='',
                       TUYAOPEN_TOOLS_ROOT=str(product / 'third_party/TuyaOpen/.tools'),
                       AITVBOX_TUYA_VENDOR_ROOT=str(product / 'vendor/tuyaopen-a133-b6-libs'))
            result = subprocess.run(['bash', str(product / 'scripts/prepare_tuyaopen_build_root.sh'),
                                     '--clean', '--no-export', '--build-root', str(build)],
                                    capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('requires explicit private product root', result.stderr)
            self.assertEqual(sentinel.read_bytes(), b'old candidate preserved')

    def test_direct_builder_refuses_stale_tree_before_venv_or_sdk(self):
        with tempfile.TemporaryDirectory() as directory:
            product, build, private, lock = self.fixture(Path(directory).resolve())
            env = dict(os.environ, MIMI_PRODUCT_EDITION='public', MIMI_PRODUCT_SOURCE_ROOT=str(product),
                       MIMI_PRIVATE_PRODUCT_ROOT='', VENV_ROOT=str(build / 'missing-venv'))
            result = subprocess.run(['bash', str(product / 'integrations/tuyaopen/build_a133.sh'),
                                     str(build), '/missing-sdk'], capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing/aliased Tuya pairing receipt', result.stderr)
            self.assertNotIn('rebuilding via', result.stdout)
            self.assertFalse((build / '.cache').exists())


if __name__ == '__main__':
    unittest.main()
