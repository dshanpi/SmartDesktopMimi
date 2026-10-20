"""Internal root storage backend; callers MUST authenticate/authorize first.

No CLI or socket endpoint. An admission identity comes from a trusted platform
broker, never from native application arguments. This module does not grant a
capability or enable native installation.
"""
import json
import os
import re
import stat
import uuid

STATE = 'storage-v1.json'
MAX_BYTES = 65536
MAX_KEYS = 128
KEY = re.compile(r'[A-Za-z0-9._-]{1,64}\Z')
APP = re.compile(r'[a-zA-Z0-9][a-zA-Z0-9._-]{1,127}\Z')
TEMP = re.compile(r'\.storage-v1\.[0-9a-f]{32}\.tmp\Z')


def fail(message):
    raise ValueError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            fail('duplicate storage field')
        result[key] = value
    return result


def encoded(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'),
                      allow_nan=False).encode('utf-8')


def text_value(key, value):
    if not isinstance(key, str) or not KEY.fullmatch(key):
        fail('invalid storage key')
    if not isinstance(value, str) or '\0' in value:
        fail('storage value must be NUL-free text')
    # Bound both the SDK request and its wrapped response, including escaping.
    if len(value.encode('utf-8')) > 4096 or len(encoded(value)) > 3500:
        fail('storage value exceeds SDK transport budget')


class Storage:
    def __init__(self, installer):
        self.installer = installer

    def checkpoint(self, phase):
        """Overridden only by process-interruption tests, never by CLI/env."""

    def private_file(self, path):
        # tree_safe already rejects symlinks, hardlinks and writable ancestry.
        s = path.lstat()
        if not stat.S_ISREG(s.st_mode) or s.st_uid != 0 or s.st_nlink != 1 or s.st_mode & 0o077:
            fail('storage state must be a private root file')
        if s.st_size > MAX_BYTES:
            fail('storage state exceeds quota')

    def read_state(self, data):
        path = data / STATE
        if not path.exists() and not path.is_symlink():
            return {'schema': 1, 'values': {}}
        self.private_file(path)
        fd = os.open(str(path), os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        try:
            raw = os.read(fd, MAX_BYTES + 1)
        finally:
            os.close(fd)
        if len(raw) > MAX_BYTES:
            fail('storage state exceeds quota')
        state = json.loads(raw.decode('utf-8'), object_pairs_hook=unique)
        if not isinstance(state, dict) or set(state) != {'schema', 'values'} or type(state['schema']) is not int or state['schema'] != 1:
            fail('invalid storage state schema')
        values = state['values']
        if not isinstance(values, dict) or len(values) > MAX_KEYS:
            fail('invalid storage values or quota')
        for key, value in values.items():
            text_value(key, value)
        return state

    @staticmethod
    def sync_dir(path):
        fd = os.open(str(path), os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)

    def operate(self, identity, arguments):
        """Use a trusted, previously authorized (id, publisher, digest) tuple.

        The digest is Installer.installed_digest after full signed admission.
        Each operation pins that installation under its lock; a caller must
        re-admit after an application upgrade, never silently rebind itself.
        """
        if not isinstance(identity, tuple) or len(identity) != 3:
            fail('trusted installation identity required')
        app_id, publisher, digest = identity
        if not isinstance(app_id, str) or not APP.fullmatch(app_id) or app_id in ('.', '..'):
            fail('invalid application identity')
        if any(not isinstance(x, str) or not re.fullmatch(r'[0-9a-f]{64}', x) for x in (publisher, digest)):
            fail('invalid installation pin')
        if not isinstance(arguments, dict) or arguments.get('op') not in ('read', 'write'):
            fail('unknown storage operation')
        writing = arguments['op'] == 'write'
        if set(arguments) != ({'op', 'key', 'value'} if writing else {'op', 'key'}):
            fail('unknown storage arguments')
        key = arguments['key']
        text_value(key, arguments['value'] if writing else '')
        i = self.installer
        with i.locked():
            i.recover()
            target = i.root / app_id
            i.tree_safe(target)
            if i.installed_digest(target) != digest or (target / '.aitvbox-publisher-key.sha256').read_text().strip() != publisher:
                fail('installation changed; new admission required')
            data = target / 'data'
            i.tree_safe(data, data=True)
            if data.stat().st_mode & 0o077:
                fail('storage directory must be private')
            # Validate committed bytes before any cleanup or mutation.
            state = self.read_state(data)
            stale = [p for p in data.iterdir() if TEMP.fullmatch(p.name)]
            for p in stale:
                self.private_file(p)
            for p in stale:
                p.unlink()
            if stale:
                self.sync_dir(data)
            if not writing:
                return {'found': key in state['values'], **({'value': state['values'][key]} if key in state['values'] else {})}
            state['values'][key] = arguments['value']
            raw = encoded(state)
            if len(state['values']) > MAX_KEYS or len(raw) > MAX_BYTES:
                fail('storage quota exceeded')
            # The complete application's existing data also has a quota. An
            # atomic replacement temporarily needs one extra file's space.
            sizes = sum(p.stat().st_size for p in data.rglob('*') if p.is_file())
            count = sum(1 for p in data.rglob('*') if p.is_file())
            if sizes + len(raw) > 64 * 1024 * 1024 or count + 1 > 4096:
                fail('application data quota exceeded')
            temp = data / ('.storage-v1.' + uuid.uuid4().hex + '.tmp')
            fd = os.open(str(temp), os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
            try:
                with os.fdopen(fd, 'wb') as out:
                    out.write(raw)
                    out.flush()
                    os.fsync(out.fileno())
                self.checkpoint('prepared')
                os.replace(str(temp), str(data / STATE))
                self.checkpoint('replaced')
                self.sync_dir(data)
                self.checkpoint('committed')
            finally:
                if temp.exists():
                    temp.unlink()
                    self.sync_dir(data)
            return {'written': True}
