"""Real signed SDK binaries and actual kernel-authenticated Unix peers.

Native fixture directories are prepared only by these root tests. Production
installer admission stays closed. Ephemeral test keys are never enrolled.
"""
import concurrent.futures
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import threading
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('broker_under_test', ROOT / 'services/mimi-ubuntu-apps/broker.py')
B = importlib.util.module_from_spec(spec); spec.loader.exec_module(B)

PROGRAM = r'''
#define _GNU_SOURCE
#include "aitvbox/app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
static int raw(const char *s, size_t n) {
    int fd=socket(AF_UNIX,SOCK_STREAM,0); if(fd<0)return 2;
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"%s",getenv("AITVBOX_APP_SOCKET"));
    if(connect(fd,(void*)&a,sizeof(a)))return 3;
    size_t used=0; while(used<n){ssize_t z=write(fd,s+used,n-used);if(z<=0)return 4;used+=(size_t)z;}
    char out[512]={0};ssize_t z=read(fd,out,sizeof(out)-1);close(fd);
    return aitvbox_app_reply(z>0 && strstr(out,"\"ok\":false"),"refused");
}
int main(int argc,char **argv){
 const char *a=aitvbox_app_action(argc,argv);if(!a)return 2;
 if(!strcmp(a,"fast"))return aitvbox_app_reply(true,"fast");
 if(!strcmp(a,"slow")||!strcmp(a,"sleep")) {struct timespec t={.tv_sec=!strcmp(a,"sleep")?10:1};nanosleep(&t,NULL);}
 if(!strcmp(a,"flood")){char x[6000];memset(x,'x',sizeof(x));return write(1,x,sizeof(x))<0;}
 if(!strcmp(a,"badreply")){puts("{\"ok\":1,\"message\":\"bad\"}");return 0;}
 if(!strcmp(a,"extrareply")){puts("{\"ok\":true,\"message\":\"a\"}\n{\"ok\":true,\"message\":\"b\"}");return 0;}
 if(!strcmp(a,"spoof")){const char*s="{\"command\":\"capability\",\"capability\":\"storage.app\",\"appId\":\"com.mimi.other\",\"arguments\":{\"op\":\"write\",\"key\":\"note\",\"value\":\"spoof\"}}\n";return raw(s,strlen(s));}
 if(!strcmp(a,"duplicate")){const char*s="{\"command\":\"capability\",\"command\":\"capability\",\"capability\":\"storage.app\",\"arguments\":{\"op\":\"read\",\"key\":\"note\"}}\n";return raw(s,strlen(s));}
 if(!strcmp(a,"multiple")){const char*s="{}\n{}\n";return raw(s,strlen(s));}
 if(!strcmp(a,"oversize")){char x[4100];memset(x,'x',sizeof(x));x[sizeof(x)-1]='\n';return raw(x,sizeof(x));}
 if(!strcmp(a,"unknown")){char response[256];return aitvbox_app_reply(aitvbox_capability_call("input.keyboard","{}",response,sizeof(response))!=0,"refused");}
 if(!strcmp(a,"noperm"))return aitvbox_app_reply(aitvbox_storage_write("note","unauthorized")!=0,"refused");
 if(!strcmp(a,"many")){int count=0;for(int i=0;i<35;i++)if(!aitvbox_storage_write("note","quota"))count++;return aitvbox_app_reply(count==32,"bounded");}
 char value[512];bool found=false;
 if(strcmp(a,"read") && aitvbox_storage_write("note","SDK persisted 中文"))return aitvbox_app_reply(false,"write failed");
 if(aitvbox_storage_read("note",value,sizeof(value),&found))return aitvbox_app_reply(false,"read failed");
 return aitvbox_app_reply(found,found?value:"missing");
}
'''


