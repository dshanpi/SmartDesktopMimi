#!/usr/bin/env python3
"""Assemble a local application/system release from externally pinned inputs.

This does not upload, install, sign with an embedded key, or invent acceptance.
Sign application/system manifests first using the common checker and external
keys. A plan contains schema_version, release_type, device_files and assets;
assets maps public filenames to {path, sha256}, relative to the plan directory.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from build_provenance import inventory_digest
from release_gate import AUTOMATED, HARDWARE, POLICY, check, load, sha
from release_identity import PUBLIC_REPOSITORY, HARDWARE_REPOSITORY, tag_for
from system_image import image_name

NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._+-]{0,140}')
HASH = re.compile(r'[a-f0-9]{64}')
COMMON = {'source.tar.gz', 'sbom.spdx.json', 'platform-contract-lock.json'}
APPLICATION = {'application.tar.gz', 'manifest.json', 'manifest.sig',
               'application-build.json', 'sdk-lock.json', 'dependency-lock.json', 'source-ownership.json'}
SYSTEM = { 'system-manifest.json', 'system-manifest.sig',
          'system-build.json', 'sdk-components.json', 'factory-manifest.json',
          'factory-manifest.sig', 'dependency-lock.json'}


def write(path, record):
    path.write_text(json.dumps(record, sort_keys=True, indent=2) + '\n')
    path.chmod(0o644)


def initial_validation(directory, kind, devices, commit):
    artifact = 'application.tar.gz' if kind == 'application' else image_name(load(directory/'system-manifest.json')['version'])
    return dict(schema_version=1, source_commit=commit, artifact_sha256=sha(directory/artifact),
                targets=[dict(device_file=name, checks={case: dict(
                    result='not_run', method='physical-device' if case in HARDWARE else 'host',
                    reason='No acceptance evidence supplied for this artifact')
                    for case in sorted(AUTOMATED | HARDWARE)}) for name in devices])


def make_lock(directory, kind):
    manifest = load(directory/('manifest.json' if kind == 'application' else 'system-manifest.json'))
    contract = load(directory/'platform-contract-lock.json')
    lock = dict(schema_version=1, policy=POLICY, repository=PUBLIC_REPOSITORY,
                release_type=kind, source_commit=manifest['commit'], version=manifest['version'],
                ipc_contract=contract['contract_id'])
    if kind == 'application':
        sdk = load(directory/'sdk-lock.json')
        lock.update(sdk_lock_sha256=sha(directory/'sdk-lock.json'),
                    toolchain_sha256=inventory_digest(sdk['files'], 'toolchain'),
                    sysroot_sha256=inventory_digest(sdk['files'], 'sysroot'),
                    dependency_lock_sha256=sha(directory/'dependency-lock.json'))
    else:
        lock['image_asset'] = image_name(lock['version'])
        if not (directory/lock['image_asset']).is_file():
            raise ValueError('missing canonical product system image')
        record = load(directory/'system-build.json')
        for field in ['platform_id', 'hardware_commit', 'sdk_baseline', 'sdk_local_inputs_sha256',
                      'toolchain_sha256', 'sysroot_sha256', 'partition_sha256']:
            lock[field] = record[field]
        if record['hardware_repository'] != HARDWARE_REPOSITORY:
            raise ValueError('unexpected private hardware repository')
        lock['hardware_repository'] = HARDWARE_REPOSITORY
        lock['dependency_lock_sha256'] = sha(directory/'dependency-lock.json')
        factory = load(directory/'factory-manifest.json')
        lock.update(factory_application_commit=factory['commit'],
                    factory_application_sha256=factory['package_sha256'])
    lock['tag'] = tag_for(kind, lock['version'], lock.get('platform_id'))
    lock['assets'] = {p.name: sha(p) for p in sorted(directory.iterdir())}
    return lock


def prepare(plan_path, output, checker, trust):
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists; never replace a candidate')
    plan = load(plan_path)
    if set(plan) != {'schema_version', 'release_type', 'device_files', 'assets'} or plan['schema_version'] != 1:
        raise ValueError('unknown preparation plan schema/fields')
    kind = plan['release_type']
    if kind not in {'application', 'system'}:
        raise ValueError('unknown release type')
    devices = plan['device_files']
    if not isinstance(devices, list) or not devices or len(set(devices)) != len(devices):
        raise ValueError('missing/duplicate target device files')
    required = COMMON | (APPLICATION if kind == 'application' else SYSTEM)
    if any(not isinstance(name, str) or not NAME.fullmatch(name) or name in required | {'validation.json', 'release-lock.json'} for name in devices):
        raise ValueError('invalid or reserved device filename')
    assets = plan['assets']
    if not isinstance(assets, dict) or not required | set(devices) <= assets.keys():
        raise ValueError('missing required inputs')
    sources = {}
    for name, entry in assets.items():
        if not NAME.fullmatch(name) or name == 'release-lock.json':
            raise ValueError('invalid or reserved asset name')
        if not isinstance(entry, dict) or set(entry) != {'path', 'sha256'} or not HASH.fullmatch(entry['sha256']):
            raise ValueError('every input requires an external SHA256 pin')
        path = plan_path.parent / entry['path']
        if path.is_symlink() or not path.is_file() or sha(path) != entry['sha256']:
            raise ValueError('input changed or is not a regular file: ' + name)
        sources[name] = path
    if kind == 'system':
        required |= {image_name(load(sources['system-manifest.json'])['version'])}
        if not required <= sources.keys() or set(devices) & required:
            raise ValueError('missing canonical or reserved system image input')
    # No private keys or arbitrary SDK attachments: extra files must be named by
    # acceptance evidence, and the complete release gate still validates them.
    allowed = required | set(devices) | {'validation.json'}
    if 'validation.json' in sources:
        report = load(sources['validation.json'])
        if sorted(t['device_file'] for t in report['targets']) != sorted(devices):
            raise ValueError('validation target inventory differs from preparation plan')
        allowed |= {e['evidence'] for t in report['targets'] for e in t['checks'].values() if 'evidence' in e}
    if kind == 'system':
        manifest = load(sources['system-manifest.json'])
        if manifest.get('ota'):
            allowed.add('system.swu')
            if 'system.swu' not in sources:
                raise ValueError('missing signed system OTA payload')
    if set(assets) - allowed:
        raise ValueError('undeclared release attachments: ' + ', '.join(sorted(set(assets)-allowed)))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mimi-prepare-', dir=output.parent) as temporary:
        directory = Path(temporary)/'release'
        directory.mkdir()
        for name, source in sources.items():
            destination = directory/name
            shutil.copyfile(source, destination)
            destination.chmod(0o644)
            if sha(destination) != assets[name]['sha256']:
                raise ValueError('input changed while copying: ' + name)
        if 'validation.json' not in sources:
            manifest = load(directory/('manifest.json' if kind == 'application' else 'system-manifest.json'))
            write(directory/'validation.json', initial_validation(directory, kind, devices, manifest['commit']))
        lock = make_lock(directory, kind)
        write(directory/'release-lock.json', lock)
        check(directory, 'draft', checker, trust)
        # Another process cannot alter the input set unnoticed during admission.
        for name, source in sources.items():
            if sha(source) != assets[name]['sha256']:
                raise ValueError('input changed during admission: ' + name)
        directory.rename(output)
    return lock


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--checker', type=Path, required=True)
    parser.add_argument('--trust', type=Path, required=True)
    args = parser.parse_args()
    try:
        lock = prepare(args.inputs.resolve(), args.output.absolute(), args.checker.resolve(), args.trust.resolve())
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'RELEASE PREPARATION BLOCKED: ' + str(error) + '\n')
    print(json.dumps(dict(tag=lock['tag'], repository=lock['repository'], assets=len(lock['assets']),
                         state='local_candidate', uploaded=False)))


if __name__ == '__main__':
    main()
