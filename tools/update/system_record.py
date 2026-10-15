#!/usr/bin/env python3
"""Turn a completed system build into pinned, unsigned release inputs.

This records build provenance only. It never signs, installs, uploads, or
converts build success into physical acceptance.
"""
import argparse
import gzip
import hashlib
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile

from application_build import load, digest
from release_identity import HARDWARE_REPOSITORY, tag_for
from prepare_release import write
from system_image import image_name, archive as archive_image, raw_digest
from product_lock import validate as validate_product_lock

HASH = re.compile(r'[a-f0-9]{64}')
COMMIT = re.compile(r'[a-f0-9]{40}')
FIELDS = {'schema_version', 'build_manifest_sha256', 'hardware_commit',
          'sdk_baseline', 'sdk_local_inputs_sha256', 'toolchain_sha256',
          'sysroot_sha256', 'partition_sha256', 'inputs'}


def regular(root, name):
    path = PurePosixPath(name)
    if path.is_absolute() or not name or '..' in path.parts or str(path) != name:
        raise ValueError('unsafe build input path')
    current = root
    for part in path.parts:
        current = current/part
        if current.is_symlink():
            raise ValueError('linked build input: '+name)
    if not current.is_file():
        raise ValueError('missing regular build input: '+name)
    return current


def build_manifest(path):
    result = {}
    for line in path.read_text().splitlines():
        if not line:
            break
        key, sep, value = line.partition('=')
        if not sep or key in result:
            raise ValueError('invalid/duplicate build manifest field')
        result[key] = value
    if (result.get('AITVBOX_RELEASE_FORMAT') != '1' or
            result.get('PRODUCT_TRACKED_DIRTY') != '0' or result.get('PRODUCT_DIRTY') != '0' or
            result.get('APPLICATION_UPDATES') != '1' or
            result.get('TARGET_SOC') != 'a133' or result.get('TARGET_BOARD') != 'b6' or
            result.get('PACK_VARIANT') != 'uart0' or
            not COMMIT.fullmatch(result.get('PRODUCT_COMMIT', ''))):
        raise ValueError('unsupported, dirty or unsigned-baseline system build')
    if result.get('SYSTEM_TAG') != tag_for('system', result['SYSTEM_VERSION'], 'a133'):
        raise ValueError('system tag/version mismatch')
    return result


def checksums(root):
    result = {}
    for line in regular(root, 'SHA256SUMS').read_text().splitlines():
        match = re.fullmatch(r'([a-f0-9]{64})  (.+)', line)
        if not match or match[2] in result:
            raise ValueError('invalid/duplicate build checksum')
        result[match[2]] = match[1]
    return result


def components(record, baseline):
    if (set(record) != {'schema_version', 'sdk_baseline', 'manifest_sha256', 'components'} or
            record['schema_version'] != 1 or record['sdk_baseline'] != baseline or
            not HASH.fullmatch(record['manifest_sha256']) or not record['components']):
        raise ValueError('invalid public SDK inventory')
    seen = set()
    for item in record['components']:
        if set(item) != {'path', 'commit', 'local_inputs_sha256'}:
            raise ValueError('private/unknown SDK fields cannot be exported')
        name = item['path']; path = PurePosixPath(name)
        if (not name or path.is_absolute() or '..' in path.parts or str(path) != name or
                name in seen or not COMMIT.fullmatch(item['commit']) or
                not HASH.fullmatch(item['local_inputs_sha256'])):
            raise ValueError('invalid/duplicate SDK component')
        seen.add(name)


