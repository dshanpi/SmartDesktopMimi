#!/usr/bin/env python3
"""Build a Debian transport package for an already signed application bundle.

dpkg owns only the transport copy. Activation still belongs to the platform
updater, never to dpkg maintainer scripts. This command cannot sign or publish.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

from configure import validate, check_sdk
from sdk_targets import target_spec
from application_sdk import verify_sdk
from application_build import load


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()


def deb_version(version):
    match = re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?', version)
    if not match: raise ValueError('Invalid application version')
    base = '.'.join(match.group(i) for i in (1, 2, 3))
    if match[4]: base += '~' + match[4]
    if match[5]: base += '+' + match[5]
    return base + '-1'


def bundle_receipt(archive):
    # Read only one bounded, regular member; do not extract paths from the tar.
    with tarfile.open(archive, 'r:gz') as tar:
        members = [m for m in tar if m.name == 'resources/application-build.json']
        if len(members) != 1 or not members[0].isfile() or members[0].size > 1 << 20:
            raise ValueError('Missing/invalid signed build receipt')
        raw = tar.extractfile(members[0]).read()
    def unique(items):
        d = {}
        for k, v in items:
            if k in d: raise ValueError('Duplicate receipt key')
            d[k] = v
        return d
    return json.loads(raw, object_pairs_hook=unique), hashlib.sha256(raw).hexdigest()


def check_binding(profile, sdk_record, manifest, receipt, receipt_hash):
    if profile['target_os'] not in ('debian', 'ubuntu') or profile['package_format'] != 'deb':
        raise ValueError('Only a Debian/Ubuntu SDK can produce a deb bundle')
    if sdk_record.get('schema_version') != 2:
        raise ValueError('Debian delivery requires a schema-2 SDK')
    if manifest.get('release_type') != 'application': raise ValueError('System packages cannot enter application deb packaging')
    deb_version(manifest['version'])
    if receipt.get('schema_version') != 1 or receipt.get('kind') != 'application-build':
        raise ValueError('Unknown build receipt')
    for field in ('platform_id', 'architecture', 'system_baseline', 'target_os', 'sdk_lock_sha256'):
        if receipt.get(field) != profile[field]: raise ValueError('Build/profile mismatch: ' + field)
    if receipt.get('target_os_version') != sdk_record['target_os_version']:
        raise ValueError('Build/SDK OS version mismatch')
    source = receipt.get('source', {})
    if source.get('dirty') is not False or not re.fullmatch('[a-f0-9]{40}', source.get('commit', '')) or source['commit'] != manifest.get('commit'):
        raise ValueError('Source commit must be clean and match the signed manifest')
    build = receipt.get('build', {})
    if build.get('application_only') is not True or build.get('cloud_provider') is not False or build.get('target') != 'lvglsim':
        raise ValueError('Independent application build required')
    screens = [f"{s['width']}x{s['height']}" for s in profile['screens']]
    if build.get('display_profiles') != screens or build.get('voice_control') is not profile['voice_control']:
        raise ValueError('Build/profile display or voice mismatch')
    files = manifest.get('files', {})
    if files.get('resources/application-build.json', {}).get('sha256') != receipt_hash:
        raise ValueError('Receipt differs from signed manifest')
    if files.get('bin/lvglsim', {}).get('sha256') != receipt.get('desktop_sha256'):
        raise ValueError('Desktop differs from signed build receipt')
    # Generic architecture/libc equality cannot distinguish Tina from Debian.
    capability = 'target-os:' + profile['target_os'] + ':' + sdk_record['target_os_version']
    targets = manifest.get('targets', [])
    if not targets: raise ValueError('No signed platform target')
    for t in targets:
        identity = t.get('identity', {})
        if identity.get('product_id') != 'smartdesktopmimi' or identity.get('platform_id') != profile['platform_id'] or identity.get('architecture') != profile['architecture'] or capability not in t.get('capabilities', []):
            raise ValueError('Signed target lacks matching platform/OS capability')


def build_deb(profile_path, sdk, bundle, verifier, trust, device, output, epoch, maintainer):
    if type(epoch) is not int or epoch < 0: raise ValueError('Nonnegative source date epoch required')
    if output.exists() or output.is_symlink(): raise ValueError('Output directory already exists')
    if not re.fullmatch(r'[^\r\n<>]+ <[A-Za-z0-9.!#$%&\x27*+/=?^_`{|}~-]+@[A-Za-z0-9.-]+>', maintainer):
        raise ValueError('A single-line package maintainer name and email are required')
    profile = validate(load(profile_path))
    if profile['package_format'] != 'deb': raise ValueError('Tina application cannot be relabelled as deb')
    check_sdk(profile, sdk)
    sdk_record = verify_sdk(sdk, profile['sdk_lock_sha256'])
    for name in ('manifest.json', 'manifest.sig', 'application.tar.gz'):
        path = bundle/name
        if path.is_symlink() or not path.is_file(): raise ValueError('Bundle input must be a regular file: ' + name)
    # Copy first: signature verification and packaging consume the same bytes.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mimi-deb-', dir=output.parent) as temporary:
        temp = Path(temporary)
        stage = temp/'root'
        relative = 'usr/share/smartdesktopmimi/bundle-' + profile['platform_id']
        payload = stage/relative
        payload.mkdir(parents=True)
        for name in ('manifest.json', 'manifest.sig', 'application.tar.gz'):
            shutil.copyfile(bundle/name, payload/name); (payload/name).chmod(0o644)
        subprocess.run([str(verifier), 'verify', '--manifest', str(payload/'manifest.json'),
                        '--signature', str(payload/'manifest.sig'), '--archive', str(payload/'application.tar.gz'),
                        '--trust', str(trust), '--device', str(device)], check=True)
        manifest = load(payload/'manifest.json')
        receipt, receipt_hash = bundle_receipt(payload/'application.tar.gz')
        check_binding(profile, sdk_record, manifest, receipt, receipt_hash)
        if digest(payload/'application.tar.gz') != manifest['package_sha256']:
            raise ValueError('Archive hash changed')
        (payload/'build-profile.json').write_text(json.dumps(profile, sort_keys=True, indent=2)+'\n')
        deb_arch=target_spec(profile['platform_id'],profile['architecture'],profile['target_os'])['deb_architecture']
        package = 'smartdesktopmimi-bundle-' + profile['platform_id']
        version = deb_version(manifest['version'])
        control = stage/'DEBIAN'; control.mkdir()
        installed_size = (sum(p.stat().st_size for p in stage.rglob('*') if p.is_file())+1023)//1024
        (control/'control').write_text(f'''Package: {package}
Version: {version}
Section: misc
Priority: optional
Architecture: {deb_arch}
Maintainer: {maintainer}
Installed-Size: {installed_size}
Description: Signed SmartDesktopMimi application transport bundle
 Stages a signed candidate for the platform-managed application updater.
 Installing or removing this transport package does not activate, stop,
 downgrade or remove the selected desktop or its user data.
''')
        documentation = stage/f'usr/share/doc/{package}'; documentation.mkdir(parents=True)
        (documentation/'README').write_text(f'''This package only stages the signed application bundle.
The enrolled platform must provide its trusted updater, provider, runtime,
startup and health hooks. The bundle cannot install or replace those components.
To request activation after installation, use the platform updater:
  sudo mimi-update install-offline --manifest /{relative}/manifest.json --signature /{relative}/manifest.sig --archive /{relative}/application.tar.gz
The updater checks its own trusted keys, hardware, OS capability, ABI, channel,
version, storage, activity and data compatibility under its shared transaction lock.
Health failure rolls back through that updater; dpkg status is NOT activation status.
No maintainer scripts run and no user data, platform services or trust keys are installed.
Removing or purging this transport package leaves updater-managed versions and data intact.
The .deb must be distributed through an authenticated repository or a separately
verified release hash. Its inner signature covers the application, not dpkg metadata.
''')
        for path in stage.rglob('*'):
            path.chmod(0o755 if path.is_dir() else 0o644)
            os.utime(path, (epoch, epoch))
        os.utime(stage, (epoch, epoch))
        result = temp/'result'; result.mkdir()
        # GitHub rewrites '~' in asset filenames. Keep Debian's prerelease
        # ordering inside control metadata, with a portable external filename.
        artifact_version = manifest['version'].replace('+', '_build_') + '-1'
        archive = result/f'{package}_{artifact_version}_{deb_arch}.deb'
        subprocess.run(['dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(stage), str(archive)],
                       env={**os.environ, 'SOURCE_DATE_EPOCH': str(epoch)}, check=True)
        lock = dict(schema_version=1, kind='application-deb-transport', source_commit=manifest['commit'],
                    application_version=manifest['version'], deb_version=version,
                    platform_id=profile['platform_id'], target_os=profile['target_os'],
                    architecture=profile['architecture'], deb_architecture=deb_arch,
                    target_os_version=sdk_record['target_os_version'], sdk_lock_sha256=profile['sdk_lock_sha256'],
                    application_sha256=manifest['package_sha256'], manifest_sha256=digest(payload/'manifest.json'),
                    deb_sha256=digest(archive), source_date_epoch=epoch,
                    activation='not_run', physical_acceptance='not_run', uploaded=False)
        (result/'deb-build.json').write_text(json.dumps(lock, sort_keys=True, indent=2)+'\n')
        os.rename(result, output)
    return lock


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('profile', 'sdk', 'bundle', 'verifier', 'trust', 'device', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--epoch', type=int, required=True)
    p.add_argument('--maintainer', required=True, help='Package maintainer: Name <email>')
    a = p.parse_args()
    try:
        result = build_deb(a.profile, a.sdk.resolve(), a.bundle, a.verifier.resolve(), a.trust, a.device, a.output.absolute(), a.epoch, a.maintainer)
    except (ValueError, OSError, KeyError, TypeError, tarfile.TarError, subprocess.SubprocessError) as error:
        p.exit(1, 'DEB PACKAGING BLOCKED: '+str(error)+'\n')
    print(json.dumps(result))


if __name__ == '__main__': main()
