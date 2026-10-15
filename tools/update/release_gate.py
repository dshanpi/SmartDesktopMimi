#!/usr/bin/env python3
"""Fail-closed release admission. This tool never promotes a failed/not-run target."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
from release_identity import check_identity, HARDWARE_REPOSITORY
import build_provenance
from system_image import release_image

POLICY = 'MIMI-UPDATE-001/1.0.0'
AUTOMATED = {'source_audit', 'build', 'compatibility', 'signature', 'archive_safety',
             'transaction_recovery', 'ui_navigation', 'web_tests'}
HARDWARE = {'identity', 'cold_boot', 'navigation_touch', 'soak', 'wifi', 'bluetooth_audio',
            'voice', 'media', 'hdmi_hid', 'application_upgrade', 'rollback',
            'power_loss', 'data_preservation', 'system_application_pairing'}


def load(path):
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f'duplicate JSON key: {key}')
            result[key] = value
        return result
    return json.loads(path.read_text(), object_pairs_hook=unique)


def sha(path):
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
        return digest.hexdigest()


def target_key(target):
    return json.dumps(target, sort_keys=True, separators=(',', ':'))


def evidence_scope(manifest, device, checker, carrier=None):
    """Classify only after the original signed artifact has passed admission.

    The temporary unsigned projection is a compatibility predicate, never an
    installation/signature authorization. Reuse the actual Go matcher so host
    SemVer, capability and ABI interpretations cannot drift from the updater.
    """
    candidates = [target for target in manifest['targets'] if target['identity'] == device['identity']]
    if not candidates:
        raise ValueError('undeclared target evidence')
    if len(candidates) == 1:
        return target_key(candidates[0])
    matched = []
    with tempfile.TemporaryDirectory(prefix='mimi-target-scope-') as temporary:
        root = Path(temporary)
        device_path = root/'device.json'
        device_path.write_text(json.dumps(device))
        for target in candidates:
            probe = copy.deepcopy(manifest if carrier is None else carrier)
            probe['targets'] = [target]
            if carrier is not None:
                # System targets classify the SOURCE runtime. Its separately
                # verified factory manifest describes the DESTINATION; do not
                # impose the destination app's data range on this predicate.
                schema = device['data_schema']
                probe['data'] = dict(min=schema, max=schema, write=schema)
            path = root/'predicate.json'
            path.write_text(json.dumps(probe))
            result = subprocess.run([str(checker), 'match', '--manifest', str(path),
                                     '--device', str(device_path)], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=30)
            if result.returncode == 0:
                matched.append(target_key(target))
    if len(matched) != 1:
        raise ValueError('target evidence has ambiguous or missing runtime/capability scope')
    return matched[0]


def check(directory, stage, checker, trust):
    if stage not in {'draft', 'prerelease', 'stable'}:
        raise ValueError('unknown release stage')
    lock = load(directory / 'release-lock.json')
    if lock.get('schema_version') == 2:
        from release_collection import check_collection
        return check_collection(directory, stage, checker, trust)
    if lock.get('schema_version') != 1 or lock.get('policy') != POLICY:
        raise ValueError('unsupported release lock')
    kind = lock['release_type']
    if kind not in {'application', 'system'}:
        raise ValueError('unknown release type')
    if not re.fullmatch(r'[0-9a-f]{40}', lock['source_commit']):
        raise ValueError('full source commit required')
    check_identity(lock)
    for field in ['toolchain_sha256', 'sysroot_sha256', 'dependency_lock_sha256']:
        if not re.fullmatch(r'[0-9a-f]{64}', lock[field]):
            raise ValueError(f'{field} missing or invalid')
    assets = lock['assets']
    image_asset = release_image(lock) if kind == 'system' else 'application.tar.gz'
    required = {'source.tar.gz', 'sbom.spdx.json', 'validation.json', 'platform-contract-lock.json'}
    if kind == 'application':
        required |= {'application.tar.gz', 'manifest.json', 'manifest.sig'}
    else:
        required |= {image_asset, 'system-manifest.json', 'system-manifest.sig',
                     'sdk-components.json', 'factory-manifest.json', 'factory-manifest.sig'}
        for key in ['hardware_commit', 'factory_application_commit']:
            if not re.fullmatch(r'[0-9a-f]{40}', lock[key]):
                raise ValueError(f'{key} missing')
        for key in ['partition_sha256', 'factory_application_sha256']:
            if not re.fullmatch(r'[0-9a-f]{64}', lock[key]):
                raise ValueError(f'{key} missing')
    if not required <= assets.keys():
        raise ValueError('missing required release assets: ' + ', '.join(sorted(required-assets.keys())))
    for name, digest in assets.items():
        if Path(name).name != name or name in {'.', '..'}:
            raise ValueError('asset paths must be plain filenames')
        path = directory / name
        if path.is_symlink() or not path.is_file() or sha(path) != digest:
            raise ValueError(f'asset hash mismatch: {name}')
    contract = load(directory / 'platform-contract-lock.json')
    if contract['contract_id'] != lock['ipc_contract']:
        raise ValueError('release/interface contract mismatch')
    report = load(directory / 'validation.json')
    if report.get('schema_version') != 1 or report['source_commit'] != lock['source_commit']:
        raise ValueError('validation source mismatch')
    tested_hash = assets[image_asset]
    if report['artifact_sha256'] != tested_hash:
        raise ValueError('validation does not describe this artifact')
    if not report['targets']:
        raise ValueError('missing target evidence')
    identities = []
    scopes = set()
    for target in report['targets']:
        device = target['device_file']
        if Path(device).name != device or device not in assets:
            raise ValueError('device evidence must be a hashed release asset')
        device_record = load(directory / device)
        identity = device_record['identity'] if kind == 'application' else device_record['device']['identity']
        if identity['ipc_contract'] != lock['ipc_contract']:
            raise ValueError('target/interface contract mismatch')
        identities.append(identity)
        checks = target['checks']
        if set(checks) != AUTOMATED | HARDWARE:
            raise ValueError('incomplete/unknown acceptance matrix')
        for case, evidence in checks.items():
            result = evidence['result']
            if result not in {'passed', 'failed', 'not_run', 'blocked'}:
                raise ValueError('invalid test result')
            if result == 'passed':
                path = evidence['evidence']
                if path not in assets:
                    raise ValueError('passed result has no hashed evidence asset')
                if case in HARDWARE and evidence.get('method') != 'physical-device':
                    raise ValueError('host evidence cannot certify hardware')
            if stage != 'draft' and result in {'failed', 'blocked'}:
                raise ValueError(f'known failure blocks publication: {case}')
            if (stage == 'stable' or (stage == 'prerelease' and case in AUTOMATED)) and result != 'passed':
                raise ValueError(f'required acceptance not passed: {case}')
        if kind == 'system':
            system_manifest = load(directory/'system-manifest.json')
            subprocess.run([str(checker), 'verify-system', '--manifest', str(directory/'system-manifest.json'),
                            '--signature', str(directory/'system-manifest.sig'), '--archive', str(directory/image_asset),
                            '--device', str(directory/device), '--trust', str(trust)], check=True)
            # A system signature does not replace the independent application signature.
            # Validate the factory app with external platform trust and target-system facts.
            with tempfile.TemporaryDirectory(prefix='mimi-factory-gate-') as temporary:
                factory_device = Path(temporary) / 'device.json'
                destination = dict(system_manifest['ota']['destination_device']) if system_manifest.get('ota') else dict(device_record['device'])
                destination['enrollment_id'] = device_record['device'].get('enrollment_id', '')
                factory_device.write_text(json.dumps(destination))
                subprocess.run([str(checker), 'verify-manifest', '--manifest', str(directory/'factory-manifest.json'),
                                '--signature', str(directory/'factory-manifest.sig'), '--device', str(factory_device),
                                '--trust', str(trust)], check=True)
            if system_manifest.get('ota'):
                if 'system.swu' not in assets or assets['system.swu'] != system_manifest['ota']['package_sha256']:
                    raise ValueError('system OTA artifact is missing or mismatched')
                if stage == 'stable' and system_manifest['ota']['channel'] != 'stable':
                    raise ValueError('testing system OTA cannot be promoted as stable')
                subprocess.run([str(checker), 'verify-system-ota', '--manifest', str(directory/'system-manifest.json'),
                                '--signature', str(directory/'system-manifest.sig'), '--archive', str(directory/'system.swu'),
                                '--factory-manifest', str(directory/'factory-manifest.json'), '--factory-signature', str(directory/'factory-manifest.sig'),
                                '--device', str(directory/device), '--trust', str(trust)], check=True)
        if kind == 'application':
            subprocess.run([str(checker), 'verify', '--manifest', str(directory/'manifest.json'),
                            '--signature', str(directory/'manifest.sig'), '--archive', str(directory/'application.tar.gz'),
                            '--device', str(directory/device), '--trust', str(trust)], check=True)
        # Full signature/archive/product checks above remain authoritative.
        # Only their signed target declarations may classify this evidence.
        manifest = load(directory/('manifest.json' if kind == 'application' else 'system-manifest.json'))
        scope = evidence_scope(manifest, device_record if kind == 'application' else device_record['device'],
                               checker, None if kind == 'application' else load(directory/'factory-manifest.json'))
        if scope in scopes:
            raise ValueError('duplicate target evidence scope')
        scopes.add(scope)
    if scopes != {target_key(target) for target in manifest['targets']}:
        raise ValueError('missing declared target evidence scope')
    if kind == 'application':
        manifest = load(directory/'manifest.json')
        build_provenance.application(directory,lock,manifest,stage,load)
        if manifest['commit'] != lock['source_commit'] or manifest['version'] != lock['version']:
            raise ValueError('manifest/source lock mismatch')
        if manifest['package_sha256'] != tested_hash:
            raise ValueError('manifest/package mismatch')
        if stage == 'stable' and manifest['channel'] != 'stable':
            raise ValueError('testing package cannot be promoted as stable')
        if stage == 'prerelease' and manifest['channel'] != 'testing':
            raise ValueError('prerelease requires testing-channel package')
        if sorted(json.dumps(t['identity'], sort_keys=True) for t in manifest['targets']) != sorted(json.dumps(i, sort_keys=True) for i in identities):
            raise ValueError('evidence targets do not equal declared package targets')
    else:
        manifest = load(directory/'system-manifest.json')
        if {t['identity']['platform_id'] for t in manifest['targets']} != {lock['platform_id']}:
            raise ValueError('system tag/manifest platform mismatch')
        if stage == 'prerelease' and manifest.get('ota',{}).get('channel','testing') != 'testing':
            raise ValueError('prerelease system requires testing channel')
        factory = load(directory/'factory-manifest.json')
        build_provenance.system(directory,lock,manifest,factory,load)
        if manifest['commit'] != lock['source_commit'] or manifest['version'] != lock['version']:
            raise ValueError('system manifest/source mismatch')
        if manifest['image_sha256'] != tested_hash or manifest['partition_sha256'] != lock['partition_sha256']:
            raise ValueError('system image/partition mismatch')
        if manifest['factory_manifest_sha256'] != assets['factory-manifest.json'] or manifest['sdk_components_sha256'] != assets['sdk-components.json']:
            raise ValueError('factory/SDK provenance mismatch')
        if manifest['factory_application_sha256'] != lock['factory_application_sha256'] or factory['package_sha256'] != lock['factory_application_sha256'] or factory['commit'] != lock['factory_application_commit']:
            raise ValueError('factory application identity mismatch')
        if sorted(json.dumps(t['identity'], sort_keys=True) for t in manifest['targets']) != sorted(json.dumps(i, sort_keys=True) for i in identities):
            raise ValueError('system target evidence mismatch')
    return lock


def require_upload_acceptance(directory, lock):
    """Require all acceptance before upload, including drafts and testing builds.

    Call after check(), which verifies hashes, identities and evidence methods.
    Local draft preparation remains available for work in progress.
    """
    if lock['schema_version'] == 2:
        for package_id in lock['packages']:
            child = load(directory / (package_id + '--release-lock.json'))
            if child['schema_version'] != 1:
                raise ValueError('unexpected nested release collection')
            _require_complete_report(load(directory / (package_id + '--validation.json')))
    else:
        _require_complete_report(load(directory / 'validation.json'))


def _require_complete_report(report):
    for target in report['targets']:
        pending = sorted(case for case, result in target['checks'].items()
                         if result['result'] != 'passed')
        if pending:
            raise ValueError('upload requires completed acceptance for ' +
                             target['device_file'] + ': ' + ', '.join(pending))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--stage', choices=['draft', 'prerelease', 'stable'], default='draft')
    parser.add_argument('--checker', type=Path, required=True)
    parser.add_argument('--trust', type=Path, required=True, help='external platform trust, never from the release payload')
    args = parser.parse_args()
    try:
        lock = check(args.directory.resolve(), args.stage, args.checker.resolve(), args.trust.resolve())
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'RELEASE BLOCKED: {error}\n')
    print(f'PASS {args.stage}: {lock["release_type"]} {lock["version"]}')


if __name__ == '__main__':
    main()
