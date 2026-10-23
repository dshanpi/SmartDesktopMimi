import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

class PlatformPackageTests(unittest.TestCase):
    def test_actual_install_and_corrupt_service_rejection(self):
        value = os.environ.get('MIMI_EDITION_PROGRAM_FIXTURES')
        if not value:
            self.skipTest('actual A133 service fixtures required')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            product = root / 'product'
            source = Path(value) / 'edition066-build-proof-public/build/platform'
            shutil.copytree(source, product / 'build/platform')
            shutil.copytree(ROOT / 'platform/runtime', product / 'platform/runtime')
            (product / 'scripts').symlink_to(ROOT / 'scripts', target_is_directory=True)
            top = root / 'top'; (top / 'include').mkdir(parents=True)
            (top / 'rules.mk').write_text('INCLUDE_DIR:=' + str(top / 'include') + '\nINSTALL_DIR:=mkdir -p\nINSTALL_BIN:=install -m 0755\n')
            (top / 'include/package.mk').write_text('')
            stage = root / 'rootfs'
            recipe = root / 'install.mk'
            recipe.write_text('TOPDIR:=' + str(top) + '\nAITVBOX_PRODUCT_BUILD:=' + str(product) +
                '\ninclude ' + str(ROOT / 'packaging/aitvbox-platform/Makefile') +
                '\nall:\n\t$(call Package/aitvbox-platform/install,' + str(stage) + ')\n')
            def install():
                return subprocess.run(['make', '--no-print-directory', '-f', str(recipe), 'all'],
                    cwd=ROOT / 'packaging/aitvbox-platform', capture_output=True, text=True)
            result = install()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            expected = {}
            for name in ('mcpd', 'agentd', 'controld', 'appd', 'app-policy'):
                expected['usr/bin/aitvbox-' + name] = product / ('build/platform/aitvbox-' + name)
            for name in ('appctl', 'agentctl', 'adminctl'):
                expected['usr/bin/aitvbox-' + name] = product / ('platform/runtime/aitvbox-' + name)
            for name in ('control', 'apps'):
                expected['etc/init.d/aitvbox-' + name] = ROOT / ('packaging/aitvbox-platform/files/aitvbox-' + name + '.init')
            self.assertEqual({f.relative_to(stage).as_posix() for f in stage.rglob('*') if f.is_file()}, set(expected))
            for name, original in expected.items():
                installed = stage / name
                self.assertFalse(installed.is_symlink())
                self.assertEqual(installed.read_bytes(), original.read_bytes(), name)
                self.assertEqual(installed.stat().st_mode & 0o777, 0o755, name)
            shutil.rmtree(stage)
            with (product / 'build/platform/aitvbox-appd').open('ab') as f:
                f.write(b'corrupt fixture')
            result = install()
            self.assertNotEqual(result.returncode, 0, 'corrupt service accepted by independent package')
            self.assertFalse(stage.exists(), 'failed gate wrote rootfs')
