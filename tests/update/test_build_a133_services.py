"""Reject missing/changed SDK services without executing or replacing outputs."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tests.sdk_source import services_source

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('services_entry', ROOT / 'scripts/build_a133_services.py')
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


class ServicesEntryTests(unittest.TestCase):
    def fixture(self, root):
        sdk = root / 'sdk'
        (sdk / '.repo').mkdir(parents=True)
        component = sdk / entry.PREFIX
        (component / 'code/src').mkdir(parents=True)
        (component / 'code/src/service.c').write_text('int service;')
        (component / 'build_services.py').write_text('raise RuntimeError("must not execute")')
        files = {p.relative_to(component).as_posix(): entry.sha(p) for p in component.rglob('*') if p.is_file()}
        lock = component / 'source-lock.json'
        lock.write_text(json.dumps(dict(files=files)))
        return sdk, component, entry.sha(lock)

    def test_changed_lock_source_helper_and_alias_rejected(self):
        for item in ['source-lock.json', 'code/src/service.c', 'build_services.py', 'alias']:
            with self.subTest(item=item), tempfile.TemporaryDirectory() as d:
                root = Path(d).resolve()
                sdk, component, digest = self.fixture(root)
                with patch.object(entry, 'LOCK_SHA256', digest):
                    entry.validate(str(sdk))
                    if item == 'alias':
                        file = component / 'code/src/service.c'
                        external = root / 'same.c'
                        file.rename(external)
                        file.symlink_to(external)
                    else:
                        (component / item).write_text('changed')
                    with self.assertRaises(ValueError):
                        entry.validate(str(sdk))

    def test_changed_helper_not_executed_and_existing_artifact_untouched(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            sdk, component, digest = self.fixture(root)
            output = root / 'old-output'
            output.mkdir()
            old = output / 'aitvbox-mcpd'
            old.write_bytes(b'old valid artifact')
            (component / 'build_services.py').write_text('print("tampered")')
            with patch.object(entry, 'LOCK_SHA256', digest), patch.object(entry.subprocess, 'run') as invoke:
                with self.assertRaises(ValueError):
                    entry.run(argparse.Namespace(sdk=str(sdk), output=str(output), check=False))
                invoke.assert_not_called()
                self.assertEqual(old.read_bytes(), b'old valid artifact')

    def test_missing_component_has_no_application_source_fallback(self):
        with tempfile.TemporaryDirectory() as d:
            sdk = Path(d).resolve()
            (sdk / '.repo').mkdir()
            with self.assertRaisesRegex(ValueError, 'missing or changed'):
                entry.validate(str(sdk))


class ActualSdkAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        make = services_source('Makefile')  # Explicit not_run if no SDK configured.
        component = make.parent.parent
        lock = json.loads((component / 'source-lock.json').read_text())
        helper = component / 'build_services.py'
        if entry.sha(helper) != lock['files']['build_services.py']:
            raise ValueError('SDK helper differs')
        spec = importlib.util.spec_from_file_location('actual_sdk_services', helper)
        cls.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.helper)

    def fixture(self, root):
        sdk = root / 'sdk'
        component = root / 'component'
        (component / 'code/src').mkdir(parents=True)
        (component / 'code/src/a.c').write_text('int a;')
        tool = sdk / 'openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider/build_provider.py'
        tool.parent.mkdir(parents=True)
        tool.write_bytes((ROOT / 'tools/update/platform_provider.py').read_bytes())
        compiler = sdk / 'bin/aarch64-openwrt-linux-gcc'
        compiler.parent.mkdir()
        compiler.write_text('not executed')
        header = sdk / 'include/a.h'
        header.parent.mkdir()
        header.write_text('fixed header')
        library = sdk / 'lib/lib.so.real'
        library.parent.mkdir()
        library.write_text('fixed library')
        (sdk / 'lib/lib.so').symlink_to(library.name)
        lock = dict(schema_version=1, kind='a133-platform-services-source',
                    shared_audit_tool_sha256=entry.sha(tool), files={'code/src/a.c': entry.sha(component / 'code/src/a.c')},
                    compiler_relative='bin/aarch64-openwrt-linux-gcc', compiler_target='bin/aarch64-openwrt-linux-gcc',
                    compiler_sha256=entry.sha(compiler), sdk_headers={'include/a.h': entry.sha(header)},
                    sdk_libraries={'lib/lib.so.real': entry.sha(library)},
                    sdk_library_aliases={'lib/lib.so': 'lib/lib.so.real'})
        (component / 'source-lock.json').write_text(json.dumps(lock))
        return sdk, argparse.Namespace(sdk=str(sdk), component=str(component))

    def test_same_bytes_at_wrong_linker_target_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, args = self.fixture(Path(d).resolve())
            self.helper.load_inputs(args)
            other = sdk / 'lib/other.so'
            other.write_bytes((sdk / 'lib/lib.so.real').read_bytes())
            alias = sdk / 'lib/lib.so'
            alias.unlink()
            alias.symlink_to(other.name)
            with self.assertRaisesRegex(ValueError, 'library alias changed'):
                self.helper.load_inputs(args)

    def test_changed_sdk_header_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, args = self.fixture(Path(d).resolve())
            self.helper.load_inputs(args)
            (sdk / 'include/a.h').write_text('changed header')
            with self.assertRaisesRegex(ValueError, 'SDK service input changed'):
                self.helper.load_inputs(args)


if __name__ == '__main__':
    unittest.main()
