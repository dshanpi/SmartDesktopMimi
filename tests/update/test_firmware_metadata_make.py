import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FirmwareMetadataMakeTests(unittest.TestCase):
    def execute_metadata(self, shadow_only=False):
        source = (ROOT / 'scripts/build_firmware.sh').read_text()
        line = next(line.strip() for line in source.splitlines()
                    if 'prepare-tmpinfo )' in line)
        invocation = line.split(')', 1)[0].strip()
        if shadow_only:
            if invocation.startswith('command '):
                invocation = invocation[len('command '):]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'openwrt/openwrt').mkdir(parents=True)
            binary = root / 'bin'; binary.mkdir()
            executable = binary / 'make'
            executable.write_text('#!/bin/bash\nprintf "%s\\n" "$@" > "$RECEIPT"\nexit "${MAKE_EXIT:-0}"\n')
            executable.chmod(0o755)
            receipt = root / 'receipt'
            env = dict(os.environ, PATH=str(binary) + ':' + os.environ['PATH'],
                       SDK=str(root), RECEIPT=str(receipt))
            script = 'make() { echo unexpected-sdk-build >&2; return 73; };\n' + invocation
            result = subprocess.run(['bash', '-c', script], env=env,
                                    capture_output=True, text=True)
            args = receipt.read_text().splitlines() if receipt.exists() else None
            return result, args, str(root / 'openwrt/openwrt')

    def test_script_metadata_calls_external_make_under_shadow_function(self):
        result, args, path = self.execute_metadata()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(args, ['-C', path, '-j1', 'prepare-tmpinfo'])
        self.assertNotIn('unexpected-sdk-build', result.stderr)

    def test_original_unqualified_invocation_reproduces_sdk_shadow_failure(self):
        result, args, _ = self.execute_metadata(shadow_only=True)
        self.assertEqual(result.returncode, 73)
        self.assertIsNone(args)
        self.assertIn('unexpected-sdk-build', result.stderr)

    def test_only_fixed_generated_product_cache_entries_are_invalidated(self):
        source = (ROOT / 'scripts/build_firmware.sh').read_text()
        start = source.index('for product_package in ')
        end = source.index('\ndone', start) + len('\ndone')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / 'out/a133/b6/openwrt/tmp/info'
            cache.mkdir(parents=True)
            packages = ['aitvbox-ipkvm', 'aitvbox-platform', 'aitvbox-suite',
                        'aitvbox-mimi-update', 'aitvbox-usb-hid']
            expected = [cache / ('.packageinfo-subpackage_allwinner_custom_' + p)
                        for p in packages]
            for path in expected: path.write_text('cached empty dump')
            retained = cache / '.packageinfo-subpackage_allwinner_custom_unrelated'
            retained.write_text('other package')
            tracked_source = root / 'source/Makefile'
            tracked_source.parent.mkdir(); tracked_source.write_text('source')
            env = dict(os.environ, SDK=str(root))
            result = subprocess.run(['bash', '-c', source[start:end]], env=env,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(all(not path.exists() for path in expected))
            self.assertEqual(retained.read_text(), 'other package')
            self.assertEqual(tracked_source.read_text(), 'source')


if __name__ == '__main__':
    unittest.main()
