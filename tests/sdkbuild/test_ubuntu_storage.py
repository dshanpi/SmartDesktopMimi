"""Internal backend tests with real signed installs; no capability is exposed."""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import select
import stat
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


F = module('storage_installer_fixture', ROOT / 'tests/sdkbuild/test_ubuntu_apps_installer.py')
S = module('ubuntu_storage', ROOT / 'services/mimi-ubuntu-apps/storage.py')


@unittest.skipUnless(os.geteuid() == 0, 'requires isolated root filesystem')
class StorageTests(unittest.TestCase):
    setUpClass = classmethod(F.InstallerTests.setUpClass.__func__)
    tearDownClass = classmethod(F.InstallerTests.tearDownClass.__func__)
    tearDown = F.InstallerTests.tearDown
    package = F.InstallerTests.package
    run_killed = F.InstallerTests.run_killed

    def setUp(self):
        F.InstallerTests.setUp(self)
        self.i.install(self.package())
        self.storage = S.Storage(self.i)
        self.pin = self.identity()
        self.state = self.target / 'data' / S.STATE

    def identity(self, target=None):
        target = target or self.target
        return (target.name, (target / F.M.MARKER).read_text().strip(), self.i.installed_digest(target))

    def write(self, key='note', value='persistent text'):
        return self.storage.operate(self.pin, {'op': 'write', 'key': key, 'value': value})

    def read(self, key='note'):
        return self.storage.operate(self.pin, {'op': 'read', 'key': key})

    def seed_raw(self, raw):
        self.state.write_bytes(raw)
        self.state.chmod(0o600)

    def test_restart_utf8_empty_and_missing(self):
        self.assertEqual(self.read(), {'found': False})
        self.write(value='中文 🌱\nsecond line')
        self.storage = S.Storage(F.M.Installer(self.config))
        self.assertEqual(self.read(), {'found': True, 'value': '中文 🌱\nsecond line'})
        self.write(value='')
        self.assertEqual(self.read(), {'found': True, 'value': ''})
        self.assertEqual(stat.S_IMODE(self.state.stat().st_mode), 0o600)

    def test_keys_are_not_paths(self):
        self.write('..', 'literal key')
        self.assertEqual(self.read('..')['value'], 'literal key')
        for key in ('../outside', '/root', '', 'x' * 65, 'a\0b', '你好'):
            with self.subTest(key=key), self.assertRaises(ValueError): self.write(key)

    def test_strict_arguments_and_identity(self):
        bad = ({'op': 'delete', 'key': 'a'}, {'op': 'read', 'key': 'a', 'id': 'other'},
               {'op': 'write', 'key': 'a'}, {'op': 'read', 'key': True}, [])
        for args in bad:
            with self.subTest(args=args), self.assertRaises(ValueError): self.storage.operate(self.pin, args)
        for pin in (list(self.pin), ('../escape', *self.pin[1:]), (self.pin[0], 'bad', self.pin[2])):
            with self.assertRaises(ValueError): self.storage.operate(pin, {'op': 'read', 'key': 'a'})

    def test_value_and_encoded_transport_limits(self):
        self.write(value='x' * 3498)
        for value in ('x' * 4097, '\n' * 2000, 'x\0y', 1, None, '\ud800'):
            with self.subTest(value_type=type(value).__name__), self.assertRaises((ValueError, UnicodeError)):
                self.write(value=value)
        self.assertEqual(self.read()['value'], 'x' * 3498)

    def test_key_count_quota_preserves_last_state(self):
        for n in range(S.MAX_KEYS): self.write(str(n), 'value')
        before = self.state.read_bytes()
        with self.assertRaisesRegex(ValueError, 'quota'): self.write('extra')
        self.assertEqual(self.state.read_bytes(), before)
        self.write('0', 'replace')
        self.assertEqual(self.read('0')['value'], 'replace')

    def test_serialized_total_quota(self):
        for n in range(18): self.write(str(n), 'x' * 3498)
        before = self.state.read_bytes()
        with self.assertRaisesRegex(ValueError, 'quota'): self.write('more', 'x' * 3498)
        self.assertEqual(self.state.read_bytes(), before)

    def test_corruption_is_not_overwritten(self):
        bad = (b'{', b'{"schema":true,"values":{}}', b'{"schema":1,"values":{},"schema":1}',
               b'{"schema":1,"values":{"x":"a","x":"b"}}', b'{"schema":1,"values":{"x":1}}',
               b'{"schema":1,"values":{},"extra":0}', b'\xff', b' ' * 65537)
        for raw in bad:
            self.seed_raw(raw)
            with self.subTest(raw=raw[:30]), self.assertRaises((ValueError, UnicodeError)): self.write()
            self.assertEqual(self.state.read_bytes(), raw)

    def test_symlink_state_refused(self):
        outside = self.r / 'outside'; outside.write_text('keep')
        self.state.symlink_to(outside)
        with self.assertRaises(ValueError): self.write()
        self.assertEqual(outside.read_text(), 'keep')

    def test_hardlink_state_refused(self):
        outside = self.r / 'outside'; outside.write_text('keep'); outside.chmod(0o600)
        os.link(outside, self.state)
        with self.assertRaises(ValueError): self.write()
        self.assertEqual(outside.read_text(), 'keep')

    def test_fifo_state_refused_without_blocking(self):
        os.mkfifo(self.state, 0o600)
        with self.assertRaises(ValueError): self.read()

    def test_state_must_be_private(self):
        self.write(); self.state.chmod(0o644)
        with self.assertRaises(ValueError): self.read()

    def test_directory_must_be_private(self):
        (self.target / 'data').chmod(0o755)
        with self.assertRaises(ValueError): self.write()

    def test_other_data_preserved(self):
        p = self.target / 'data/media.bin'; p.write_bytes(bytes(range(256)))
        digest = hashlib.sha256(p.read_bytes()).hexdigest()
        self.write(); self.read()
        self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(), digest)

    def test_publisher_and_installation_pin(self):
        for pin in ((self.pin[0], '0' * 64, self.pin[2]), (self.pin[0], self.pin[1], '0' * 64)):
            with self.assertRaisesRegex(ValueError, 'changed'): self.storage.operate(pin, {'op': 'write', 'key': 'a', 'value': 'bad'})
        self.assertFalse(self.state.exists())

    def test_real_install_upgrade_retains_data_and_invalidates_pin(self):
        self.write(value='survives signed upgrade')
        self.i.install(self.package('1.1.0'))
        with self.assertRaisesRegex(ValueError, 'changed'): self.read()
        self.pin = self.identity()
        self.assertEqual(self.read()['value'], 'survives signed upgrade')

    def test_storage_recovers_interrupted_install_before_access(self):
        self.write(value='before interrupted install')
        self.run_killed(self.package('1.1.0'), 'new-moved')
        self.assertEqual(self.read()['value'], 'before interrupted install')
        self.assertEqual(json.loads((self.target / 'manifest.json').read_text())['version'], '1.0.0')

    def test_same_transaction_lock(self):
        with self.i.locked():
            with self.assertRaises(BlockingIOError): self.write()
        self.write()
        with self.i.locked():
            with self.assertRaises(BlockingIOError): self.read()

    def test_installer_refuses_while_real_storage_process_holds_lock(self):
        package = self.package('1.1.0')
        ready_r, ready_w = os.pipe(); resume_r, resume_w = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(ready_r); os.close(resume_w)
            def pause(phase):
                if phase == 'prepared':
                    os.write(ready_w, b'1'); os.read(resume_r, 1)
            self.storage.checkpoint = pause
            try: self.write(value='concurrent persistent data')
            except BaseException: os._exit(99)
            os._exit(0)
        os.close(ready_w); os.close(resume_r)
        try:
            self.assertTrue(select.select([ready_r], [], [], 5)[0], 'storage did not reach checkpoint')
            self.assertEqual(os.read(ready_r, 1), b'1')
            with self.assertRaises(BlockingIOError): self.i.install(package)
        finally:
            os.write(resume_w, b'1'); os.close(resume_w); os.close(ready_r)
            _, status = os.waitpid(pid, 0)
        self.assertEqual(status, 0)
        self.i.install(package); self.pin = self.identity()
        self.assertEqual(self.read()['value'], 'concurrent persistent data')

    def test_total_application_data_budget(self):
        extra = self.target / 'data/large-media'
        with extra.open('wb') as out: out.truncate(64 * 1024 * 1024)
        with self.assertRaisesRegex(ValueError, 'quota'): self.write()
        self.assertFalse(self.state.exists())
        self.assertEqual(extra.stat().st_size, 64 * 1024 * 1024)

    def test_unknown_temp_names_are_preserved(self):
        extra = self.target / 'data/.storage-v1.user.tmp'
        extra.write_text('user data')
        self.write(); self.read()
        self.assertEqual(extra.read_text(), 'user data')

    def test_real_process_interruptions_keep_whole_old_or_new(self):
        for phase in ('prepared', 'replaced', 'committed'):
            self.write(value='old')
            pid = os.fork()
            if pid == 0:
                self.storage.checkpoint = lambda p: os._exit(73) if p == phase else None
                try: self.write(value='new')
                except BaseException: os._exit(99)
                os._exit(0)
            _, status = os.waitpid(pid, 0)
            self.assertTrue(os.WIFEXITED(status)); self.assertEqual(os.WEXITSTATUS(status), 73)
            self.assertEqual(self.read()['value'], 'old' if phase == 'prepared' else 'new')
            self.assertFalse(list((self.target / 'data').glob('.storage-v1.*.tmp')))

    def test_two_apps_have_separate_data(self):
        self.write(value='first app')
        source = self.r / 'source-1.0.0-one'
        manifest = json.loads((source / 'manifest.json').read_text()); manifest['id'] = 'com.mimi.other'
        (source / 'manifest.json').write_text(json.dumps(manifest))
        package = self.r / 'other.aitapp'
        with contextlib.redirect_stdout(io.StringIO()):
            self.i.package.build(argparse.Namespace(source=str(source), output=str(package), key=str(self.base / 'one.key')))
        self.i.install(package)
        pin = self.identity(self.apps / 'com.mimi.other')
        self.assertEqual(self.storage.operate(pin, {'op': 'read', 'key': 'note'}), {'found': False})
        self.storage.operate(pin, {'op': 'write', 'key': 'note', 'value': 'second app'})
        self.assertEqual(self.read()['value'], 'first app')


if __name__ == '__main__':
    unittest.main()
