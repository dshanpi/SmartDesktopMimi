"""SDK source tampering and output preservation at the HDMI build boundary."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tests.sdk_source import hdmi_source

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hdmi_entry', ROOT / 'scripts/build_a133_hdmi.py')
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


class HdmiEntryTests(unittest.TestCase):
    def fixture(self, root):
        sdk = root / 'sdk'
        (sdk / '.repo').mkdir(parents=True)
        component = sdk / entry.PREFIX
        (component / 'code/src').mkdir(parents=True)
        (component / 'code/src/main.cpp').write_text('int main(){}')
        (component / 'build_hdmi.py').write_text('raise RuntimeError("must not execute")')
        files = {p.relative_to(component).as_posix(): entry.sha(p) for p in component.rglob('*') if p.is_file()}
        lock = component / 'source-lock.json'
        lock.write_text(json.dumps(dict(files=files)))
        return sdk, component, entry.sha(lock)

    def test_changed_lock_source_helper_and_alias_rejected(self):
        for item in ['source-lock.json', 'code/src/main.cpp', 'build_hdmi.py', 'alias']:
            with self.subTest(item=item), tempfile.TemporaryDirectory() as d:
                root = Path(d).resolve()
                sdk, component, digest = self.fixture(root)
                with patch.object(entry, 'LOCK_SHA256', digest):
                    entry.validate(str(sdk))
                    if item == 'alias':
                        file = component / 'code/src/main.cpp'
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
            old = output / 'hdmi_preview'
            old.write_bytes(b'old valid artifact')
            (component / 'build_hdmi.py').write_text('print("tampered")')
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


class ActualSdkHdmiAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = hdmi_source('src/main.cpp')
        component = source.parent.parent.parent
        helper = component / 'build_hdmi.py'
        lock = json.loads((component / 'source-lock.json').read_text())
        if entry.sha(helper) != lock['files']['build_hdmi.py']:
            raise ValueError('fixed SDK HDMI helper changed')
        spec = importlib.util.spec_from_file_location('actual_sdk_hdmi', helper)
        cls.helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.helper)

    def fixture(self, root):
        sdk = root / 'sdk'
        component = root / 'component'
        (component / 'code/src').mkdir(parents=True)
        source = component / 'code/src/main.cpp'
        source.write_text('int a;')
        tool = sdk / 'openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider/build_provider.py'
        tool.parent.mkdir(parents=True)
        tool.write_bytes((ROOT / 'tools/update/platform_provider.py').read_bytes())
        compiler = sdk / 'bin/aarch64-openwrt-linux-g++'
        compiler.parent.mkdir()
        compiler.write_text('not executed')
        header = sdk / 'include/a.h'
        header.parent.mkdir()
        header.write_text('fixed header')
        library = sdk / 'lib/lib.so.real'
        library.parent.mkdir()
        library.write_text('fixed library')
        (sdk / 'lib/lib.so').symlink_to(library.name)
        config = sdk / 'openwrt/openwrt/.config'
        config.write_text('CONFIG_SUNXI_ALLOC_IOMMU=y\n')
        lock = dict(schema_version=1, kind='a133-hdmi-source',
                    shared_audit_tool_sha256=entry.sha(tool), files={'code/src/main.cpp': entry.sha(source)},
                    compiler_relative='bin/aarch64-openwrt-linux-g++', compiler_target='bin/aarch64-openwrt-linux-g++',
                    compiler_sha256=entry.sha(compiler), sdk_headers={'include/a.h': entry.sha(header)},
                    sdk_link_inputs={'lib/lib.so.real': entry.sha(library)},
                    sdk_link_aliases={'lib/lib.so': 'lib/lib.so.real'}, allocator_config={'CONFIG_SUNXI_ALLOC_IOMMU':'y'})
        (component / 'source-lock.json').write_text(json.dumps(lock))
        return sdk, argparse.Namespace(sdk=str(sdk), component=str(component))

    def test_wrong_allocator_rejected_without_compilation(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, args = self.fixture(Path(d).resolve())
            self.helper.load_inputs(args)
            (sdk / 'openwrt/openwrt/.config').write_text('CONFIG_SUNXI_ALLOC_IOMMU=n\n')
            with self.assertRaisesRegex(ValueError, 'allocator configuration changed'):
                self.helper.load_inputs(args)

    def test_same_bytes_at_wrong_library_alias_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, args = self.fixture(Path(d).resolve())
            self.helper.load_inputs(args)
            other = sdk / 'lib/other.so'
            other.write_bytes((sdk / 'lib/lib.so.real').read_bytes())
            alias = sdk / 'lib/lib.so'
            alias.unlink()
            alias.symlink_to(other.name)
            with self.assertRaisesRegex(ValueError, 'linker alias changed'):
                self.helper.load_inputs(args)

    def test_cpp_compiler_alias_cannot_escape_sdk(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, args = self.fixture(Path(d).resolve())
            self.helper.load_inputs(args)
            compiler = sdk / 'bin/aarch64-openwrt-linux-g++'
            other = sdk.parent / 'outside-compiler'
            compiler.rename(other)
            compiler.symlink_to(other)
            with self.assertRaisesRegex(ValueError, 'compiler changed or escaped'):
                self.helper.load_inputs(args)

    def test_link_trace_rejects_external_input_and_ignores_command_echo(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            sdk, args = self.fixture(root)
            self.helper.load_inputs(args)
            output = root / 'output'
            output.mkdir()
            trace = str(sdk / 'bin/aarch64-openwrt-linux-g++') + ' -o target\n' + str(sdk / 'lib/lib.so') + '\n'
            files, aliases = self.helper.link_inputs(trace, sdk, output)
            self.assertEqual(list(files), ['lib/lib.so.real'])
            self.assertEqual(aliases, {'lib/lib.so':'lib/lib.so.real'})
            other = root / 'external.o'
            other.write_bytes(b'host')
            with self.assertRaises(ValueError):
                self.helper.link_inputs(trace + str(other) + '\n', sdk, output)


if __name__ == '__main__':
    unittest.main()