def prepare(build, plan_path, output, checker, trust):
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists')
    plan = load(plan_path)
    if set(plan) != FIELDS or plan['schema_version'] != 1:
        raise ValueError('unknown system record plan')
    for field in FIELDS - {'schema_version', 'hardware_commit', 'sdk_baseline', 'inputs'}:
        if not HASH.fullmatch(plan[field]):
            raise ValueError('missing external pin: '+field)
    if not COMMIT.fullmatch(plan['hardware_commit']) or not plan['sdk_baseline']:
        raise ValueError('missing SDK/hardware identity')
    regular(build, 'BUILD-COMPLETE')
    if (build/'BUILD-INCOMPLETE').exists() or (build/'BUILD-INCOMPLETE').is_symlink():
        raise ValueError('system build incomplete')
    manifest_path = regular(build, 'BUILD-MANIFEST.txt')
    if digest(manifest_path) != plan['build_manifest_sha256']:
        raise ValueError('build manifest differs from external pin')
    manifest = build_manifest(manifest_path)
    sums = checksums(build)
    canonical = image_name(manifest['SYSTEM_VERSION'])
    named = manifest.get('SYSTEM_IMAGE_FILE')
    if named is not None and named != canonical:
        raise ValueError('build system image name/version mismatch')
    image_input = 'firmware/' + (canonical if named else 'a133_linux_b6_uart0.img')
    names = ['BUILD-MANIFEST.txt', image_input,
             'metadata/baseline-lock.json', 'metadata/factory-manifest.json',
             'metadata/factory-manifest.sig', 'metadata/factory-device.json',
             'logs/50-bootchain-gate.log']
    if named:
        names.append('metadata/image-archive.json')
    product_fields = {'PRODUCT_EDITION', 'PLATFORM_PRODUCT_LOCK_SHA256', 'PRODUCT_ROOTFS_IMAGE_SHA256'}
    product_declared = bool(product_fields & manifest.keys())
    if product_declared:
        if not product_fields <= manifest.keys():
            raise ValueError('incomplete product source lock build fields')
        names += ['metadata/platform-product-lock.json', 'metadata/product-rootfs-check.json']
    selected = {name: regular(build, name) for name in names}
    for name, path in selected.items():
        if sums.get(name) != digest(path):
            raise ValueError('build readback mismatch: '+name)
    product = None
    if product_declared:
        product = validate_product_lock(load(selected['metadata/platform-product-lock.json']))
        check = load(selected['metadata/product-rootfs-check.json'])
        if (manifest['PRODUCT_EDITION'] != product['edition'] or
                manifest['PLATFORM_PRODUCT_LOCK_SHA256'] != sums['metadata/platform-product-lock.json'] or
                product['repositories']['application']['commit'] != manifest['PRODUCT_COMMIT'] or
                product['repositories']['hardware']['commit'] != plan['hardware_commit'] or
                product['sdk_manifest_sha256'] != manifest.get('SDK_MANIFEST_SHA256') or
                type(check.get('schema_version')) is not int or check.get('schema_version') != 1 or check.get('status') != 'passed' or
                check.get('edition') != product['edition'] or check.get('physical') != 'not_run' or
                check.get('release_qualified') is not False or
                check.get('product_lock_sha256') != sums['metadata/platform-product-lock.json'] or
                not HASH.fullmatch(check.get('rootfs_image_sha256', '')) or
                check['rootfs_image_sha256'] != manifest['PRODUCT_ROOTFS_IMAGE_SHA256']):
            raise ValueError('product source/rootfs/build identity mismatch')
    seal = load(selected['metadata/baseline-lock.json'])
    factory = load(selected['metadata/factory-manifest.json'])
    device = load(selected['metadata/factory-device.json'])
    if (seal.get('schema_version') != 1 or seal.get('policy') != 'MIMI-UPDATE-001/1.0.0' or
            seal.get('factory_commit') != factory['commit'] or
            seal.get('factory_version') != factory['version'] or
            seal.get('factory_package_sha256') != factory['package_sha256'] or
            device['runtime']['system'] != manifest['SYSTEM_VERSION'] or device.get('enrollment_id') or
            device['identity']['platform_id'] != 'a133' or device['identity']['architecture'] != 'aarch64'):
        raise ValueError('factory/system baseline mismatch')
    for name, sealed in [('factory-manifest.json', 'usr/lib/smartdesktopmimi/factory/manifest.json'),
                         ('factory-manifest.sig', 'usr/lib/smartdesktopmimi/factory/manifest.sig'),
                         ('factory-device.json', 'etc/smartdesktopmimi/device.json')]:
        if seal['files'][sealed]['sha256'] != sums['metadata/'+name]:
            raise ValueError('factory record differs from sealed system input')
    # Generic image descriptors intentionally have no per-unit enrollment ID.
    # The checker needs a nonempty ID for matching only; this copy is never installed.
    inputs = plan['inputs']
    if set(inputs) != {'sdk-components.json', 'dependency-lock.json'}:
        raise ValueError('unknown/missing public record input')
    paths = {}
    for name, entry in inputs.items():
        if set(entry) != {'path', 'sha256'} or not HASH.fullmatch(entry['sha256']):
            raise ValueError('record input requires an external pin')
        path = plan_path.parent/entry['path']
        if path.is_symlink() or not path.is_file() or digest(path) != entry['sha256']:
            raise ValueError('record input changed: '+name)
        paths[name] = path
    sdk_record = load(paths['sdk-components.json'])
    components(sdk_record, plan['sdk_baseline'])
    if sdk_record['manifest_sha256'] != manifest.get('SDK_MANIFEST_SHA256'):
        raise ValueError('SDK inventory does not match actual build manifest')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mimi-system-record-', dir=output.parent) as temp:
        stage = Path(temp); directory = stage/'record'; directory.mkdir()
        device['enrollment_id'] = 'build-verification-only'
        write(stage/'device.json', device)
        subprocess.run([str(checker), 'verify-manifest', '--manifest', str(selected['metadata/factory-manifest.json']),
                        '--signature', str(selected['metadata/factory-manifest.sig']), '--trust', str(trust),
                        '--device', str(stage/'device.json')], check=True)
        for name, path in paths.items():
            shutil.copyfile(path, directory/name)
        for name in ['factory-manifest.json', 'factory-manifest.sig']:
            shutil.copyfile(selected['metadata/'+name], directory/name)
        image = selected[image_input]
        if named:
            packed = load(selected['metadata/image-archive.json'])
            if (packed.get('kind') != 'system-image-archive' or packed.get('filename') != canonical or
                    packed.get('system_version') != manifest['SYSTEM_VERSION'] or
                    packed.get('archive_sha256') != sums[image_input] or
                    packed.get('archive_sha256') != manifest.get('SYSTEM_IMAGE_ARCHIVE_SHA256') or
                    packed.get('raw_sha256') != manifest.get('SYSTEM_IMAGE_RAW_SHA256') or
                    str(packed.get('raw_bytes')) != manifest.get('SYSTEM_IMAGE_RAW_BYTES') or
                    packed.get('archive_bytes') != image.stat().st_size or
                    not isinstance(packed.get('raw_bytes'), int) or packed['raw_bytes'] <= 0):
                raise ValueError('product image archive differs from pinned build record')
            raw_sha, raw_size = raw_digest(image, packed['raw_bytes'])
            if raw_sha != packed['raw_sha256'] or raw_size != packed['raw_bytes']:
                raise ValueError('product image raw readback mismatch')
            shutil.copyfile(image, directory/canonical)
            if digest(directory/canonical) != sums[image_input]:
                raise ValueError('product image changed while copying')
        else:
            packed = archive_image(image, directory, manifest['SYSTEM_VERSION'], sums[image_input])
            raw_sha = packed['raw_sha256']
        record = {key: plan[key] for key in ['hardware_commit', 'sdk_baseline', 'sdk_local_inputs_sha256',
                   'toolchain_sha256', 'sysroot_sha256', 'partition_sha256']}
        record.update(schema_version=1, kind='system-build', platform_id='a133',
                      system_version=manifest['SYSTEM_VERSION'],
                      build_manifest_sha256=plan['build_manifest_sha256'], raw_image_sha256=raw_sha, image_asset=canonical,
                      source_commit=manifest['PRODUCT_COMMIT'], hardware_repository=HARDWARE_REPOSITORY,
                      image_sha256=digest(directory/canonical),
                      sdk_components_sha256=digest(directory/'sdk-components.json'),
                      dependency_lock_sha256=digest(directory/'dependency-lock.json'),
                      factory_application={key: factory[value] for key,value in
                          [('commit','commit'),('version','version'),('sha256','package_sha256')]})
        if product:
            shutil.copyfile(selected['metadata/platform-product-lock.json'], directory/'platform-product-lock.json')
            shutil.copyfile(selected['metadata/product-rootfs-check.json'], directory/'product-rootfs-check.json')
            record.update(product_edition=product['edition'],
                          platform_product_lock_sha256=sums['metadata/platform-product-lock.json'],
                          product_rootfs_check_sha256=sums['metadata/product-rootfs-check.json'],
                          rootfs_image_sha256=manifest['PRODUCT_ROOTFS_IMAGE_SHA256'])
        write(directory/'system-build.json', record)
        for name, path in selected.items():
            if digest(path) != sums[name]:raise ValueError('build changed during record preparation')
        for name, path in paths.items():
            if digest(path) != inputs[name]['sha256'] or digest(directory/name) != inputs[name]['sha256']:
                raise ValueError('record input changed during preparation')
        directory.rename(output)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['build', 'inputs', 'output', 'checker', 'trust']:
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    try:
        prepare(args.build.resolve(), args.inputs.resolve(), args.output.absolute(), args.checker.resolve(), args.trust.resolve())
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'SYSTEM RECORD BLOCKED: '+str(error)+'\n')
    print('Prepared unsigned system inputs; system signing and physical acceptance are still required')


if __name__ == '__main__':main()
