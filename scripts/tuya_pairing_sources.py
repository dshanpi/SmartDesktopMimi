#!/usr/bin/env python3
"""Select explicit Tuya pairing sources; reject stale profiles before any build."""
import argparse
import json
from pathlib import Path
import shutil
import sys

from build_a133_provider import TUYA_LOCK, TUYA_LOCK_HASH, pinned_file, canonical, inside, sha

ROOT = Path(__file__).resolve().parents[1]
APP = 'apps/tuya.ai/your_chat_bot/'
RECEIPT = '.mimi-pairing/sources.json'
ABI_SHA = '14775e0d9a1a06ce60946c89dc024e8379c08ff28149009400d58139b5bfe1e4'


def selection(profile, private_value=None):
    if profile not in ('public', 'private'):
        raise ValueError('explicit public/private edition required')
    if profile == 'public' and private_value:
        raise ValueError('public Tuya edition forbids private product input')
    interface = ROOT / 'integrations/tuyaopen/pairing/mimi_device_pairing.h'
    pinned_file(interface, ROOT, ABI_SHA)
    lock_sha = None
    if profile == 'private':
        if not private_value:
            raise ValueError('private Tuya edition requires explicit private product root')
        private = canonical(private_value)
        if inside(private, ROOT) or inside(ROOT, private):
            raise ValueError('private Tuya product must be separate from public source')
        lock_file = private / TUYA_LOCK
        pinned_file(lock_file, private, TUYA_LOCK_HASH)
        lock = json.loads(lock_file.read_text())
        expected = {'adapters/tuya/mimi_device_pairing.c', 'adapters/tuya/mimi_tuya_license_provider.h'}
        if (lock.get('schema') != 1 or lock.get('component') != 'tuya-device-pairing' or
                lock.get('api_version') != 1 or lock.get('interface_sha256') != ABI_SHA or
                set(lock.get('files', {})) != expected):
            raise ValueError('private Tuya component/interface mismatch')
        for name, item in lock['files'].items():
            source = private / name
            pinned_file(source, private, item['sha256'])
            mode = '100755' if source.stat().st_mode & 0o111 else '100644'
            if mode != item['git_mode'] or source.stat().st_size != item['bytes']:
                raise ValueError('private Tuya source mode/size mismatch')
        runtime = private / 'adapters/tuya/mimi_device_pairing.c'
        lock_sha = TUYA_LOCK_HASH
    else:
        runtime = ROOT / 'integrations/tuyaopen/pairing/mimi_device_pairing_stub.c'
    selected = {
        APP + 'include/mimi_device_pairing.h': interface,
        APP + 'src/mimi_device_pairing.c': runtime,
        APP + 'src/tuya_main.c': ROOT / 'third_party/TuyaOpen' / APP / 'src/tuya_main.c',
        APP + 'CMakeLists.txt': ROOT / 'third_party/TuyaOpen' / APP / 'CMakeLists.txt',
        APP + 'build_a133.sh': ROOT / 'integrations/tuyaopen/build_a133.sh',
        APP + 'config/A133_B6.config': ROOT / 'integrations/tuyaopen/config/A133_B6.config',
    }
    for source in selected.values():
        if source.resolve() != source or not source.is_file():
            raise ValueError('missing or aliased pairing input')
    main = selected[APP + 'src/tuya_main.c'].read_text()
    entry = main.split('void user_main(void)\n{', 1)
    if (len(entry) != 2 or 'if (!mimi_tuya_pairing_enabled()) return;' not in entry[1].split('int ret =', 1)[0]
            or 'tuya_iot_license_read(' in main or 'tuya_iot_init(' in main):
        raise ValueError('Tuya main must disable public before initialization and delegate pairing')
    record = dict(schema=1, requirement='REQ-MIMI-PRIVATE-066', profile=profile,
                  interface_sha256=ABI_SHA, private_source_lock_sha256=lock_sha,
                  files={name: sha(source) for name, source in sorted(selected.items())},
                  checker_sha256=sha(Path(__file__).resolve()), release_qualified=False)
    return selected, record


