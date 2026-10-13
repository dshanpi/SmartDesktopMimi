#!/usr/bin/env python3
"""Assemble independently gated targets into one signed application release."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from release_gate import POLICY, check, load, sha

ID = re.compile(r'[a-z0-9][a-z0-9-]{0,47}')


def check_collection(directory, stage, checker, trust):
    lock = load(directory / 'release-lock.json')
    if lock.get('schema_version') != 2 or lock.get('policy') != POLICY or lock.get('release_type') != 'application':
        raise ValueError('unsupported collection lock')
    assets = lock['assets']
    if not {'release-index.json', 'release-index.sig'} <= assets.keys():
        raise ValueError('missing signed index')
    for name, digest in assets.items():
        if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,200}', name) or name == 'release-lock.json':
            raise ValueError('invalid collection asset name')
        p = directory / name
        if p.is_symlink() or not p.is_file() or sha(p) != digest:
            raise ValueError('collection asset hash mismatch: ' + name)
    index = load(directory / 'release-index.json')
    if lock['tag'] != 'v' + lock['version'] or any(index[k] != lock[k] for k in ['version', 'channel', 'repository']):
        raise ValueError('collection/index identity mismatch')
    if index['commit'] != lock['source_commit']:
        raise ValueError('collection/index source mismatch')
    subprocess.run([str(checker), 'verify-index', '--manifest', str(directory/'release-index.json'),
                    '--signature', str(directory/'release-index.sig'), '--source', str(directory),
                    '--trust', str(trust)], check=True)
    if stage == 'prerelease' and index['channel'] != 'testing':
        raise ValueError('prerelease requires testing-channel index')
    if stage == 'stable' and index['channel'] != 'stable':
        raise ValueError('stable requires stable-channel index')
    entries = index['packages']
    ids = [entry['id'] for entry in entries]
    if not 1 <= len(ids) <= 32 or len(set(ids)) != len(ids) or any(not ID.fullmatch(i) for i in ids):
        raise ValueError('invalid collection package IDs')
    if lock['packages'] != ids:
        raise ValueError('collection target inventory mismatch')
    expected = {'release-index.json', 'release-index.sig'}
    for entry in entries:
        package_id = entry['id']
        lock_name = package_id + '--release-lock.json'
        if lock_name not in assets:
            raise ValueError('missing target release lock')
        expected.add(lock_name)
        child = load(directory / lock_name)
        if child.get('schema_version') != 1 or child.get('release_type') != 'application':
            raise ValueError('collection child must be an application package')
        if any(child[k] != lock[k] for k in ['version', 'source_commit', 'tag']):
            raise ValueError('target source/version mismatch')
        with tempfile.TemporaryDirectory(prefix='mimi-target-gate-') as temp:
            target = Path(temp)
            shutil.copyfile(directory/lock_name, target/'release-lock.json')
            for name, digest in child['assets'].items():
                if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,140}', name) or name == 'release-lock.json':
                    raise ValueError('invalid child asset name')
                mapped = package_id + '--' + name
                if assets.get(mapped) != digest:
                    raise ValueError('child/collection asset mismatch')
                expected.add(mapped)
                shutil.copyfile(directory/mapped, target/name)
            check(target, stage, checker, trust)
            manifest = load(target/'manifest.json')
            if manifest['channel'] != lock['channel'] or manifest['sequence'] != index['sequence']:
                raise ValueError('target channel/sequence mismatch')
    if expected != set(assets):
        raise ValueError('undeclared collection assets')
    return lock


def assemble(targets, output, repository, key_id, key, checker, trust):
    if output.exists():
        raise ValueError('output already exists; never replace a release')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('invalid repository')
    ids = [i for i, _ in targets]
    if not 1 <= len(ids) <= 32 or len(ids) != len(set(ids)) or any(not ID.fullmatch(i) for i in ids):
        raise ValueError('invalid/duplicate target IDs')
    # Validate inputs before creating output. Each target retains its original lock/evidence.
    inputs = []
    for package_id, directory in targets:
        child = check(directory, 'draft', checker, trust)
        if child['release_type'] != 'application' or child['schema_version'] != 1:
            raise ValueError('only individual application releases can be assembled')
        inputs.append((package_id, directory, child, load(directory/'manifest.json')))
    first = inputs[0][3]
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mimi-collection-', dir=output.parent) as temp:
        staging = Path(temp)/'release'
        staging.mkdir()
        index = dict(schema_version=1, policy=POLICY, release_type='application-index',
                     repository=repository, version=first['version'], commit=first['commit'],
                     channel=first['channel'], sequence=first['sequence'], key_id=key_id, packages=[])
        for package_id, directory, child, manifest in inputs:
            if any(manifest[k] != first[k] for k in ['version', 'commit', 'channel', 'sequence']):
                raise ValueError('all targets must have identical version/source/channel/sequence')
            for name in [*child['assets'], 'release-lock.json']:
                if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,140}', name):
                    raise ValueError('invalid child filename')
                shutil.copyfile(directory/name, staging/(package_id+'--'+name))
            index['packages'].append(dict(id=package_id,
                manifest_asset=package_id+'--manifest.json', signature_asset=package_id+'--manifest.sig',
                package_asset=package_id+'--application.tar.gz',
                manifest_sha256=sha(directory/'manifest.json'), signature_sha256=sha(directory/'manifest.sig'),
                package_sha256=manifest['package_sha256'], package_bytes=manifest['package_bytes']))
        (staging/'release-index.json').write_text(json.dumps(index, indent=2)+'\n')
        subprocess.run([str(checker), 'sign-index', '--manifest', str(staging/'release-index.json'),
                        '--signature', str(staging/'release-index.sig'), '--source', str(staging),
                        '--trust', str(trust), '--key', str(key)], check=True)
        lock = dict(schema_version=2, policy=POLICY, release_type='application', repository=repository,
                    source_commit=first['commit'], version=first['version'], tag='v'+first['version'],
                    channel=first['channel'], packages=ids,
                    assets={p.name:sha(p) for p in sorted(staging.iterdir())})
        (staging/'release-lock.json').write_text(json.dumps(lock, indent=2)+'\n')
        check_collection(staging, 'draft', checker, trust)
        staging.rename(output)
    return lock


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target', action='append', required=True, help='unique-id=/absolute/target-release-directory')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--repository', required=True)
    p.add_argument('--key-id', required=True)
    p.add_argument('--key', type=Path, required=True)
    p.add_argument('--checker', type=Path, required=True)
    p.add_argument('--trust', type=Path, required=True)
    a = p.parse_args()
    targets = []
    for value in a.target:
        package_id, separator, path = value.partition('=')
        if not separator: p.error('--target requires id=directory')
        targets.append((package_id,Path(path).resolve()))
    assemble(targets,a.output.resolve(),a.repository,a.key_id,a.key.resolve(),a.checker.resolve(),a.trust.resolve())
    print('PASS assembled signed collection: '+str(a.output))


if __name__ == '__main__': main()
