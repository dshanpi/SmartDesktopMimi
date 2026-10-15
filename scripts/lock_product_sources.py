#!/usr/bin/env python3
"""Lock selected product sources and existing programs; never claim a full image build."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile

import build_a133_provider as provider
import check_product_edition as edition_gate
import tuya_pairing_sources as tuya

ROOT = Path(__file__).resolve().parents[1]
PUBLIC = 'dshanpi/SmartDesktopMimi'
PRIVATE = 'dshanpi/SmartDesktopMimi-Private'
HARDWARE = '100askTeam/A133-AIDesktop-Tina5'
HASH = re.compile(r'[a-f0-9]{64}\Z')
COMMIT = re.compile(r'[a-f0-9]{40}\Z')
STRIP_SHA256 = '42cba88845773ca75e8d0a4693285845841267bca1ff708f77fdba3c62b6772b'
PACKAGE_TRANSFORM = dict(
    schema=1, algorithm='a133-sdk-rstrip-rpath-then-strip-v1', strip_arguments=['--strip-all'],
    inputs={
        'strip': dict(path='prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain/bin/aarch64-openwrt-linux-strip',
                      resolved='prebuilt/rootfsbuilt/aarch64/toolchain-sunxi-glibc-gcc-1130/toolchain/bin/aarch64-openwrt-linux-gnu-strip',
                      sha256=STRIP_SHA256),
        'patchelf': dict(path='openwrt/openwrt/staging_dir/host/bin/patchelf',
                         resolved='out/a133/b6/openwrt/staging_dir/host/bin/patchelf',
                         sha256='53d03dde55a97e139e22e3fa04739f4ed695d113f99061ea52ea7c141e610687'),
        'rstrip': dict(path='openwrt/openwrt/scripts/rstrip.sh',
                       resolved='openwrt/openwrt/scripts/rstrip.sh',
                       sha256='6a43c5e8643c72a1bac5c87cf99195bf3574ec6aadf61d26818181aed6791943')})


def package_tools(sdk):
    # The SDK script contains eval/unquoted paths. Only this fixed SDK-local
    # pipeline is supported; do not turn it into a caller-selected shell command.
    sdk = provider.canonical(str(sdk))
    if not re.fullmatch(r'/[A-Za-z0-9_./-]+', str(sdk)):
        raise ValueError('SDK package pipeline requires a shell-safe fixed root')
    result = {}
    for name, record in PACKAGE_TRANSFORM['inputs'].items():
        actual = (sdk / record['path']).resolve(strict=True)
        if (actual != sdk / record['resolved'] or not provider.inside(actual, sdk)
                or not actual.is_file() or provider.sha(actual) != record['sha256']):
            raise ValueError('SDK package transformation input differs: ' + name)
        result[name] = actual
    return result


def package_program(sdk, source):
    """Run the pinned real SDK pipeline on copies, checking its masked failures.

    rstrip.sh ends with true. Independently execute its exact ELF transformation
    with checked subprocess exits and require byte equality; script exit 0 alone
    is deliberately insufficient. Neither the source nor SDK files are written.
    """
    sdk = provider.canonical(str(sdk))
    tools = package_tools(sdk)
    before = source.read_bytes()
    if (len(before) < 64 or before[:6] != b'\x7fELF\x02\x01'
            or int.from_bytes(before[16:18], 'little') not in (2, 3)
            or int.from_bytes(before[18:20], 'little') != 183):
        raise ValueError('package input must be an AArch64 ELF executable/shared object')
    env = {'PATH': '/usr/bin:/bin', 'LC_ALL': 'C', 'TOPDIR': str(sdk / 'openwrt/openwrt'),
           'STRIP': str(tools['strip']) + ' --strip-all', 'PATCHELF': str(tools['patchelf'])}
    with tempfile.TemporaryDirectory(prefix='mimi-package-', dir='/tmp') as directory:
        actual, strict = Path(directory) / 'actual', Path(directory) / 'strict'
        for path in (actual, strict):
            path.write_bytes(before)
            path.chmod(0o755)
        def run(command):
            return subprocess.run([str(x) for x in command], env=env, check=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
        old = run([tools['patchelf'], '--print-rpath', strict]).stdout.decode().rstrip('\n')
        # Mirrors the pinned shell case patterns with IFS=':'. Base /lib and
        # /usr/lib are intentionally removed by that SDK; their children remain.
        new = ':'.join(path for path in old.split(':')
                       if re.fullmatch(r'(?:/lib/[^/].*|/usr/lib/[^/].*|\$ORIGIN/.*|\$ORIGIN)', path))
        if new != old:
            run([tools['patchelf'], '--set-rpath', new, strict])
        run([tools['strip'], '--strip-all', strict])
        run(['/bin/bash', tools['rstrip'], actual])
        if actual.read_bytes() != strict.read_bytes() or actual.stat().st_mode & 0o7777 != 0o755:
            raise ValueError('SDK package pipeline differs from strict transformation replay')
        output = actual.read_bytes()
    if package_tools(sdk) != tools or source.read_bytes() != before:
        raise ValueError('package transformation inputs changed during derivation')
    return dict(sha256=hashlib.sha256(output).hexdigest(), bytes=len(output), mode=0o755)


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)


def repository(value, expected):
    root = provider.canonical(value)
    if git(root, 'rev-parse', '--show-toplevel').decode().strip() != str(root):
        raise ValueError('source must be its repository root')
    origin = git(root, 'remote', 'get-url', 'origin').decode().strip()
    allowed = {'https://github.com/' + expected, 'https://github.com/' + expected + '.git',
               'git@github.com:' + expected, 'git@github.com:' + expected + '.git'}
    if origin not in allowed:
        raise ValueError('unexpected source repository')
    if git(root, 'status', '--porcelain', '--untracked-files=no'):
        raise ValueError('tracked source changes must be committed first')
    head = git(root, 'rev-parse', 'HEAD').decode().strip()
    if not COMMIT.fullmatch(head):
        raise ValueError('missing fixed source commit')
    return root, dict(repository=expected, commit=head, files={})


def blob(root, name, actual):
    path = PurePosixPath(name)
    if path.is_absolute() or str(path) != name or '..' in path.parts or not name:
        raise ValueError('unsafe source blob path')
    row = git(root, 'ls-tree', '-z', 'HEAD', '--', name).rstrip(b'\0')
    metadata, selected = row.split(b'\t')
    mode, kind, oid = metadata.decode().split()
    if selected.decode() != name or kind != 'blob' or mode not in ('100644', '100755'):
        raise ValueError('source must be a committed regular blob')
    original = git(root, 'cat-file', 'blob', oid)
    if actual.resolve() != actual or not actual.is_file() or original != actual.read_bytes():
        raise ValueError('actual input differs from committed source: ' + name)
    actual_mode = '100755' if actual.stat().st_mode & 0o111 else '100644'
    if actual_mode != mode:
        raise ValueError('source executable mode differs from Git blob')
    return dict(sha256=hashlib.sha256(original).hexdigest(), git_blob=oid,
                git_mode=mode, bytes=len(original))


def source_context(args):
    # Public must reject private selection before reading any repository or SDK.
    if args.edition == 'public' and (args.private_sdk or args.private_product_root):
        raise ValueError('public source lock forbids private product inputs')
    sdk, component, tool, recipe, private_sdk, compiler, product = provider.preflight(
        args.sdk, args.edition, args.private_sdk, args.private_product_root)
    public_root, public = repository(str(ROOT), PUBLIC)
    hardware_root, hardware = repository(args.hardware_source, HARDWARE)
    repositories = dict(application=public, hardware=hardware)
    private_root = None
    if product:
        private_root, private = repository(str(product[0]), PRIVATE)
        repositories['private_product'] = private
    for name in ['scripts/build_a133_provider.py', 'scripts/check_product_edition.py',
                 'scripts/tuya_pairing_sources.py', 'scripts/lock_product_sources.py']:
        public['files'][name] = blob(public_root, name, public_root / name)
    selected, _ = tuya.selection(args.edition, args.private_product_root)
    for staged, actual in selected.items():
        target = repositories['private_product'] if private_root and provider.inside(actual, private_root) else public
        root = private_root if target is repositories.get('private_product') else public_root
        name = actual.relative_to(root).as_posix()
        target['files'][name] = blob(root, name, actual)
    sources = [tool, component / provider.EDITION / args.edition / 'provider-recipe.json']
    for name in recipe['sources']:
        actual, _ = provider.selected_source(component, args.edition, name, product)
        if provider.inside(actual, sdk):
            sources.append(actual)
        else:
            relative = actual.relative_to(private_root).as_posix()
            repositories['private_product']['files'][relative] = blob(private_root, relative, actual)
    for actual in sources:
        name = 'overlay/' + actual.relative_to(sdk).as_posix()
        hardware['files'][name] = blob(hardware_root, name, actual)
    overlay_name = 'meta/overlay-manifest.json'
    hardware['files'][overlay_name] = blob(hardware_root, overlay_name, hardware_root / overlay_name)
    if private_root:
        for name in (provider.PRIVATE_LOCK, provider.TUYA_LOCK):
            repositories['private_product']['files'][name] = blob(private_root, name, private_root / name)
    sdk_manifest = sdk / '.repo/manifest.xml'
    if not sdk_manifest.is_file():
        raise ValueError('missing SDK manifest')
    context = dict(repositories=repositories, sdk_manifest_sha256=provider.sha(sdk_manifest),
                   recipe_sha256=provider.RECIPES[args.edition],
                   source_graph_sha256=recipe['source_graph_sha256'],
                   provider_tool_sha256=provider.TOOL_HASH, compiler_sha256=recipe['compiler_sha256'],
                   strip_tool_sha256=STRIP_SHA256, package_transform=PACKAGE_TRANSFORM)
    strip = compiler.with_name('aarch64-openwrt-linux-strip').resolve(strict=True)
    if not provider.inside(strip, sdk) or provider.sha(strip) != STRIP_SHA256:
        raise ValueError('SDK package strip tool differs from the qualified input')
    package_tools(sdk)
    return context


def collect(args):
    context = source_context(args)
    if args.check_sources:
        return dict(status='passed', scope='Selected committed source blobs and SDK recipe preflight only', **context)
    build = provider.canonical(args.build)
    programs = edition_gate.check(str(build), args.edition)
    # Recheck after reading programs, rather than attaching a stale source snapshot.
    if source_context(args) != context:
        raise ValueError('source changed during product locking')
    lock = dict(schema_version=2, kind='platform-product-lock', requirement='REQ-MIMI-EDITION-067',
                edition=args.edition, platform_id='a133', **context,
                programs={key: programs[key] for key in ('provider_sha256', 'tuya_sha256',
                          'provider_record_sha256', 'tuya_record_sha256')},
                rootfs_programs={},
                evidence_scope='Selected source blobs match these commits and existing compiled receipts; this does not assert the programs were rebuilt at these commits',
                full_sdk_inventory='requires separate system build lock',
                full_image='not_run', physical='not_run', release_qualified=False)
    selected = {'usr/bin/lv_backend': build / 'lv_port_linux/lv_backend'}
    if args.edition == 'private':
        selected['usr/bin/your_chat_bot_QIO_1.0.1.bin'] = build / 'tuyaopen/your_chat_bot_QIO_1.0.1.bin'
    sdk = provider.canonical(args.sdk)
    for name, actual in selected.items():
        lock['rootfs_programs'][name] = package_program(sdk, actual)
    if source_context(args) != context or edition_gate.check(str(build), args.edition) != programs:
        raise ValueError('source or program changed while deriving rootfs hashes')
    validate(lock)
    output = provider.canonical(args.output)
    if any(provider.inside(output, provider.canonical(value)) for value in
           (args.sdk, args.hardware_source, args.private_product_root) if value):
        raise ValueError('product lock output cannot overwrite source inputs')
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('x') as stream:
        stream.write(json.dumps(lock, sort_keys=True, indent=2) + '\n')
    return lock


def validate(lock):
    required = {'schema_version', 'kind', 'requirement', 'edition', 'platform_id', 'repositories',
                'sdk_manifest_sha256', 'recipe_sha256', 'source_graph_sha256', 'provider_tool_sha256',
                'compiler_sha256', 'strip_tool_sha256', 'programs', 'rootfs_programs', 'evidence_scope',
                'full_sdk_inventory', 'full_image', 'physical', 'release_qualified'}
    version = lock.get('schema_version')
    if type(version) is int and version == 2:
        required.add('package_transform')
        # JSON canonical bytes distinguish bool from int, unlike Python ==.
        if json.dumps(lock.get('package_transform'), sort_keys=True) != json.dumps(PACKAGE_TRANSFORM, sort_keys=True):
            raise ValueError('unknown SDK package transformation provenance')
        if lock.get('strip_tool_sha256') != STRIP_SHA256:
            raise ValueError('package transformation strip digest mismatch')
    if (set(lock) != required or type(version) is not int or version not in (1, 2) or lock['kind'] != 'platform-product-lock'
            or lock['requirement'] != 'REQ-MIMI-EDITION-067' or lock['platform_id'] != 'a133'
            or lock['edition'] not in ('public', 'private') or lock['release_qualified'] is not False
            or lock['full_image'] != 'not_run' or lock['physical'] != 'not_run'
            or lock['full_sdk_inventory'] != 'requires separate system build lock'
            or not isinstance(lock['evidence_scope'], str) or not lock['evidence_scope']):
        raise ValueError('unknown/overqualified platform product lock')
    expected = {'application': PUBLIC, 'hardware': HARDWARE}
    if lock['edition'] == 'private':
        expected['private_product'] = PRIVATE
    if set(lock['repositories']) != set(expected):
        raise ValueError('product repository composition mismatch')
    for name, repo in lock['repositories'].items():
        if (set(repo) != {'repository', 'commit', 'files'} or repo['repository'] != expected[name]
                or not COMMIT.fullmatch(repo['commit']) or not repo['files']):
            raise ValueError('invalid product source reference')
        for path, item in repo['files'].items():
            p = PurePosixPath(path)
            if (p.is_absolute() or str(p) != path or '..' in p.parts or not path or '\\' in path
                    or set(item) != {'sha256', 'git_blob', 'git_mode', 'bytes'}
                    or not HASH.fullmatch(item['sha256']) or not COMMIT.fullmatch(item['git_blob'])
                    or item['git_mode'] not in ('100644', '100755') or type(item['bytes']) is not int or item['bytes'] < 0):
                raise ValueError('invalid product source blob')
    for field in ('sdk_manifest_sha256', 'recipe_sha256', 'source_graph_sha256', 'provider_tool_sha256', 'compiler_sha256', 'strip_tool_sha256'):
        if not HASH.fullmatch(lock[field]):
            raise ValueError('invalid product input digest')
    if set(lock['programs']) != {'provider_sha256', 'tuya_sha256', 'provider_record_sha256', 'tuya_record_sha256'} or any(not HASH.fullmatch(v) for v in lock['programs'].values()):
        raise ValueError('invalid product program receipts')
    installed = {'usr/bin/lv_backend'}
    if lock['edition'] == 'private':
        installed.add('usr/bin/your_chat_bot_QIO_1.0.1.bin')
    if set(lock['rootfs_programs']) != installed:
        raise ValueError('product installed program composition mismatch')
    for value in lock['rootfs_programs'].values():
        if (set(value) != {'sha256', 'bytes', 'mode'} or not HASH.fullmatch(value['sha256'])
                or type(value['bytes']) is not int or value['bytes'] <= 0 or type(value['mode']) is not int or value['mode'] != 0o755):
            raise ValueError('invalid stripped rootfs program')
    return lock


def check_rootfs(lock, root):
    validate(lock)
    marker = root / 'etc/smartdesktopmimi/product-edition'
    if marker.resolve() != marker or marker.read_text().strip() != lock['edition']:
        raise ValueError('rootfs product edition differs from source lock')
    for name, entry in lock['rootfs_programs'].items():
        path = root / name
        if (path.resolve() != path or not path.is_file() or provider.sha(path) != entry['sha256']
                or path.stat().st_size != entry['bytes'] or path.stat().st_mode & 0o7777 != entry['mode']):
            raise ValueError('rootfs program differs from selected/stripped product: ' + name)
    if lock['edition'] == 'public':
        for name in ('usr/bin/your_chat_bot_QIO_1.0.1.bin', 'etc/aitvbox-tuya-pid', 'usr/share/tuyaopen_models'):
            path = root / name
            if path.exists() or path.is_symlink():
                raise ValueError('public rootfs contains a private device runtime')
    for name in ('factory/tuya/license.env', 'overlay/factory/tuya/license.env', 'etc/100ask/device_sig', 'etc/100ask/secret'):
        path = root / name
        if path.exists() or path.is_symlink():
            raise ValueError('generic rootfs contains per-device provisioning material')
    return dict(status='passed', scope='Extracted rootfs edition/program hashes and no per-device provisioning material', edition=lock['edition'])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--edition', choices=['public', 'private'], required=True)
    for name in ['sdk', 'hardware-source']:
        p.add_argument('--' + name, required=True)
    for name in ['private-sdk', 'private-product-root', 'build', 'output']:
        p.add_argument('--' + name)
    p.add_argument('--check-sources', action='store_true')
    args = p.parse_args()
    try:
        if not args.check_sources and (not args.build or not args.output):
            raise ValueError('--build and --output are required')
        print(json.dumps(collect(args), sort_keys=True))
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        p.exit(1, 'PRODUCT SOURCE LOCK BLOCKED: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
