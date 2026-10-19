#!/usr/bin/env python3
"""Root-managed Ubuntu extension installation, independent of desktop payloads.

Schema 1 without platform permissions is admitted initially. Native execution
and broker integration are separate requirements, never inferred from install.
"""
import argparse
import contextlib
import fcntl
import hashlib
import gzip
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile
import tarfile
import uuid

PACKAGER = Path(__file__).resolve().parents[2] / 'tools/aitapp/aitapp.py'
JOURNAL = '.transaction.json'
MARKER = '.aitvbox-publisher-key.sha256'
MAX_DATA = 64 * 1024 * 1024


def reject(message):
    raise ValueError(message)


def secure(path, directory=False):
    """No untrusted path component may redirect root's subsequent operations."""
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts or str(path) == '/':
        reject('absolute non-root controlled path required')
    for parent in reversed(path.parents):
        s = parent.lstat()
        if not stat.S_ISDIR(s.st_mode) or s.st_uid != 0 or s.st_mode & 0o022:
            reject('untrusted parent directory')
    s = path.lstat()
    kind = stat.S_ISDIR if directory else stat.S_ISREG
    if not kind(s.st_mode) or s.st_uid != 0 or s.st_mode & 0o022:
        reject('untrusted path ownership, mode or type')
    if not directory and s.st_nlink != 1:
        reject('hard linked controlled file')
    return path


def duplicate_free(pairs):
    out = {}
    for k, v in pairs:
        if k in out:
            reject('duplicate JSON field')
        out[k] = v
    return out


def load_json(path):
    if path.stat().st_size > 65536:
        reject('configuration or journal too large')
    return json.loads(path.read_text(), object_pairs_hook=duplicate_free)


