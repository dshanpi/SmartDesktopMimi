"""Enforce desktop/platform source ownership; this is not a migration certificate."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath

LOCK = 'docs/update/source-ownership.json'
KINDS = {'application', 'platform', 'shared-frozen', 'platform-client-frozen'}
EXTENSIONS = {'.c', '.cpp', '.h', '.hpp'}
PREFIX = 'apps/lv_port_linux/'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate ownership key: ' + key)
            result[key] = value
        return result
    return json.loads(path.read_text(), object_pairs_hook=unique)


def safe_name(name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or str(p) != name or '..' in p.parts or '\\' in name:
        raise ValueError('unsafe ownership path: ' + name)
    return name


def source_file(root, name):
    safe_name(name)
    path = root / name
    for parent in (path,) + tuple(path.parents):
        if parent == root:
            break
        if parent.is_symlink():
            raise ValueError('ownership source is a symlink: ' + name)
    if not path.is_file():
        raise ValueError('missing ownership source: ' + name)
    return path


def verify(root):
    root = root.resolve()
    policy = load(root / LOCK)
    if policy.get('schema_version') != 1 or policy.get('platform_id') != 'a133':
        raise ValueError('unknown source ownership schema/platform')
    if not policy.get('files') or not policy.get('scopes'):
        raise ValueError('empty source ownership inventory')
    for name in policy.get('retired_platform_sources', {}):
        path = root / safe_name(name)
        if path.exists() or path.is_symlink():
            raise ValueError('retired platform implementation reintroduced: ' + name)
    actual = set()
    for name in policy['scopes']:
        path = root / safe_name(name)
        if path.is_symlink() or not path.is_dir():
            raise ValueError('missing/linked ownership scope: ' + name)
        for file in path.rglob('*'):
            if file.suffix in EXTENSIONS and (file.is_file() or file.is_symlink()):
                actual.add(file.relative_to(root).as_posix())
    declared = set(policy['files'])
    missing = actual - declared
    if missing:
        raise ValueError('unclassified boundary sources: ' + ', '.join(sorted(missing)))
    for name, entry in policy['files'].items():
        path = source_file(root, name)
        if entry.get('owner') not in KINDS:
            raise ValueError('unknown owner: ' + name)
        if entry['owner'].endswith('-frozen') and entry.get('sha256') != digest(path):
            raise ValueError('frozen platform/shared source changed: ' + name)
    return policy


def owner(policy, name):
    if name in policy['files']:
        return policy['files'][name]['owner']
    # Third-party LVGL and product application code can evolve independently.
    # Sensitive directories below are explicit, never covered by a broad src/ rule.
    if name == PREFIX + 'src/main.c' or name.startswith(tuple(PREFIX + p for p in (
            'lvgl/', 'src/ui/', 'src/apps/'))):
        return 'application'
    raise ValueError('unclassified compilation source: ' + name)


def audit_commands(root, commands):
    root = root.resolve()
    policy = verify(root)
    if not commands:
        raise ValueError('empty compilation inventory')
    checked = set()
    for entry in commands:
        path = Path(entry['file'])
        if not path.is_absolute():
            path = Path(entry['directory']) / path
        try:
            # Check the lexical path as well as the resolved one: an alias must
            # not conceal a platform source or escape the reviewed source tree.
            name = path.relative_to(root).as_posix()
            source_file(root, name)
            path.resolve().relative_to(root)
        except ValueError as error:
            raise ValueError('untrusted compilation source: ' + str(path)) from error
        if owner(policy, name) == 'platform':
            raise ValueError('platform service entered application build: ' + name)
        checked.add(name)
    return {'source_ownership_sha256': digest(root / LOCK),
            'compiled_source_count': len(checked)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    p.add_argument('--commands', type=Path)
    a = p.parse_args()
    try:
        result = audit_commands(a.root, load(a.commands)) if a.commands else verify(a.root)
    except (ValueError, OSError, KeyError) as error:
        p.exit(1, 'SOURCE OWNERSHIP BLOCKED: ' + str(error) + '\n')
    print(json.dumps({'status': 'passed', 'ownership_sha256': digest(a.root / LOCK),
                      'migration_complete': False,
                      'compiled_source_count': result.get('compiled_source_count')}))


if __name__ == '__main__':
    main()