def staged_root(value):
    root = canonical(value)
    protected = (ROOT, ROOT / 'third_party/TuyaOpen')
    if root in protected or any(inside(p, root) for p in protected):
        raise ValueError('build root cannot overwrite source or its parent')
    return root


def stage(value, profile, private_value=None):
    selected, receipt = selection(profile, private_value)
    root = staged_root(value)
    if not (root / 'tos.py').is_file():
        raise ValueError('Tuya build tree missing tos.py')
    # Remove the old receipt first; partial staging cannot look like a valid tree.
    receipt_file = root / RECEIPT
    if receipt_file.exists():
        if receipt_file.resolve() != receipt_file:
            raise ValueError('aliased pairing receipt')
        receipt_file.unlink()
    for name, source in selected.items():
        target = root / name
        if target.resolve() != target:
            raise ValueError('aliased pairing destination')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o755 if name.endswith('/build_a133.sh') else 0o644)
    selection(profile, private_value)  # Private source changed while copying is a failure.
    receipt_file.parent.mkdir(parents=True, exist_ok=True)
    receipt_file.write_text(json.dumps(receipt, sort_keys=True, indent=2) + '\n')
    return verify(value, profile, private_value)


def verify(value, profile, private_value=None):
    _, expected = selection(profile, private_value)
    root = staged_root(value)
    receipt_file = root / RECEIPT
    if receipt_file.resolve() != receipt_file or not receipt_file.is_file():
        raise ValueError('missing/aliased Tuya pairing receipt; run prepare')
    actual = json.loads(receipt_file.read_text())
    if actual != expected:
        raise ValueError('stale Tuya pairing profile/source receipt; run prepare')
    for name, digest in expected['files'].items():
        pinned_file(root / name, root, digest)
    names = list((root / APP / 'src').glob('mimi_device_pairing*.c'))
    if names != [root / APP / 'src/mimi_device_pairing.c']:
        raise ValueError('unexpected pairing implementations in build tree')
    return dict(status='passed', phase='staged-source', **expected)


def record(value, profile, private_value=None):
    receipt = verify(value, profile, private_value)
    root = staged_root(value)
    name = APP + 'dist/your_chat_bot_1.0.1/your_chat_bot_QIO_1.0.1.bin'
    binary = root / name
    if binary.resolve() != binary or not binary.is_file():
        raise ValueError('missing/aliased Tuya program')
    with binary.open('rb') as f:
        header = f.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\xb7\x00':
        raise ValueError('Tuya program must be ELF64 little-endian AArch64')
    receipt.update(phase='compiled', binary=name, binary_sha256=sha(binary), binary_bytes=binary.stat().st_size)
    (root / '.mimi-pairing/build-result.json').write_text(json.dumps(receipt, sort_keys=True, indent=2) + '\n')
    return receipt


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['check', 'stage', 'verify', 'record'])
    p.add_argument('--profile', choices=['public', 'private'], required=True)
    p.add_argument('--private-product-root')
    p.add_argument('--build-root')
    a = p.parse_args()
    try:
        if a.action == 'check':
            result = dict(status='passed', phase='preflight', **selection(a.profile, a.private_product_root)[1])
            if a.build_root:
                build_root = staged_root(a.build_root)
                if a.private_product_root:
                    private = canonical(a.private_product_root)
                    if inside(build_root, private) or inside(private, build_root):
                        raise ValueError('Tuya build root overlaps private product input')
        elif not a.build_root:
            raise ValueError('--build-root is required')
        else:
            result = {'stage': stage, 'verify': verify, 'record': record}[a.action](a.build_root, a.profile, a.private_product_root)
        print(json.dumps(result, sort_keys=True))
    except (ValueError, KeyError, OSError) as error:
        p.exit(1, 'TUYA PAIRING BLOCKED: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