@unittest.skipUnless(os.geteuid() == 0, 'requires root isolated runtime tests')
class BrokerTests(unittest.TestCase):
    @classmethod
    def cli(cls, *args):
        return subprocess.run([sys.executable, str(ROOT / 'tools/aitapp/aitapp.py'), *map(str, args)],
                              check=True, capture_output=True, timeout=20)

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='mimi-broker-tests-', dir='/var/lib')
        cls.base = Path(cls.tmp.name); cls.base.chmod(0o755)
        cls.source = cls.base / 'source'
        cls.cli('init', cls.source, '--id', 'com.mimi.broker', '--name', 'Broker test', '--publisher', 'test', '--native-ubuntu')
        (cls.source / 'src/main.c').write_text(PROGRAM)
        m = json.loads((cls.source / 'manifest.json').read_text()); m['permissions'] = ['storage.app']
        (cls.source / 'manifest.json').write_text(json.dumps(m))
        names = ('main', 'read', 'fast', 'slow', 'sleep', 'flood', 'badreply', 'extrareply', 'spoof', 'duplicate', 'multiple', 'oversize', 'unknown', 'noperm', 'many')
        page = {'schema': 1, 'title': 'Broker', 'layout': {'type': 'column', 'children': [
            {'type': 'action', 'label': n, 'command': 'app.invoke', 'arguments': {'name': n}} for n in names]}}
        (cls.source / 'ui/main.json').write_text(json.dumps(page))
        cls.cli('compile-ubuntu', cls.source)
        for name in ('one', 'other'):
            subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:2048', '-out', str(cls.base / (name + '.key'))], check=True, capture_output=True)
            subprocess.run(['openssl', 'pkey', '-in', str(cls.base / (name + '.key')), '-pubout', '-out', str(cls.base / (name + '.pem'))], check=True, capture_output=True)
        cls.package = cls.base / 'signed.aitapp'
        cls.cli('build', cls.source, '--key', cls.base / 'one.key', '-o', cls.package)
        cls.runner = cls.base / 'runner'
        subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'services/mimi-ubuntu-apps/native_runner.c'), '-o', str(cls.runner)], check=True)
        der = subprocess.check_output(['openssl', 'pkey', '-pubin', '-in', str(cls.base / 'one.pem'), '-outform', 'DER'])
        cls.publisher = hashlib.sha256(der).hexdigest()
        print(json.dumps({'fixture_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest()
              for name, path in [('signed_package', cls.package), ('native', cls.source / 'bin/app'),
                                 ('runner', cls.runner), ('public_sdk', ROOT / 'platform/app-sdk/src/app.c')]}}), flush=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.case = tempfile.TemporaryDirectory(dir=self.base)
        self.r = Path(self.case.name)
        self.runtime = tempfile.TemporaryDirectory(prefix='mimi-br-', dir='/var/lib')
        self.run_root = Path(self.runtime.name); self.run_root.chmod(0o755)
        self.apps = self.r / 'apps'; self.apps.mkdir()
        self.config = self.r / 'config.json'; self.config.write_text(json.dumps({'schema': 1, 'apps_root': str(self.apps), 'trusted_keys': [str(self.base / 'one.pem')]}))
        self.installer = B.I.Installer(self.config)
        self.target = self.apps / 'com.mimi.broker'; self.target.mkdir()
        self.installer.package.safe_extract(self.package, self.target)
        (self.target / 'data').mkdir(mode=0o700)
        (self.target / B.I.MARKER).write_text(self.publisher + '\n')
        self.broker = B.NativeBroker(self.installer, self.runner, self.run_root)

    def tearDown(self):
        self.assertEqual(list(self.run_root.iterdir()), [])
        self.runtime.cleanup(); self.case.cleanup()

    def invoke(self, action='main'):
        return self.broker.invoke('com.mimi.broker', action)

    def replace_signed_fixture(self, permissions=None, version=None):
        source = self.r / 'source'; shutil.copytree(self.source, source)
        m = json.loads((source / 'manifest.json').read_text())
        if permissions is not None: m['permissions'] = permissions
        if version is not None: m['version'] = version
        (source / 'manifest.json').write_text(json.dumps(m))
        package = self.r / 'changed.aitapp'; self.cli('build', source, '--key', self.base / 'one.key', '-o', package)
        # Root test fixture preparation only, never production install policy.
        for p in list(self.target.iterdir()):
            if p.name == 'data': continue
            shutil.rmtree(p) if p.is_dir() else p.unlink()
        self.installer.package.safe_extract(package, self.target)
        (self.target / B.I.MARKER).write_text(self.publisher + '\n')

    def test_actual_sdk_storage_write_read_and_new_invocation(self):
        self.assertEqual(self.invoke(), {'ok': True, 'message': 'SDK persisted 中文'})
        self.assertEqual(self.invoke('read'), {'ok': True, 'message': 'SDK persisted 中文'})
        self.assertEqual((self.target / 'data/storage-v1.json').stat().st_mode & 0o777, 0o600)

    def test_fast_no_rpc_processes(self):
        for _ in range(3): self.assertEqual(self.invoke('fast'), {'ok': True, 'message': 'fast'})

    def test_permission_missing_refuses_sdk_write(self):
        self.replace_signed_fixture(permissions=[])
        self.assertTrue(self.invoke('noperm')['ok'])
        self.assertFalse((self.target / 'data/storage-v1.json').exists())

    def test_unavailable_manifest_permission_refused(self):
        self.replace_signed_fixture(permissions=['input.keyboard'])
        with self.assertRaisesRegex(ValueError, 'unavailable'): self.invoke()

    def test_unlisted_action_refused(self):
        with self.assertRaisesRegex(ValueError, 'signed UI'): self.invoke('hidden')

    def test_malformed_capability_frames_refused(self):
        for action in ('duplicate', 'multiple', 'oversize', 'unknown'):
            with self.subTest(action=action): self.assertTrue(self.invoke(action)['ok'])
        self.assertFalse((self.target / 'data/storage-v1.json').exists())

    def test_self_reported_identity_refused(self):
        self.assertTrue(self.invoke('spoof')['ok'])
        self.assertFalse((self.target / 'data/storage-v1.json').exists())

    def test_capability_request_count_bounded(self):
        self.assertTrue(self.invoke('many')['ok'])

    def test_stdout_flood_refused_and_cleaned(self):
        with self.assertRaisesRegex(ValueError, 'output exceeds'): self.invoke('flood')

    def test_invalid_and_multiple_native_responses(self):
        for action in ('badreply', 'extrareply'):
            with self.subTest(action=action), self.assertRaises(ValueError): self.invoke(action)

    def test_timeout_terminates_and_cleans(self):
        before = time.monotonic()
        with self.assertRaises(ValueError): self.invoke('sleep')
        self.assertLess(time.monotonic() - before, 7)

    def test_tampered_payload_refused(self):
        with (self.target / 'bin/app').open('ab') as f: f.write(b'changed')
        with self.assertRaises(ValueError): self.invoke()

    def test_wrong_signature_refused(self):
        cfg = json.loads(self.config.read_text()); cfg['trusted_keys'] = [str(self.base / 'other.pem')]
        self.config.write_text(json.dumps(cfg))
        self.broker = B.NativeBroker(B.I.Installer(self.config), self.runner, self.run_root)
        with self.assertRaisesRegex(ValueError, 'publisher'): self.invoke()

    def test_publisher_marker_mismatch_refused(self):
        (self.target / B.I.MARKER).write_text('0' * 64 + '\n')
        with self.assertRaisesRegex(ValueError, 'publisher'): self.invoke()

    def test_bad_target_and_old_schema_refused(self):
        path = self.target / 'manifest.json'; original = path.read_text()
        for mode in ('target', 'schema'):
            m = json.loads(original)
            if mode == 'target': m['target']['architecture'] = 'aarch64'
            else: m['schema'] = 2; del m['target']
            path.write_text(json.dumps(m))
            with self.assertRaises(ValueError): self.invoke()

    def test_root_file_and_symlink_rules(self):
        p = self.target / 'bin/app'; p.chmod(0o777)
        with self.assertRaises(ValueError): self.invoke()
        p.chmod(0o755); p.unlink(); p.symlink_to(self.source / 'bin/app')
        with self.assertRaises(ValueError): self.invoke()

    def test_actual_unrelated_same_uid_socket_peer_refused(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            result = pool.submit(self.invoke, 'slow')
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and not list(self.run_root.glob('call-*/cap.sock')): time.sleep(0.01)
            path = next(self.run_root.glob('call-*/cap.sock'))
            def drop(): os.setgroups([]); os.setgid(65534); os.setuid(65534)
            code = 'import socket,sys;s=socket.socket(socket.AF_UNIX);s.connect(sys.argv[1]);s.sendall(b\'{}\\n\');print(s.recv(512).decode())'
            p = subprocess.run([sys.executable, '-c', code, str(path)], preexec_fn=drop, capture_output=True, text=True, timeout=3)
            self.assertEqual(p.returncode, 0, p.stderr); self.assertFalse(json.loads(p.stdout)['ok'])
            self.assertTrue(result.result(timeout=7)['ok'])

    def test_actual_other_runner_with_same_executable_refused(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            result = pool.submit(self.invoke, 'slow')
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and not list(self.run_root.glob('call-*/cap.sock')): time.sleep(0.01)
            path = next(self.run_root.glob('call-*/cap.sock'))
            fd = os.open(path.parent / 'code/bin/app', os.O_RDONLY)
            try:
                p = subprocess.run([str(self.runner), '--executable-fd', str(fd), '--action', 'noperm', '--socket', str(path)],
                                   pass_fds=(fd,), capture_output=True, text=True, timeout=7)
            finally: os.close(fd)
            self.assertEqual(p.returncode, 0, p.stderr); self.assertTrue(json.loads(p.stdout)['ok'])
            self.assertTrue(result.result(timeout=7)['ok'])

    def test_changed_installation_revokes_inflight_storage_identity(self):
        self.invoke()
        before = (self.target / 'data/storage-v1.json').read_bytes()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            result = pool.submit(self.invoke, 'slow')
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline and not list(self.run_root.glob('call-*/cap.sock')): time.sleep(0.01)
            self.assertTrue(list(self.run_root.glob('call-*/cap.sock')))
            with self.installer.locked(): self.replace_signed_fixture(version='1.1.0')
            self.assertEqual(result.result(timeout=7), {'ok': False, 'message': 'write failed'})
        self.assertEqual((self.target / 'data/storage-v1.json').read_bytes(), before)
        self.assertTrue(self.invoke('read')['ok'])

    def test_frame_deadline_is_total_not_per_read(self):
        # Real stream bytes arrive frequently enough to defeat per-recv timeout.
        left, right = socket.socketpair()
        with left, right, concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            def trickle():
                try:
                    for _ in range(8): right.sendall(b' '); time.sleep(0.06)
                except OSError: pass
            future = pool.submit(trickle)
            before = time.monotonic()
            with self.assertRaises((ValueError, socket.timeout)):
                B.NativeBroker.request(left, time.monotonic() + 5)
            self.assertLess(time.monotonic() - before, 0.4)
            left.close(); future.result(timeout=2)

    def test_bad_identity_does_not_create_application_state(self):
        with self.assertRaises(ValueError): self.broker.invoke('../escape', 'main')
        self.assertFalse((self.target / 'data/storage-v1.json').exists())

    def test_installer_native_policy_remains_closed(self):
        with self.assertRaisesRegex(ValueError, 'not yet admitted'): self.installer.install(self.package)


SESSION_CLIENT = r'''
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    int fd=socket(AF_UNIX,SOCK_STREAM,0); if(fd<0) return 3;
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"%s",argv[1]);
    if(connect(fd,(void*)&a,sizeof(a))) return 4;
    const char*q="{\"command\":\"invoke\",\"appId\":\"com.mimi.broker\",\"action\":\"fast\"}\n";
    if(write(fd,q,strlen(q))!=(ssize_t)strlen(q)) return 5;
    char b[512]; ssize_t n=read(fd,b,sizeof(b)); if(n<=0) return 6;
    ssize_t out=write(1,b,(size_t)n); close(fd); return out==n ? 0 : 7;
}
'''


@unittest.skipUnless(os.geteuid() == 0, 'requires root-controlled temporary endpoint')
class DesktopEndpointTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='mimi-session-endpoint-', dir='/var/lib')
        cls.base = Path(cls.tmp.name); cls.base.chmod(0o755)
        cls.helper_c = cls.base / 'client.c'; cls.helper_c.write_text(SESSION_CLIENT)
        cls.helper = cls.base / 'desktop-fixture'
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(cls.helper_c), '-o', str(cls.helper)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.runtime = tempfile.TemporaryDirectory(prefix='mimi-session-run-', dir='/var/lib')
        self.root = Path(self.runtime.name); self.root.chmod(0o755)
        self.socket_path = self.root / 'apps.sock'
        self.profile_path = self.root / 'session.json'
        self.profile = {
            'schema_version': 1, 'uid': 1000, 'gid': 1000,
            'unit': 'mimi-ubuntu-login-host.service', 'script': '/usr/lib/mimi/run.py',
            'script_sha256': 'a' * 64, 'provider': '/usr/lib/mimi/provider',
            'provider_sha256': 'b' * 64, 'runtime': '/run/mimi/session',
            'settings': '/var/lib/mimi/settings.json', 'screen': '1920x1080',
            'display': ':0', 'xauthority': '/run/mimi/display.auth',
            'login_session': '2'
        }
        self.profile_path.write_text(json.dumps(self.profile)); self.profile_path.chmod(0o600)
        self.test_cgroup = next(
            parts[2] for line in B.proc_record(os.getpid())['cgroups'].splitlines()
            if len(parts := line.split(':', 2)) == 3 and
            (parts[0] == '0' or parts[1] == 'name=systemd')
        )

        class FakeBroker:
            def invoke(inner, app_id, action):
                if (app_id, action) != ('com.mimi.broker', 'fast'):
                    raise ValueError('unknown signed action')
                return {'ok': True, 'message': 'launched'}

        self.endpoint = B.DesktopEndpoint(FakeBroker(), self.socket_path, self.helper,
                                          self.profile_path, 1000, 1000)
        # The test process and its real C client share a kernel cgroup. Session
        # discovery itself is exercised independently against explicit records.
        self.endpoint.session_context = lambda: {
            'profile': self.profile, 'control_group': self.test_cgroup
        }

    def tearDown(self):
        self.runtime.cleanup()

    @staticmethod
    def drop_to_desktop():
        os.setgroups([]); os.setgid(1000); os.setuid(1000)

    def start_server(self):
        stop = threading.Event()
        thread = threading.Thread(target=self.endpoint.serve, args=(stop,), daemon=True)
        thread.start()
        deadline = time.monotonic() + 2
        while not self.socket_path.exists() and time.monotonic() < deadline: time.sleep(0.005)
        self.assertTrue(self.socket_path.exists())
        return stop, thread

    def test_root_endpoint_accepts_pinned_binary_in_service_cgroup(self):
        stop, thread = self.start_server()
        try:
            p = subprocess.run([str(self.helper), str(self.socket_path)], preexec_fn=self.drop_to_desktop,
                               capture_output=True, text=True, timeout=3)
            self.assertEqual(p.returncode, 0, p.stderr)
            self.assertEqual(json.loads(p.stdout), {'ok': True, 'message': 'launched'})
        finally:
            stop.set(); thread.join(3); self.assertFalse(thread.is_alive())
        self.assertFalse(self.socket_path.exists())

    def test_session_context_requires_active_local_session_and_bound_unit(self):
        del self.endpoint.session_context
        session = mock.Mock(returncode=0, stdout=(
            'User=1000\nActive=yes\nRemote=no\nType=x11\nSeat=seat0\nState=active\n'
            'Scope=session-2.scope\nDisplay=\nService=gdm-password\nClass=user\n'), stderr='')
        unit = mock.Mock(returncode=0, stdout=(
            'ActiveState=active\nUser=1000\nGroup=1000\nControlGroup=/system.slice/mimi-ubuntu-login-host.service'
            '\nBindsTo=system.slice session-2.scope\nAfter=basic.target session-2.scope\n'), stderr='')
        with mock.patch.object(B.subprocess, 'run', side_effect=[session, unit]) as run:
            result = self.endpoint.session_context()
        self.assertEqual(result['profile']['login_session'], '2')
        self.assertEqual(result['control_group'], '/system.slice/mimi-ubuntu-login-host.service')
        self.assertEqual(run.call_count, 2)

    def test_session_context_rejects_service_bound_to_another_session(self):
        del self.endpoint.session_context
        session = mock.Mock(returncode=0, stdout=(
            'User=1000\nActive=yes\nRemote=no\nType=x11\nSeat=seat0\nState=active\n'
            'Scope=session-2.scope\nDisplay=:0\nService=gdm-password\nClass=user\n'), stderr='')
        unit = mock.Mock(returncode=0, stdout=(
            'ActiveState=active\nUser=1000\nGroup=1000\nControlGroup=/system.slice/mimi-ubuntu-login-host.service'
            '\nBindsTo=session-3.scope\nAfter=session-2.scope\n'), stderr='')
        with mock.patch.object(B.subprocess, 'run', side_effect=[session, unit]):
            with self.assertRaisesRegex(ValueError, 'bound to the active login session'):
                self.endpoint.session_context()

    def test_inactive_or_remote_session_is_rejected(self):
        del self.endpoint.session_context
        session = mock.Mock(returncode=0, stdout=(
            'User=1000\nActive=yes\nRemote=yes\nType=x11\nSeat=seat0\nState=active\n'
            'Scope=session-2.scope\nDisplay=:0\nService=gdm-password\nClass=user\n'), stderr='')
        with mock.patch.object(B.subprocess, 'run', return_value=session):
            with self.assertRaisesRegex(ValueError, 'active enrolled local X11 session'):
                self.endpoint.session_context()

    def test_nonempty_foreign_display_is_rejected(self):
        del self.endpoint.session_context
        session = mock.Mock(returncode=0, stdout=(
            'User=1000\\nActive=yes\\nRemote=no\\nType=x11\\nSeat=seat0\\nState=active\\n'
            'Scope=session-2.scope\\nDisplay=:1\\nService=gdm-password\\nClass=user\\n'), stderr='')
        with mock.patch.object(B.subprocess, 'run', return_value=session):
            with self.assertRaisesRegex(ValueError, 'active enrolled local X11 session'):
                self.endpoint.session_context()

    def test_session_profile_requires_root_private_mode_and_fixed_identity(self):
        self.profile_path.chmod(0o644)
        with self.assertRaisesRegex(ValueError, 'unsafe login session profile mode'):
            self.endpoint.read_session_profile()
        self.profile_path.chmod(0o600)
        self.profile['uid'] = 1001
        self.profile_path.write_text(json.dumps(self.profile)); self.profile_path.chmod(0o600)
        with self.assertRaisesRegex(ValueError, 'profile identity mismatch'):
            self.endpoint.read_session_profile()

    def test_cgroup_membership_is_exact_not_prefix(self):
        good = {'cgroups': '0::/system.slice/mimi-ubuntu-login-host.service\n'}
        child = {'cgroups': '0::/system.slice/mimi-ubuntu-login-host.service/child\n'}
        other = {'cgroups': '0::/system.slice/other.service\n'}
        self.assertTrue(self.endpoint.cgroup_contains(good, '/system.slice/mimi-ubuntu-login-host.service'))
        self.assertFalse(self.endpoint.cgroup_contains(child, '/system.slice/mimi-ubuntu-login-host.service'))
        self.assertFalse(self.endpoint.cgroup_contains(other, '/system.slice/mimi-ubuntu-login-host.service'))

    def test_different_same_uid_executable_is_rejected(self):
        peer = subprocess.Popen(['/usr/bin/sleep', '2'], preexec_fn=self.drop_to_desktop)
        try:
            class Peer:
                def getsockopt(inner, *args): return struct.pack('3i', peer.pid, 1000, 1000)
            with self.assertRaisesRegex(ValueError, 'executable'):
                self.endpoint.authenticate(Peer())
        finally: peer.terminate(); peer.wait(timeout=2)

    def test_preexisting_regular_file_is_never_replaced_by_socket(self):
        self.socket_path.write_text('preserve')
        with self.assertRaisesRegex(ValueError, 'untrusted desktop socket'):
            self.endpoint.serve(threading.Event())
        self.assertEqual(self.socket_path.read_text(), 'preserve')


if __name__ == '__main__': unittest.main()
