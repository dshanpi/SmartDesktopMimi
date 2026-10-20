"""Internal Ubuntu native invocation broker; no public invoke endpoint.

Production native install/profile activation remains closed. The platform caller
must itself be trusted. Native capability sockets authenticate the actual runner
child, never application-provided identities. See the platform README.
"""
import contextlib
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import selectors
import shutil
import socket
import stat
import struct
import subprocess
import tempfile
import time


def sibling(name):
    spec = importlib.util.spec_from_file_location('mimi_broker_' + name, Path(__file__).with_name(name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


I = sibling('installer')
S = sibling('storage')
LIMIT = 4096


def fail(message):
    raise ValueError(message)


def decode(raw):
    if not raw or len(raw) > LIMIT or b'\0' in raw:
        fail('invalid JSON frame size')
    obj = json.loads(raw.decode('utf-8'), object_pairs_hook=I.duplicate_free,
                     parse_constant=lambda value: fail('non-finite JSON number'))
    if not isinstance(obj, dict):
        fail('JSON object required')
    return obj


def proc_record(pid):
    """One proc-directory FD pins this process across the identity reads."""
    fd = os.open('/proc/' + str(pid), os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        def read(name):
            item = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=fd)
            try:
                value = os.read(item, 16385)
            finally:
                os.close(item)
            if len(value) > 16384:
                fail('oversized process metadata')
            return value.decode('ascii')
        fields = read('stat').rsplit(')', 1)[1].split()
        if fields[0] in ('Z', 'X', 'x'):
            fail('process exited')
        status = dict(line.split(':', 1) for line in read('status').splitlines() if ':' in line)
        exe = os.stat('exe', dir_fd=fd)
        return {'parent': int(fields[1]), 'sid': int(fields[3]), 'start': int(fields[19]),
                'exe': (exe.st_dev, exe.st_ino), 'status': status, 'cgroups': read('cgroup')}
    finally:
        os.close(fd)


def actions(node):
    result = set()
    if isinstance(node, dict):
        if node.get('type') == 'action' and node.get('command') == 'app.invoke':
            result.add(node['arguments']['name'])
        for value in node.values():
            result.update(actions(value))
    elif isinstance(node, list):
        for value in node:
            result.update(actions(value))
    return result


class NativeBroker:
    def __init__(self, installer, runner, runtime_root):
        if os.getuid() != 0 or os.geteuid() != 0:
            fail('root platform broker required')
        self.installer = installer
        self.storage = S.Storage(installer)
        self.runner = I.secure(runner)
        mode = self.runner.stat().st_mode
        if not mode & 0o111 or mode & (stat.S_ISUID | stat.S_ISGID):
            fail('invalid runner mode')
        self.runtime_root = I.secure(runtime_root, directory=True)
        if len(str(self.runtime_root).encode()) > 65:
            fail('runtime root too long for Unix socket')

    def admit(self, app_id, action, work):
        i = self.installer
        if not isinstance(app_id, str) or not i.package.APP_ID.fullmatch(app_id) or len(app_id) > 96:
            fail('invalid application id')
        if not isinstance(action, str):
            fail('invalid action')
        with i.locked():
            i.recover()
            target = i.root / app_id
            i.tree_safe(target)
            # Snapshot code while the installation is locked. Data and the
            # installer marker are not signed payload. No source trees admitted.
            code = work / 'code'
            code.mkdir(mode=0o700)
            entries = list(target.iterdir())
            if any(p.name == 'src' for p in entries):
                fail('source files are not installed payload')
            total = count = 0
            for p in target.rglob('*'):
                if p.relative_to(target).parts[0] == 'data' or p.name == I.MARKER:
                    continue
                if p.is_file():
                    total += p.stat().st_size
                    count += 1
            if total > 66 * 1024 * 1024 or count > 256:
                fail('installed code exceeds limits')
            for p in entries:
                if p.name in ('data', I.MARKER):
                    continue
                if p.is_dir():
                    shutil.copytree(p, code / p.name)
                else:
                    shutil.copy2(p, code / p.name)
            try:
                m = i.package.load_manifest(code)
                if m['schema'] != 3 or m['id'] != app_id:
                    fail('explicit Ubuntu native installation required')
                if not set(m['permissions']) <= {'storage.app'}:
                    fail('requested capability unavailable')
                files = i.package.payload_files(code, m)
                sums = code / 'SHA256SUMS'
                if i.package.checksums(code, files) != sums.read_bytes():
                    fail('signed payload checksum mismatch')
            except SystemExit as exc:
                raise ValueError('invalid native payload') from exc
            signature = I.secure(code / 'SIGNATURE')
            if not 0 < signature.stat().st_size <= 16384:
                fail('invalid signature length')
            publisher = None
            for key in i.keys:
                I.secure(key)
                p = subprocess.run(['/usr/bin/openssl', 'dgst', '-sha256', '-verify', str(key),
                                    '-signature', str(signature), str(sums)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
                if p.returncode == 0:
                    der = subprocess.check_output(['/usr/bin/openssl', 'pkey', '-pubin', '-in', str(key), '-outform', 'DER'],
                                                  stderr=subprocess.DEVNULL, timeout=5)
                    publisher = hashlib.sha256(der).hexdigest()
                    break
            if publisher is None or I.secure(target / I.MARKER).read_text().strip() != publisher:
                fail('untrusted or changed publisher')
            allowed = set()
            for entry in [m['ui']['entry']] + [v['uiEntry'] for v in m.get('localizations', {}).values()]:
                allowed.update(actions(i.package.read_json(code / entry, 'signed UI')))
            if action not in allowed:
                fail('action absent from signed UI')
            native = code / m['runtime']['entry']
            module = i.package.ubuntu_module()
            receipt = module.inspect_host(module.Elf(native))
            identity = (app_id, publisher, i.installed_digest(target))
            fd = os.open(str(I.secure(native)), os.O_RDONLY | os.O_NOFOLLOW)
            return fd, identity, set(m['permissions']), receipt

    def authenticate(self, client, process, monitor_start, executable, runner_inode, peer_pin):
        pid, uid, gid = struct.unpack('3i', client.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if (uid, gid) != (65534, 65534) or process.poll() is not None:
            fail('unrecognized capability peer')
        monitor = proc_record(process.pid)
        if monitor['parent'] != os.getpid() or (monitor_start is not None and monitor['start'] != monitor_start) or monitor['exe'] != runner_inode:
            fail('runner identity mismatch')
        record = proc_record(pid)
        status = record['status']
        if record['parent'] != process.pid or record['exe'] != executable:
            fail('peer is not the admitted runner child')
        if [int(x) for x in status['Uid'].split()] != [65534] * 4 or [int(x) for x in status['Gid'].split()] != [65534] * 4:
            fail('native identity changed')
        if status['Groups'].strip() or status['NoNewPrivs'].strip() != '1' or status['Seccomp'].strip() != '2':
            fail('native process restrictions missing')
        if any(int(status[k], 16) for k in ('CapInh', 'CapPrm', 'CapEff', 'CapAmb')):
            fail('native process has capabilities')
        pin = (pid, record['start'], monitor['start'])
        if peer_pin is not None and pin != peer_pin:
            fail('native process replaced')
        return pin

    @staticmethod
    def request(client, deadline):
        frame_deadline = min(deadline, time.monotonic() + 0.2)
        raw = bytearray()
        while b'\n' not in raw:
            remaining = frame_deadline - time.monotonic()
            if remaining <= 0:
                fail('capability frame deadline exceeded')
            client.settimeout(remaining)
            part = client.recv(LIMIT + 1 - len(raw))
            if not part:
                fail('incomplete capability frame')
            raw.extend(part)
            if len(raw) > LIMIT:
                fail('capability frame too large')
        if raw.count(b'\n') != 1 or raw[-1:] != b'\n':
            fail('one capability frame required')
        return decode(raw[:-1])

    @staticmethod
    def desktop_request(client, deadline):
        frame_deadline = min(deadline, time.monotonic() + 0.2)
        raw = bytearray()
        while b'\n' not in raw:
            remaining = frame_deadline - time.monotonic()
            if remaining <= 0:
                fail('desktop request deadline exceeded')
            client.settimeout(remaining)
            part = client.recv(LIMIT + 1 - len(raw))
            if not part:
                fail('incomplete desktop request')
            raw.extend(part)
            if len(raw) > LIMIT:
                fail('desktop request too large')
        if raw.count(b'\n') != 1 or raw[-1:] != b'\n':
            fail('one desktop request required')
        return decode(raw[:-1])


    def invoke(self, app_id, action):
        # This API is called only by trusted platform code. A future desktop
        # endpoint needs its own session authentication before reaching here.
        I.secure(self.runner)
        I.secure(self.runtime_root, directory=True)
        with tempfile.TemporaryDirectory(prefix='call-', dir=self.runtime_root) as temp:
            work = Path(temp)
            work.chmod(0o711)
            fd, identity, permissions, receipt = self.admit(app_id, action, work)
            process = None
            try:
                executable_stat = os.fstat(fd)
                executable = (executable_stat.st_dev, executable_stat.st_ino)
                runner_stat = self.runner.stat()
                runner_inode = (runner_stat.st_dev, runner_stat.st_ino)
                path = work / 'cap.sock'
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
                    listener.bind(str(path))
                    path.chmod(0o666)
                    listener.listen(8)
                    listener.setblocking(False)
                    process = subprocess.Popen([str(self.runner), '--executable-fd', str(fd),
                                                '--action', action, '--socket', str(path)],
                                               pass_fds=(fd,), stdin=subprocess.DEVNULL,
                                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                               env={'PATH': '/usr/bin:/bin', 'LANG': 'C'}, start_new_session=True)
                    # The unreaped Popen child cannot have its PID reused.
                    # Capture the full live identity at the first capability
                    # request; no-RPC programs may already have exited here.
                    monitor_start = None
                    os.set_blocking(process.stdout.fileno(), False)
                    output = bytearray()
                    peer_pin = None
                    connections = requests = 0
                    deadline = time.monotonic() + 6
                    eof = False
                    with selectors.DefaultSelector() as selector:
                        selector.register(listener, selectors.EVENT_READ, 'capability')
                        selector.register(process.stdout, selectors.EVENT_READ, 'output')
                        while not eof:
                            if time.monotonic() >= deadline:
                                fail('native invocation deadline exceeded')
                            for key, _ in selector.select(0.05):
                                if key.data == 'output':
                                    part = os.read(process.stdout.fileno(), LIMIT + 1 - len(output))
                                    if not part:
                                        eof = True
                                        break
                                    output.extend(part)
                                    if len(output) > LIMIT:
                                        fail('native output exceeds limit')
                                else:
                                    client, _ = listener.accept()
                                    connections += 1
                                    with client:
                                        if connections > 64:
                                            fail('too many capability connections')
                                        try:
                                            peer_pin = self.authenticate(client, process, monitor_start, executable, runner_inode, peer_pin)
                                            request = self.request(client, deadline)
                                            self.authenticate(client, process, monitor_start, executable, runner_inode, peer_pin)
                                            requests += 1
                                            if requests > 32:
                                                fail('too many capability requests')
                                            if set(request) != {'command', 'capability', 'arguments'} or request['command'] != 'capability' or request['capability'] != 'storage.app' or 'storage.app' not in permissions:
                                                fail('capability not authorized')
                                            result = self.storage.operate(identity, request['arguments'])
                                            response = {'ok': True, 'result': result}
                                        except (ValueError, OSError, KeyError, RecursionError, UnicodeError):
                                            response = {'ok': False, 'error': 'capability refused'}
                                        raw = S.encoded(response) + b'\n'
                                        if len(raw) > LIMIT:
                                            fail('capability response exceeds limit')
                                        with contextlib.suppress(OSError):
                                            client.sendall(raw)
                    status = process.wait(timeout=max(0.01, deadline - time.monotonic()))
                    if status != 0:
                        fail('native process failed or timed out')
                    reply = decode(bytes(output))
                    if set(reply) != {'ok', 'message'} or type(reply['ok']) is not bool or not isinstance(reply['message'], str) or '\0' in reply['message'] or len(reply['message'].encode('utf-8')) > 512:
                        fail('invalid native response')
                    return reply
            finally:
                os.close(fd)
                if process is not None:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=2)
                    process.stdout.close()

class DesktopEndpoint:
    """Root UNIX endpoint bound to the active login-bridge session profile.

    The existing login bridge starts the desktop in a systemd service bound to
    the logind session scope. Its process SID therefore need not equal the
    GDM session leader SID; authentication checks the root profile, active
    session, unit binding, cgroup and pinned executable instead.
    """
    PROFILE_FIELDS = {
        'schema_version', 'uid', 'gid', 'unit', 'script', 'script_sha256',
        'provider', 'provider_sha256', 'runtime', 'settings', 'screen',
        'display', 'xauthority', 'login_session'
    }

    def __init__(self, broker, socket_path, desktop_binary, session_profile, uid, gid):
        if os.getuid() != 0 or os.geteuid() != 0:
            fail('root desktop endpoint required')
        self.broker = broker
        self.path = Path(socket_path)
        parent = I.secure(self.path.parent, directory=True)
        if not isinstance(uid, int) or isinstance(uid, bool) or uid <= 0 or not isinstance(gid, int) or isinstance(gid, bool) or gid <= 0:
            fail('invalid desktop account')
        if len(os.fsencode(self.path)) >= 108:
            fail('desktop socket path too long')
        self.uid, self.gid = uid, gid
        self.session_profile = Path(session_profile)
        if not self.session_profile.is_absolute() or len(str(self.session_profile).encode()) > 4096:
            fail('invalid session profile path')
        I.secure(self.session_profile.parent, directory=True)
        self.binary = I.secure(desktop_binary)
        s = self.binary.stat()
        if s.st_uid != 0 or s.st_mode & 0o022 or not stat.S_ISREG(s.st_mode) or not s.st_mode & 0o111:
            fail('desktop executable is not root controlled')
        self.desktop_inode = (s.st_dev, s.st_ino)
        if not parent.is_dir():
            fail('invalid socket parent')

    def read_session_profile(self):
        path = I.secure(self.session_profile)
        s = path.stat()
        if stat.S_IMODE(s.st_mode) != 0o600:
            fail('unsafe login session profile mode')
        profile = I.load_json(path)
        if not isinstance(profile, dict) or set(profile) != self.PROFILE_FIELDS or \
                type(profile.get('schema_version')) is not int or profile['schema_version'] != 1 or \
                type(profile.get('uid')) is not int or profile['uid'] != self.uid or \
                type(profile.get('gid')) is not int or profile['gid'] != self.gid:
            fail('login session profile identity mismatch')
        if not isinstance(profile.get('unit'), str) or not re.fullmatch(r'mimi-ubuntu-[a-z0-9-]{1,32}\.service', profile['unit']) or \
                not isinstance(profile.get('login_session'), str) or not re.fullmatch(r'[0-9]{1,32}', profile['login_session']) or \
                not isinstance(profile.get('display'), str) or not re.fullmatch(r':[0-9]{1,5}(?:\.[0-9])?', profile['display']):
            fail('invalid login session profile')
        return profile

    @staticmethod
    def properties(raw, description):
        fields = {}
        for line in raw.splitlines():
            if '=' not in line:
                fail('invalid ' + description)
            key, value = line.split('=', 1)
            if key in fields:
                fail('duplicate ' + description + ' property')
            fields[key] = value
        return fields

    @staticmethod
    def cgroup_contains(record, expected):
        for line in record.get('cgroups', '').splitlines():
            parts = line.split(':', 2)
            if len(parts) == 3 and parts[2] == expected and (parts[0] == '0' or parts[1] == 'name=systemd'):
                return True
        return False

    def session_context(self):
        profile = self.read_session_profile()
        session_id = profile['login_session']
        session = subprocess.run(
            ['/usr/bin/loginctl', 'show-session', session_id, '--no-pager',
             '-p', 'User', '-p', 'Active', '-p', 'Remote', '-p', 'Type',
             '-p', 'Seat', '-p', 'State', '-p', 'Scope', '-p', 'Display',
             '-p', 'Service', '-p', 'Class'],
            capture_output=True, text=True, timeout=2, check=True)
        fields = self.properties(session.stdout, 'logind session')
        if fields != {
                'User': str(self.uid), 'Active': 'yes', 'Remote': 'no',
                'Type': 'x11', 'Seat': 'seat0', 'State': 'active',
                'Scope': 'session-' + session_id + '.scope',
                'Display': fields.get('Display'), 'Service': fields.get('Service'),
                'Class': 'user'} or fields.get('Display') not in ('', profile['display']) or \
                fields.get('Service') not in ('gdm-password', 'gdm-autologin'):
            fail('desktop session is not the active enrolled local X11 session')

        unit = subprocess.run(
            ['/usr/bin/systemctl', 'show', profile['unit'], '--no-pager',
             '-p', 'ActiveState', '-p', 'User', '-p', 'Group',
             '-p', 'ControlGroup', '-p', 'BindsTo', '-p', 'After'],
            capture_output=True, text=True, timeout=2, check=True)
        state = self.properties(unit.stdout, 'desktop service')
        control_group = '/system.slice/' + profile['unit']
        scope = 'session-' + session_id + '.scope'
        if set(state) != {'ActiveState', 'User', 'Group', 'ControlGroup', 'BindsTo', 'After'} or \
                state['ActiveState'] != 'active' or state['User'] != str(self.uid) or \
                state['Group'] != str(self.gid) or state['ControlGroup'] != control_group or \
                scope not in state['BindsTo'].split() or scope not in state['After'].split():
            fail('desktop service is not bound to the active login session')
        return {'profile': profile, 'control_group': control_group}

    def authenticate(self, client, expected=None):
        pid, uid, gid = struct.unpack('3i', client.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
        if (uid, gid) != (self.uid, self.gid):
            fail('desktop peer account mismatch')
        record = proc_record(pid)
        status = record['status']
        if record['exe'] != self.desktop_inode or status['Uid'].split() != [str(uid)] * 4 or \
                status['Gid'].split() != [str(gid)] * 4:
            fail('desktop peer executable or identity mismatch')
        context = self.session_context()
        profile = context['profile']
        if not self.cgroup_contains(record, context['control_group']):
            fail('desktop peer is outside the bound application service')
        pin = (pid, record['start'], record['sid'], record['exe'],
               profile['login_session'], profile['unit'], context['control_group'])
        if expected is not None and pin != expected:
            fail('desktop peer or login session changed during request')
        return pin

    def serve_client(self, client):
        pin = self.authenticate(client)
        request = NativeBroker.desktop_request(client, time.monotonic() + 2)
        self.authenticate(client, pin)
        if set(request) != {'command', 'appId', 'action'} or request.get('command') != 'invoke' or \
                not isinstance(request.get('appId'), str) or not isinstance(request.get('action'), str):
            fail('invalid desktop invocation request')
        reply = self.broker.invoke(request['appId'], request['action'])
        raw = S.encoded(reply) + b'\n'
        if len(raw) > LIMIT:
            fail('desktop reply exceeds limit')
        client.sendall(raw)

    def serve(self, stop=None):
        I.secure(self.path.parent, directory=True)
        try:
            current = self.path.lstat()
        except FileNotFoundError:
            current = None
        if current is not None:
            if not stat.S_ISSOCK(current.st_mode) or current.st_uid != 0 or current.st_nlink != 1:
                fail('refusing to replace untrusted desktop socket')
            self.path.unlink()
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM | socket.SOCK_CLOEXEC)
        bound = None
        try:
            listener.bind(str(self.path))
            bound = os.lstat(self.path)
            if not stat.S_ISSOCK(bound.st_mode) or bound.st_uid != 0:
                fail('desktop socket ownership mismatch')
            os.chown(self.path, 0, self.gid, follow_symlinks=False)
            os.chmod(self.path, 0o660)
            listener.listen(8)
            listener.settimeout(0.2)
            while stop is None or not stop.is_set():
                try:
                    client, _ = listener.accept()
                except socket.timeout:
                    continue
                with client:
                    client.settimeout(2)
                    try:
                        self.serve_client(client)
                    except (ValueError, OSError, KeyError, RecursionError, UnicodeError,
                            subprocess.SubprocessError):
                        with contextlib.suppress(OSError):
                            client.sendall(b'{"ok":false,"message":"Application launch failed"}\n')
        finally:
            listener.close()
            try:
                current = os.lstat(self.path)
                if bound is not None and stat.S_ISSOCK(current.st_mode) and \
                        (current.st_dev, current.st_ino) == (bound.st_dev, bound.st_ino):
                    self.path.unlink()
            except FileNotFoundError:
                pass
