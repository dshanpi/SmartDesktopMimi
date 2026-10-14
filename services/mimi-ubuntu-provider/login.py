#!/usr/bin/python3
"""Root platform bridge for one enrolled user's actual local X11 login."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import pwd
import re
import signal
import stat
import subprocess
import time

ID = re.compile(r'[A-Za-z0-9]{1,32}\Z')
DISPLAY = re.compile(r':[0-9]{1,5}(?:\.[0-9])?\Z')
SHA = re.compile(r'[a-f0-9]{64}\Z')
stop_requested = False


def absolute(value):
    if not isinstance(value, str) or not value.startswith('/') or str(Path(value)) != value or '..' in Path(value).parts or any(ord(c) < 32 for c in value):
        raise ValueError('noncanonical platform path')
    return Path(value)


def trusted(path):
    path = absolute(str(path))
    for p in (path, *path.parents):
        s = p.lstat()
        if s.st_uid != 0 or stat.S_ISLNK(s.st_mode) or s.st_mode & 0o022:
            raise ValueError('untrusted platform path')
    return path


def read_file(path, uid, limit, private=False):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        s = os.fstat(fd)
        if not stat.S_ISREG(s.st_mode) or s.st_uid != uid or s.st_nlink != 1 or s.st_size > limit or (private and stat.S_IMODE(s.st_mode) != 0o600):
            raise ValueError('unsafe platform file')
        with os.fdopen(os.dup(fd), 'rb') as stream:
            raw = stream.read(limit + 1)
        after = os.fstat(fd)
        if len(raw) > limit or (s.st_ino, s.st_size, s.st_mtime_ns, s.st_ctime_ns) != (after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns):
            raise ValueError('platform file changed during read')
        return raw
    finally:
        os.close(fd)


def object_pairs(pairs):
    d = {}
    for k, v in pairs:
        if k in d:
            raise ValueError('duplicate configuration field')
        d[k] = v
    return d


def read_json(path):
    trusted(path)
    d = json.loads(read_file(path, 0, 65536), object_pairs_hook=object_pairs)
    if not isinstance(d, dict):
        raise ValueError('configuration must be an object')
    return d


def command(args, timeout=12):
    trusted(args[0])
    return subprocess.run(args, env={'PATH': '/usr/bin:/bin', 'LANG': 'C'}, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, check=True).stdout


def properties(raw):
    result = {}
    for line in raw.decode('utf-8').splitlines():
        k, sep, v = line.partition('=')
        if not sep or k in result:
            raise ValueError('ambiguous logind properties')
        result[k] = v
    return result


def allowed_session(s, uid):
    required = {'User': str(uid), 'Seat': 'seat0', 'Remote': 'no', 'Active': 'yes', 'Class': 'user', 'Type': 'x11'}
    return (all(s.get(k) == v for k, v in required.items()) and ID.fullmatch(s.get('Id', ''))
            and DISPLAY.fullmatch(s.get('Display', '')) and s.get('Scope') == 'session-' + s['Id'] + '.scope')


def select_session(sessions, uid):
    matches = [s for s in sessions if allowed_session(s, uid)]
    if len(matches) > 1:
        raise ValueError('multiple active graphical sessions')
    return matches[0] if matches else None


def discover(uid):
    rows = command(['/usr/bin/loginctl', 'list-sessions', '--no-legend', '--no-pager']).decode().splitlines()
    sessions = []
    if len(rows) > 128:
        raise ValueError('too many login sessions')
    for row in rows:
        fields = row.split()
        if len(fields) < 4 or not ID.fullmatch(fields[0]) or fields[1] != str(uid) or fields[3] != 'seat0':
            continue
        keys = ['Id', 'User', 'Seat', 'Remote', 'Active', 'Class', 'Type', 'Display', 'Scope', 'Service']
        p = command(['/usr/bin/loginctl', 'show-session', fields[0], *['--property=' + k for k in keys]])
        values = properties(p)
        if set(values) != set(keys) or values['Id'] != fields[0] or values['User'] != str(uid):
            raise ValueError('incomplete or changed logind projection')
        sessions.append(values)
    return select_session(sessions, uid)


def authority_source(session, uid):
    service = session.get('Service')
    if service in ('gdm-password', 'gdm-autologin'):
        return Path('/run/user') / str(uid) / 'gdm/Xauthority'
    if service == 'lightdm':
        return absolute(pwd.getpwuid(uid).pw_dir) / '.Xauthority'
    raise ValueError('unsupported graphical display manager')


def read_authority(path, uid):
    # Resolve each directory through its open parent, so user rename/symlink
    # races cannot turn a reviewed source into an unrelated root file.
    path = absolute(str(path))
    fd = os.open('/', os.O_RDONLY | os.O_DIRECTORY)
    try:
        for name in path.parts[1:-1]:
            next_fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
            os.close(fd)
            fd = next_fd
            s = os.fstat(fd)
            if s.st_uid not in (0, uid) or s.st_mode & 0o022:
                raise ValueError('unsafe display authorization ancestor')
        leaf = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
        try:
            s = os.fstat(leaf)
            if s.st_uid != uid or not stat.S_ISREG(s.st_mode) or stat.S_IMODE(s.st_mode) != 0o600 or s.st_nlink != 1 or not 0 < s.st_size <= 65536:
                raise ValueError('unsafe display authorization')
            raw = os.read(leaf, 65537)
            after = os.fstat(leaf)
            if len(raw) != s.st_size or (s.st_mtime_ns, s.st_ctime_ns) != (after.st_mtime_ns, after.st_ctime_ns):
                raise ValueError('display authorization changed')
            return raw
        finally:
            os.close(leaf)
    finally:
        os.close(fd)


def atomic(path, raw, uid=0):
    trusted(path.parent)
    temp = path.with_name(path.name + '.pending')
    fd = os.open(temp, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    try:
        os.fchown(fd, uid, -1)
        with os.fdopen(os.dup(fd), 'wb') as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
        parent = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(parent)
        finally:
            os.close(parent)
    finally:
        os.close(fd)
        if temp.exists():
            temp.unlink()


class Bridge:
    def __init__(self, path):
        self.config = read_json(path)
        if set(self.config) != {'schema_version', 'updater', 'updater_sha256', 'updater_config', 'session_profile'} or self.config['schema_version'] != 1:
            raise ValueError('invalid login configuration')
        self.updater = trusted(self.config['updater'])
        expected = self.config['updater_sha256']
        if not isinstance(expected, str) or not SHA.fullmatch(expected):
            raise ValueError('invalid updater pin')
        self.updater_config = trusted(self.config['updater_config'])
        self.platform = read_json(self.updater_config)
        self.profile = read_json(self.config['session_profile'])
        fields = {'schema_version', 'uid', 'gid', 'unit', 'script', 'script_sha256', 'provider', 'provider_sha256', 'runtime', 'settings', 'screen'}
        if set(self.profile) != fields or self.profile['schema_version'] != 1 or type(self.profile['uid']) is not int or self.profile['uid'] <= 0:
            raise ValueError('invalid graphical user profile')
        self.uid = self.profile['uid']
        if pwd.getpwuid(self.uid).pw_gid != self.profile['gid'] or not re.fullmatch(r'mimi-ubuntu-[a-z0-9-]{1,32}\.service', self.profile['unit']):
            raise ValueError('invalid graphical user identity')
        self.session_file = absolute(self.platform['ubuntu_session_file'])
        self.base = self.session_file.parent
        if not re.fullmatch(r'/run/mimi-ubuntu-login-[a-z0-9-]{1,32}', str(self.base)) or self.session_file.name != 'session.json' or self.profile['runtime'] != str(self.base / 'session'):
            raise ValueError('login runtime must be fixed platform directory')
        self.lock_path = trusted(absolute(self.platform['lock']).parent) / absolute(self.platform['lock']).name
        self.auth = self.base / 'display.auth'
        self.last = None
        self.last_result = None
        self.verify_updater()

    def verify_updater(self):
        trusted(self.updater)
        actual = hashlib.sha256(read_file(self.updater, 0, 64 * 1024 * 1024)).hexdigest()
        if actual != self.config['updater_sha256']:
            raise ValueError('pinned updater changed')

    def transaction(self):
        fd = os.open(self.lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        s = os.fstat(fd)
        if s.st_uid != 0 or not stat.S_ISREG(s.st_mode) or s.st_nlink != 1 or stat.S_IMODE(s.st_mode) != 0o600:
            os.close(fd)
            raise ValueError('unsafe update transaction lock')
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BaseException:
            os.close(fd)
            raise
        return fd

    def invoke(self, *args):
        self.verify_updater()
        return command([str(self.updater), args[0], '--config', str(self.updater_config), *args[1:]], timeout=45)

    def prepare(self, session):
        if not allowed_session(session, self.uid):
            raise ValueError('graphical session changed')
        raw = read_authority(authority_source(session, self.uid), self.uid)
        if not self.base.exists():
            self.base.mkdir(mode=0o755)
        trusted(self.base)
        runtime = self.base / 'session'
        if not runtime.exists():
            runtime.mkdir(mode=0o700)
            os.chown(runtime, self.uid, self.profile['gid'])
        s = runtime.lstat()
        if not stat.S_ISDIR(s.st_mode) or s.st_uid != self.uid or stat.S_IMODE(s.st_mode) != 0o700:
            raise ValueError('unsafe user runtime')
        fd = self.transaction()
        try:
            if self.session_file.exists():
                self.invoke('platform', 'stop')
            atomic(self.auth, raw, self.uid)
            profile = dict(self.profile, display=session['Display'], xauthority=str(self.auth), login_session=session['Id'])
            atomic(self.session_file, (json.dumps(profile, sort_keys=True) + '\n').encode())
        finally:
            os.close(fd)

    def cleanup(self):
        if not self.session_file.exists():
            self.last = None
            return
        fd = self.transaction()
        try:
            self.invoke('platform', 'stop')
            for p in (self.auth, self.session_file):
                if p.exists():
                    p.unlink()
            self.last = None
        finally:
            os.close(fd)

    def tick(self):
        session = discover(self.uid)
        if session is None:
            self.cleanup()
            return 'waiting-for-local-x11-login'
        if session['Id'] == self.last:
            return getattr(self, 'last_result', None) or 'login-already-handled'
        self.prepare(session)
        self.last = session['Id']
        # StartSelected takes the shared lock itself and rechecks logind and
        # signatures. Failure is not silently retried within the same login.
        try:
            self.invoke('start-selected')
        except (OSError, ValueError, subprocess.SubprocessError):
            self.last_result = 'login-start-failed-no-retry'
            raise
        self.last_result = 'selected-signed-desktop-started'
        return self.last_result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True)
    p.add_argument('--probe', action='store_true', help='Only inspect real logind; no launch or configuration writes')
    args = p.parse_args()
    if os.getuid() != 0:
        p.exit(1, 'root platform service required\n')
    bridge = Bridge(args.config)
    if args.probe:
        session = discover(bridge.uid)
        print(json.dumps({'status': 'eligible' if session else 'waiting-for-local-x11-login', 'session_id': session['Id'] if session else None}))
        return
    def terminate(*_):
        global stop_requested
        stop_requested = True
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    previous = None
    try:
        while not stop_requested:
            try:
                status = bridge.tick()
            except BlockingIOError:
                status = 'waiting-for-update-transaction'
            except (OSError, ValueError, subprocess.SubprocessError, KeyError):
                status = 'login-start-failed'
            if status != previous:
                print(status, flush=True)
                previous = status
            time.sleep(2)
    finally:
        # A busy transaction leaves the root-owned profile intact for the next
        # service instance. BindsTo stops its UI on logout even during updates.
        bridge.cleanup()


if __name__ == '__main__':
    main()
