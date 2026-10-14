#!/usr/bin/env python3
"""Check traceability coverage of a committed Git diff, never product acceptance."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

BATCH_DIR = 'docs/change-gates/batches/'
ID = re.compile(r'REQ-MIMI-[A-Z]+-[0-9]{3}\Z')
STATUSES = {'passed', 'failed', 'not_run', 'blocked'}


class GateError(ValueError):
    pass


def git(root, *args):
    try:
        return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as e:
        raise GateError('Git comparison failed: ' + e.stderr.decode(errors='replace').strip()) from e


def text(value, field):
    if not isinstance(value, str) or not value.strip():
        raise GateError(field + ' must be nonempty text')
    return value


def path(value):
    text(value, 'path')
    p = PurePosixPath(value)
    if p.is_absolute() or value == '.' or str(p) != value or '..' in p.parts or '\\' in value or ':' in value or any(ord(c) < 32 for c in value):
        raise GateError('Unsafe repository path: ' + repr(value))
    return value


def regular_blob(root, commit, name):
    path(name)
    entry = git(root, 'ls-tree', '-z', commit, '--', name)
    rows = entry.rstrip(b'\0').split(b'\0') if entry else []
    if len(rows) != 1:
        raise GateError('Missing record: ' + name)
    metadata, actual = rows[0].split(b'\t', 1)
    if actual.decode() != name or not metadata.startswith((b'100644 blob ', b'100755 blob ')):
        raise GateError('Record must be a regular tracked file: ' + name)
    data = git(root, 'cat-file', 'blob', metadata.split()[2].decode())
    if len(data) > 1024 * 1024:
        raise GateError('Record exceeds 1 MiB: ' + name)
    return data.decode('utf-8')


def unique_object(pairs):
    result = {}
    for k, v in pairs:
        if k in result:
            raise GateError('Duplicate JSON key: ' + k)
        result[k] = v
    return result


def validate_batch(root, head, name, changed):
    try:
        batch = json.loads(regular_blob(root, head, name), object_pairs_hook=unique_object)
    except (json.JSONDecodeError, UnicodeError) as e:
        raise GateError('Invalid UTF-8 JSON batch: ' + name) from e
    if not isinstance(batch, dict) or type(batch.get('schema')) is not int or batch.get('schema') != 1:
        raise GateError('Unsupported batch schema: ' + name)
    for field in ('summary', 'context', 'next_steps'):
        text(batch.get(field), field)
    requirements = batch.get('requirements')
    if not isinstance(requirements, list) or not requirements:
        raise GateError('requirements must be a nonempty list: ' + name)
    covered = set()
    for requirement in requirements:
        if not isinstance(requirement, dict) or not ID.fullmatch(str(requirement.get('id', ''))):
            raise GateError('Invalid requirement ID: ' + name)
        record = path(requirement.get('record'))
        if record != 'docs/change-gates/' + requirement['id'] + '.md':
            raise GateError('Record path must match requirement ID')
        if record not in changed:
            raise GateError('Detailed record must be updated in this diff: ' + record)
        document = regular_blob(root, head, record)
        if requirement['id'] not in document:
            raise GateError('Detailed record does not identify its requirement: ' + record)
        for field in ('scope', 'invariants'):
            text(requirement.get(field), field)
        paths = requirement.get('paths')
        if not isinstance(paths, list) or not paths:
            raise GateError('paths must be a nonempty list')
        for item in paths:
            covered.add(path(item))
        checks = requirement.get('checks')
        if not isinstance(checks, list) or not checks:
            raise GateError('checks must be a nonempty list')
        for check in checks:
            if not isinstance(check, dict) or not isinstance(check.get('status'), str) or check['status'] not in STATUSES:
                raise GateError('Invalid check status')
            for field in ('name', 'scope', 'evidence'):
                text(check.get(field), field)
            if check['status'] != 'passed':
                text(check.get('reason'), 'reason')
    return covered


def check(root, base_ref, head_ref):
    base = git(root, 'rev-parse', '--verify', '--end-of-options', base_ref + '^{commit}').decode().strip()
    head = git(root, 'rev-parse', '--verify', '--end-of-options', head_ref + '^{commit}').decode().strip()
    # Do not accept a diverged or reversed baseline as the intended development range.
    git(root, 'merge-base', '--is-ancestor', base, head)
    raw = git(root, 'diff', '--name-only', '-z', '--no-renames', base, head, '--')
    changed = {p.decode('utf-8') for p in raw.split(b'\0') if p}
    batches = sorted(p for p in changed if p.startswith(BATCH_DIR) and p.endswith('.json'))
    old_tree = git(root, 'ls-tree', '-r', '--name-only', '-z', base, '--', BATCH_DIR)
    old_records = {p.decode('utf-8') for p in old_tree.split(b'\0') if p}
    if set(batches) & old_records:
        raise GateError('Historical batches are immutable; append a new batch: ' + ', '.join(sorted(set(batches) & old_records)))
    covered = set()
    for name in batches:
        covered.update(validate_batch(root, head, name, changed))
    missing = sorted(changed - covered)
    if missing:
        raise GateError('Changes without traceability coverage: ' + ', '.join(missing))
    return {'schema': 1, 'base_commit': base, 'head_commit': head,
            'status': 'passed' if changed else 'not_run',
            'scope': 'Committed file coverage and record structure only; human semantic/evidence review is still required',
            'changed_files': sorted(changed), 'batches': batches,
            'diff_sha256': hashlib.sha256(git(root, 'diff', '--binary', '--no-ext-diff', '--no-textconv', '--no-renames', base, head, '--')).hexdigest(),
            'release_qualified': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--base', required=True)
    p.add_argument('--head', default='HEAD')
    a = p.parse_args()
    try:
        print(json.dumps(check(a.root, a.base, a.head), ensure_ascii=False, indent=2))
    except (GateError, UnicodeError, TypeError) as e:
        p.exit(1, 'TRACEABILITY BLOCKED: ' + str(e) + '\n')


if __name__ == '__main__':
    main()