def sync_dir(path):
    fd = os.open(str(path), os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def sync_tree(path):
    for f in path.rglob('*'):
        if f.is_file():
            fd = os.open(str(f), os.O_RDONLY | os.O_NOFOLLOW)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
    for d in sorted((p for p in path.rglob('*') if p.is_dir()),
                    key=lambda p: len(p.parts), reverse=True):
        sync_dir(d)
    sync_dir(path)


class Installer:
    def __init__(self, config):
        if os.geteuid() != 0:
            reject('platform installation requires root')
        cfg = load_json(secure(config))
        if not isinstance(cfg, dict) or set(cfg) != {'schema', 'apps_root', 'trusted_keys'} or type(cfg['schema']) is not int or cfg['schema'] != 1:
            reject('unknown installer configuration')
        if not isinstance(cfg['apps_root'], str) or not isinstance(cfg['trusted_keys'], list) or not 1 <= len(cfg['trusted_keys']) <= 16 or not all(isinstance(k, str) for k in cfg['trusted_keys']):
            reject('invalid platform paths')
        self.root = secure(cfg['apps_root'], directory=True)
        self.keys = [secure(k) for k in cfg['trusted_keys']]
        if any(self.root == p or self.root in p.parents for p in [Path(config)] + self.keys):
            reject('installer control files must be outside application root')
        # The same package parser/limits/signature format as the build tool.
        spec = importlib.util.spec_from_file_location('aitapp_package', PACKAGER)
        self.package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.package)

    @contextlib.contextmanager
    def locked(self):
        path = self.root / '.appctl.lock'
        fd = os.open(str(path), os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
        try:
            s = os.fstat(fd)
            if not stat.S_ISREG(s.st_mode) or s.st_uid != 0 or s.st_nlink != 1 or s.st_mode & 0o077:
                reject('unsafe transaction lock')
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            yield
        finally:
            os.close(fd)

    def checkpoint(self, phase):
        """Override only in process-kill tests; no CLI/env failure bypass."""

    def tree_safe(self, path, data=False):
        secure(path, directory=True)
        total = count = 0
        for p in path.rglob('*'):
            s = p.lstat()
            if stat.S_ISDIR(s.st_mode):
                secure(p, directory=True)
            else:
                secure(p)
                total += s.st_size
                count += 1
                if count > 4096 or total > (MAX_DATA if data else 128 * 1024 * 1024):
                    reject('installed tree exceeds quota')

    def remove(self, path):
        # Only paths produced by this transaction, never arbitrary CLI strings.
        self.tree_safe(path)
        shutil.rmtree(path)

    def recover(self):
        journal = self.root / JOURNAL
        tmp = self.root / '.transaction.tmp'
        if tmp.exists() or tmp.is_symlink():
            secure(tmp)
            tmp.unlink()
            sync_dir(self.root)
        if not journal.exists() and not journal.is_symlink():
            return False
        j = load_json(secure(journal))
        if not isinstance(j, dict) or set(j) != {'schema', 'app', 'token', 'previous', 'previous_digest'} or type(j['schema']) is not int or j['schema'] != 1 or type(j['previous']) is not bool or not isinstance(j['app'], str) or not self.package.APP_ID.fullmatch(j['app']) or len(j['app']) > 96 or not isinstance(j['token'], str) or not re.fullmatch('[0-9a-f]{32}', j['token']):
            reject('invalid recovery journal')
        if j['previous']:
            if not isinstance(j['previous_digest'], str) or not re.fullmatch('[0-9a-f]{64}', j['previous_digest']):
                reject('invalid previous installation fingerprint')
        elif j['previous_digest'] is not None:
            reject('unexpected previous fingerprint')
        target = self.root / j['app']
        stage = self.root / ('.stage-' + j['token'])
        old = self.root / ('.old-' + j['token'])
        for p in (target, stage, old):
            if p.exists() or p.is_symlink():
                self.tree_safe(p)
        if j['previous']:
            prior = old if old.exists() else target
            if not prior.exists() or self.installed_digest(prior) != j['previous_digest']:
                reject('prior installation fingerprint mismatch')
            if old.exists():
                if target.exists():
                    self.remove(target)
                os.replace(old, target)
        elif target.exists():
            if stage.exists():
                reject('ambiguous initial install state')
            self.remove(target)
        if stage.exists():
            self.remove(stage)
        sync_dir(self.root)
        journal.unlink()
        sync_dir(self.root)
        return True

    def installed_digest(self, path):
        return hashlib.sha256(secure(path / 'SHA256SUMS').read_bytes() + secure(path / MARKER).read_bytes()).hexdigest()

    def verify_stage(self, stage):
        m = self.package.load_manifest(stage)
        if type(m['schema']) is not int or m['schema'] != 1:
            reject('Ubuntu native target and runtime not yet admitted')
        if m['permissions']:
            reject('required platform capabilities unavailable')
        files = self.package.payload_files(stage, m)
        if self.package.checksums(stage, files) != (stage / 'SHA256SUMS').read_bytes():
            reject('payload checksum list mismatch')
        signature = stage / 'SIGNATURE'
        if not signature.is_file() or not 0 < signature.stat().st_size <= 16384:
            reject('signature missing or oversized')
        for key in self.keys:
            result = subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(key), '-signature', str(signature), str(stage / 'SHA256SUMS')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                der = subprocess.check_output(['openssl', 'pkey', '-pubin', '-in', str(key), '-outform', 'DER'], stderr=subprocess.DEVNULL)
                return m, hashlib.sha256(der).hexdigest()
        reject('untrusted publisher signature')

    def install(self, package):
        with self.locked():
            self.recover()
            token = uuid.uuid4().hex
            stage = self.root / ('.stage-' + token)
            old = self.root / ('.old-' + token)
            stage.mkdir(mode=0o700)
            journal_written = False
            try:
                # Copy to a private bounded snapshot: subsequent checks never
                # reread caller-controlled package bytes after validation.
                fd = os.open(str(package), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
                try:
                    if not stat.S_ISREG(os.fstat(fd).st_mode):
                        reject('package must be a regular file')
                    with tempfile.TemporaryFile(dir=str(self.root)) as snapshot:
                        total = 0
                        while True:
                            chunk = os.read(fd, 65536)
                            if not chunk:
                                break
                            total += len(chunk)
                            if total > self.package.MAX_PACKAGE_BYTES:
                                reject('package exceeds limit')
                            snapshot.write(chunk)
                        snapshot.flush()
                        snapshot.seek(0)
                        with gzip.GzipFile(fileobj=snapshot) as decoded:
                            class BoundedStream:
                                remaining = 66 * 1024 * 1024

                                def read(self, size):
                                    data = decoded.read(min(size, self.remaining + 1))
                                    self.remaining -= len(data)
                                    if self.remaining < 0:
                                        reject('decompressed archive exceeds limit')
                                    return data
                            with tarfile.open(fileobj=BoundedStream(), mode='r|') as archive:
                                count = total_bytes = 0
                                for member in archive:
                                    count += 1
                                    total_bytes += member.size
                                    if count > 256 or member.size > self.package.MAX_FILE_BYTES or total_bytes > self.package.MAX_EXTRACTED_BYTES:
                                        reject('archive count or size exceeds limit')
                                    name = member.name
                                    if not name or name != '/'.join(Path(name).parts) or name.startswith('/'):
                                        reject('noncanonical archive path')
                        self.package.safe_extract(Path('/proc/self/fd/' + str(snapshot.fileno())), stage)
                finally:
                    os.close(fd)
                # Canonical members cannot rename/change the private stage root.
                # Validate original executable bits before normalizing metadata.
                for p in stage.rglob('*'):
                    mode = p.stat().st_mode
                    os.chown(p, 0, 0)
                    p.chmod(0o755 if p.is_dir() else (0o644 | (mode & 0o111)))
                m, fingerprint = self.verify_stage(stage)
                for p in stage.rglob('*'):
                    os.chown(p, 0, 0)
                    p.chmod(0o755 if p.is_dir() else 0o444)
                target = self.root / m['id']
                previous = target.exists() or target.is_symlink()
                if previous:
                    self.tree_safe(target)
                    stored = secure(target / MARKER).read_text().strip()
                    before = self.package.load_manifest(target)
                    if before['id'] != m['id'] or stored != fingerprint:
                        reject('application publisher ownership mismatch')
                    if tuple(map(int, m['version'].split('.'))) <= tuple(map(int, before['version'].split('.'))):
                        reject('application version must increase')
                    data = target / 'data'
                    if data.exists() or data.is_symlink():
                        self.tree_safe(data, data=True)
                        shutil.copytree(data, stage / 'data')
                (stage / 'data').mkdir(mode=0o700, exist_ok=True)
                (stage / MARKER).write_text(fingerprint + '\n')
                stage.chmod(0o755)
                sync_tree(stage)
                j = {'schema': 1, 'app': m['id'], 'token': token, 'previous': previous, 'previous_digest': self.installed_digest(target) if previous else None}
                tmp = self.root / '.transaction.tmp'
                fd = os.open(str(tmp), os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
                with os.fdopen(fd, 'w') as f:
                    json.dump(j, f); f.write('\n'); f.flush(); os.fsync(f.fileno())
                os.replace(tmp, self.root / JOURNAL)
                sync_dir(self.root)
                journal_written = True
                self.checkpoint('prepared')
                if previous:
                    os.replace(target, old)
                    sync_dir(self.root)
                self.checkpoint('old-moved')
                os.replace(stage, target)
                sync_dir(self.root)
                self.checkpoint('new-moved')
                (self.root / JOURNAL).unlink()
                sync_dir(self.root)
                journal_written = False
                self.checkpoint('committed')
                if old.exists():
                    self.remove(old)
                    sync_dir(self.root)
                return {'id': m['id'], 'version': m['version'], 'publisher_sha256': fingerprint, 'runtime': 'declarative', 'status': 'installed'}
            finally:
                if not journal_written and stage.exists():
                    self.remove(stage)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--config', required=True)
    sub = p.add_subparsers(dest='operation', required=True)
    sub.add_parser('install').add_argument('package')
    sub.add_parser('recover')
    args = p.parse_args()
    # A developer checkout is not a privileged installed platform program.
    secure(Path(__file__))
    secure(PACKAGER)
    i = Installer(args.config)
    if args.operation == 'install':
        result = i.install(args.package)
    else:
        with i.locked():
            result = {'recovered': i.recover()}
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, tarfile.TarError, subprocess.SubprocessError) as exc:
        raise SystemExit('mimi-ubuntu-apps: ' + str(exc))
