import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/update'))
import runtime_permissions as gate


class RuntimePermissionsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tree = self.root / 'root'
        self.tree.mkdir(mode=0o755)
        for name, required in gate.REQUIRED.items():
            p = self.tree / name.lstrip('/')
            p.parent.mkdir(exist_ok=True, mode=0o755)
            p.write_text('fixture\n')
            p.chmod(0o755 if required & 1 else 0o644)
        (self.tree / 'etc/shadow').write_text('fixture\n')
        (self.tree / 'etc/shadow').chmod(0o600)
        self.image = self.root / 'root.img'
        self.build()

    def build(self):
        with self.image.open('wb') as f:
            f.truncate(8 * 1024 * 1024)
        subprocess.run(['mkfs.ext4', '-q', '-F', '-d', str(self.tree), str(self.image)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # Image owner is controlled explicitly, independent of the test user.
        for path in [self.tree] + list(self.tree.rglob('*')):
            name = '/' + path.relative_to(self.tree).as_posix().strip('.')
            for field in ('uid', 'gid'):
                self.debug('set_inode_field "' + name + '" ' + field + ' 0')

    def debug(self, command):
        subprocess.run(['debugfs', '-w', '-R', command, str(self.image)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def assert_readonly_failure(self, pattern):
        before = hashlib.sha256(self.image.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, pattern):
            gate.verify(self.image)
        self.assertEqual(before, hashlib.sha256(self.image.read_bytes()).hexdigest())

    def test_valid_actual_ext4_is_read_only(self):
        before = hashlib.sha256(self.image.read_bytes()).hexdigest()
        self.assertEqual(len(gate.verify(self.image)), 8)
        self.assertEqual(before, hashlib.sha256(self.image.read_bytes()).hexdigest())

    def test_each_observed_boot_permission_failure(self):
        for path in gate.REQUIRED:
            with self.subTest(path=path):
                self.build()
                self.debug('set_inode_field ' + path + ' mode 0100600')
                self.assert_readonly_failure('nonroot runtime access')

    def test_loader_requires_execute(self):
        self.debug('set_inode_field /lib/ld-linux-aarch64.so.1 mode 0100644')
        self.assert_readonly_failure('nonroot runtime access')

    def test_nonroot_owned_library(self):
        self.debug('set_inode_field /lib/libc.so.6 uid 1000')
        self.assert_readonly_failure('not root-owned')

    def test_inaccessible_parent(self):
        self.debug('set_inode_field /lib mode 040700')
        self.assert_readonly_failure('not traversable')

    def test_world_writable_parent(self):
        self.debug('set_inode_field /etc mode 040777')
        self.assert_readonly_failure('writable by non-owner')

    def test_shadow_is_never_relaxed_with_other_account_files(self):
        for mode in ('0100644', '0100700', '0104600'):
            with self.subTest(mode=mode):
                self.build()
                self.debug('set_inode_field /etc/shadow mode ' + mode)
                self.assert_readonly_failure('shadow must remain private')

    def test_missing_dependency(self):
        self.debug('rm /lib/libgcc_s.so.1')
        self.assert_readonly_failure('missing runtime path')

    def test_safe_relative_and_absolute_image_symlinks(self):
        for absolute in (False, True):
            with self.subTest(absolute=absolute):
                link = self.tree / 'lib/libc.so.6'
                if link.is_symlink():
                    link.unlink()
                else:
                    link.rename(self.tree / 'lib/libc-real.so.6')
                link.symlink_to('/lib/libc-real.so.6' if absolute else 'libc-real.so.6')
                self.build()
                self.assertEqual(gate.verify(self.image)['/lib/libc.so.6']['resolved'],
                                 '/lib/libc-real.so.6')

    def test_link_cycle(self):
        p = self.tree / 'lib/libc.so.6'
        p.unlink()
        p.symlink_to('libc.so.6')
        self.build()
        self.assert_readonly_failure('link cycle')

    @unittest.skipUnless(shutil.which('mksquashfs'), 'squashfs tools unavailable')
    def test_actual_squashfs(self):
        image = self.root / 'root.squashfs'
        subprocess.run(['mksquashfs', str(self.tree), str(image), '-noappend',
                        '-all-root', '-processors', '1'], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.assertEqual(len(gate.verify(image)), 8)

    def test_image_symlink_rejected(self):
        link = self.root / 'alias.img'
        link.symlink_to(self.image)
        with self.assertRaisesRegex(ValueError, 'regular image'):
            gate.verify(link)


if __name__ == '__main__':
    unittest.main()
