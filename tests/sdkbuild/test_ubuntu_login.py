"""Logind selection, display authorization and no-replay boundaries."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mimi_login', ROOT / 'services/mimi-ubuntu-provider/login.py')
login = importlib.util.module_from_spec(spec)
spec.loader.exec_module(login)


def session():
    return dict(Id='42', User=str(os.getuid()), Seat='seat0', Remote='no', Active='yes', Class='user', Type='x11', Display=':0', Scope='session-42.scope', Service='gdm-password')


class LoginTests(unittest.TestCase):
    def test_only_unique_real_local_user_session(self):
        good = session()
        self.assertEqual(login.select_session([good], os.getuid()), good)
        for k, v in dict(User='125', Seat='seat1', Remote='yes', Active='no', Class='greeter', Type='tty', Display='remote:0', Scope='session-43.scope', Id='../42').items():
            with self.subTest(key=k):
                self.assertIsNone(login.select_session([dict(good, **{k: v})], os.getuid()))
        self.assertIsNone(login.select_session([dict(good, Type='wayland')], os.getuid()))
        with self.assertRaises(ValueError):
            login.select_session([good, dict(good, Id='43', Scope='session-43.scope')], os.getuid())

    def test_ephemeral_ssh_rows_never_enter_graphical_query(self):
        with patch.object(login, 'command', return_value=b'42 1000 ubuntu\n43 1000 ubuntu pts/0\n') as command:
            self.assertIsNone(login.discover(1000))
            command.assert_called_once()

    def test_real_logind_projection_and_zero_empty_output(self):
        raw = b'42 1000 ubuntu seat0 tty2\n'
        props = ('\n'.join(k+'='+v for k,v in session().items())+'\n').encode()
        with patch.object(login, 'command', side_effect=[raw, props]) as command:
            self.assertEqual(login.discover(1000)['Id'], '42')
            args = command.call_args_list[1][0][0]
            self.assertIn('--property=Id', args)
            self.assertFalse(any(',' in a for a in args))
        with patch.object(login, 'command', side_effect=[raw, b'']):
            with self.assertRaises(ValueError):
                login.discover(1000)

    def test_gdm_authorization_has_fixed_user_path(self):
        self.assertEqual(str(login.authority_source(session(), 1000)), '/run/user/1000/gdm/Xauthority')
        with self.assertRaises(ValueError):
            login.authority_source(dict(session(), Service='caller-supplied'), 1000)

    def test_authorization_links_permissions_owner_and_change(self):
        # Outside /tmp; every real ancestor is checked, including the user's home.
        with tempfile.TemporaryDirectory(dir=Path.home()) as d:
            p = Path(d) / 'authority'
            p.write_bytes(b'test-cookie-fixture')
            p.chmod(0o600)
            self.assertEqual(login.read_authority(p, os.getuid()), b'test-cookie-fixture')
            p.chmod(0o700)
            self.assertEqual(login.read_authority(p, os.getuid()), b'test-cookie-fixture')
            for mode in (0o640, 0o644, 0o710, 0o701, 0o711, 0o1600, 0o4600):
                p.chmod(mode)
                with self.subTest(mode=oct(mode)), self.assertRaises(ValueError):
                    login.read_authority(p, os.getuid())
            p.chmod(0o600)
            link = Path(d) / 'alias'
            link.symlink_to(p)
            with self.assertRaises(OSError):
                login.read_authority(link, os.getuid())
            p.chmod(0o644)
            with self.assertRaises(ValueError):
                login.read_authority(p, os.getuid())
            p.chmod(0o600)
            hard = Path(d) / 'hard'
            os.link(p, hard)
            with self.assertRaises(ValueError):
                login.read_authority(p, os.getuid())
            hard.unlink()
            with self.assertRaises(ValueError):
                login.read_authority(p, os.getuid() + 1)
            with patch.object(login.os, 'read', return_value=b'truncated'):
                with self.assertRaises(ValueError):
                    login.read_authority(p, os.getuid())

    def test_private_service_umask_keeps_user_runtime_reachable(self):
        with tempfile.TemporaryDirectory(dir=Path.home()) as d:
            b = login.Bridge.__new__(login.Bridge)
            b.uid = os.getuid()
            b.base = Path(d) / 'runtime'
            b.session_file = b.base / 'session.json'
            b.auth = b.base / 'display.auth'
            b.profile = {'gid': os.getgid()}
            old = os.umask(0o077)
            try:
                with patch.object(login, 'read_authority', return_value=b'fixture'), patch.object(login, 'trusted'), patch.object(login, 'atomic') as atomic, patch.object(b, 'transaction', side_effect=lambda:os.open('/dev/null',os.O_RDONLY)):
                    b.prepare(session())
                    self.assertEqual(b.base.stat().st_mode & 0o777, 0o755)
                    self.assertEqual((b.base/'session').stat().st_mode & 0o777, 0o700)
                    self.assertEqual(atomic.call_args_list[0][0][2], os.getuid())
            finally:
                os.umask(old)

    def test_no_login_never_launches(self):
        b = login.Bridge.__new__(login.Bridge)
        b.uid = os.getuid()
        with patch.object(login, 'discover', return_value=None), patch.object(b, 'cleanup') as clean, patch.object(b, 'invoke') as invoke:
            self.assertEqual(b.tick(), 'waiting-for-local-x11-login')
            invoke.assert_not_called()
            clean.assert_called_once()

    def test_failed_start_is_not_replayed_in_same_login(self):
        b = login.Bridge.__new__(login.Bridge)
        b.uid = os.getuid()
        b.last = None
        with patch.object(login, 'discover', return_value=session()), patch.object(b, 'prepare') as prepare, patch.object(b, 'invoke', side_effect=ValueError('signature rejected')) as invoke:
            with self.assertRaises(ValueError):
                b.tick()
            self.assertEqual(b.tick(), 'login-start-failed-no-retry')
            invoke.assert_called_once_with('start-selected')
            prepare.assert_called_once()

    def test_update_lock_blocks_before_runtime_write(self):
        b = login.Bridge.__new__(login.Bridge)
        b.uid = os.getuid()
        b.last = None
        with patch.object(login, 'discover', return_value=session()), patch.object(b, 'prepare', side_effect=BlockingIOError), patch.object(b, 'invoke') as invoke:
            with self.assertRaises(BlockingIOError):
                b.tick()
            self.assertIsNone(b.last)
            invoke.assert_not_called()

    def test_duplicate_config_and_property_and_path_escape_rejected(self):
        with self.assertRaises(ValueError):
            json.loads('{"uid":1000,"uid":125}', object_pairs_hook=login.object_pairs)
        with self.assertRaises(ValueError):
            login.properties(b'User=1000\nUser=125\n')
        for p in ['/run/../tmp/authority', '/run/a\n', 'relative', '/run/a//b']:
            with self.subTest(path=p), self.assertRaises(ValueError):
                login.absolute(p)


if __name__ == '__main__':
    unittest.main()
