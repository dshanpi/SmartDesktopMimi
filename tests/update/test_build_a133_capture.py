"""Keep the SDK capture source and dependency boundary explicit."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tests.sdk_source import capture_source

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('capture_entry', ROOT / 'scripts/build_a133_capture.py')
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


class CaptureEntryTests(unittest.TestCase):
    def fixture(self, root):
        sdk = root / 'sdk'
        (sdk / '.repo').mkdir(parents=True)
        component = sdk / entry.PREFIX
        (component / 'code').mkdir(parents=True)
        (component / 'code/main.cpp').write_text('int main(){}')
        (component / 'build_capture.py').write_text('raise RuntimeError("must not execute")')
        files = {p.relative_to(component).as_posix(): entry.sha(p) for p in component.rglob('*') if p.is_file()}
        lock = component / 'source-lock.json'
        lock.write_text(json.dumps(dict(files=files)))
        return sdk, component, entry.sha(lock)

    def test_changed_lock_source_helper_and_alias_rejected(self):
        for item in ['source-lock.json', 'code/main.cpp', 'build_capture.py', 'alias']:
            with self.subTest(item=item), tempfile.TemporaryDirectory() as d:
                root = Path(d).resolve()
                sdk, component, digest = self.fixture(root)
                with patch.object(entry, 'LOCK_SHA256', digest):
                    entry.validate(str(sdk))
                    if item == 'alias':
                        file = component / 'code/main.cpp'
                        external = root / 'same.cpp'
                        file.rename(external)
                        file.symlink_to(external)
                    else:
                        (component / item).write_text('changed')
                    with self.assertRaises(ValueError):
                        entry.validate(str(sdk))

    def test_changed_helper_not_executed_existing_artifact_untouched(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            sdk, component, digest = self.fixture(root)
            output = root / 'old-output'
            output.mkdir()
            old = output / 'aitvbox-kvm-video'
            old.write_bytes(b'old valid artifact')
            (component / 'build_capture.py').write_text('print("tampered")')
            with patch.object(entry, 'LOCK_SHA256', digest), patch.object(entry.subprocess, 'run') as invoke:
                with self.assertRaises(ValueError):
                    entry.run(argparse.Namespace(sdk=str(sdk), output=str(output), check=False))
                invoke.assert_not_called()
                self.assertEqual(old.read_bytes(), b'old valid artifact')

    def test_missing_component_has_no_old_source_fallback(self):
        with tempfile.TemporaryDirectory() as d:
            sdk = Path(d).resolve()
            (sdk / '.repo').mkdir()
            with self.assertRaisesRegex(ValueError, 'missing or changed'):
                entry.validate(str(sdk))


class ActualSdkCaptureAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        component = capture_source('main.cpp').parent.parent
        helper = component / 'build_capture.py'
        lock = json.loads((component / 'source-lock.json').read_text())
        if entry.sha(helper) != lock['files']['build_capture.py']:
            raise ValueError('fixed SDK capture helper changed')
        spec = importlib.util.spec_from_file_location('actual_sdk_capture', helper)
        cls.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.helper)

    def test_old_linker_forms_and_scripts_are_all_pinned(self):
        with tempfile.TemporaryDirectory() as d:
            sdk = Path(d).resolve() / 'sdk'
            (sdk / 'lib').mkdir(parents=True)
            for name in ['library.so.1','libc.so','archive.a','crt.o']:
                (sdk / 'lib' / name).write_text('fixed '+name)
            (sdk / 'lib/library.so').symlink_to('library.so.1')
            output = sdk.parent / 'output'
            trace = '\n'.join([
                '-lfoo ('+str(sdk / 'lib/library.so')+')',
                'attempt to open '+str(sdk / 'lib/libc.so')+' succeeded',
                '('+str(sdk / 'lib/archive.a')+')elf-init.oS',
                str(sdk / 'lib/crt.o'),
                '/lib/libfoo.so.1 ('+str(sdk / 'lib/library.so.1')+')',
                str(output / 'video-obj/main.o'),
                str(sdk / 'gcc')+' -o output',
            ])
            files, aliases = self.helper.link_inputs(trace,sdk,output)
            self.assertEqual(set(files),{'lib/library.so.1','lib/libc.so','lib/archive.a','lib/crt.o'})
            self.assertEqual(aliases['lib/library.so'],'lib/library.so.1')
            host = sdk.parent / 'host.so'
            host.write_text('untrusted')
            with self.assertRaises(ValueError):
                self.helper.link_inputs(trace+'\n-lbad ('+str(host)+')',sdk,output)

    def test_make_input_metacharacters_rejected(self):
        for name in ['sdk;touch-bad','sdk$(bad)','sdk`bad`','sdk\nother','sdk(space)']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.helper.safe_root(Path('/safe') / name)

    def test_generated_dependency_path_cannot_hide_sdk_escape(self):
        # Only the single expected patched source is exempted from input audit.
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            output, code, sdk = root/'output', root/'code', root/'sdk'
            (output/'video-obj').mkdir(parents=True)
            code.mkdir();sdk.mkdir()
            foreign = root/'foreign.cpp'
            foreign.write_text('untrusted')
            (output/'video-obj/main.d').write_text(str(output/'video-obj/main.o')+': '+str(foreign)+'\n')
            spec=importlib.util.spec_from_file_location('audit_fixture',ROOT/'tools/update/platform_provider.py')
            audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(audit)
            with self.assertRaisesRegex(ValueError,'unclassified compiler dependency'):
                self.helper.compile_inputs(output,code,sdk,audit)


if __name__ == '__main__':
    unittest.main()
