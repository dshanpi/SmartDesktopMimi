#!/usr/bin/env python3
"""Reject mixed/stale provider and Tuya programs before image/package mutation."""
import argparse
import json
from pathlib import Path
import sys

from build_a133_provider import RECIPES, PRIVATE_LOCK_HASH, TUYA_LOCK_HASH, canonical, pinned_file, sha
import tuya_pairing_sources as tuya
import ui_settings_receipt as ui_writer

REFERENCE = {
    'public': 'ca41de0b311b9d5d2d34aa81e9dfc6ea4f45445d9b70e2c446a9721e0c83730f',
    'private': '626fc7e8926b1bfc20ccb30e3a91e172dc16f196d9110423f86e1281f98e5cfb',
}
PRIVATE_RUNTIME = '4ae0aa1d2bb328bfdb52e77bef6b6b741cadc48abe0d3f00cfaaa7c7c7c22518'


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate edition record key')
        result[key] = value
    return result


def load(path):
    if path.resolve() != path or not path.is_file() or path.stat().st_size > 128 * 1024:
        raise ValueError('missing/aliased/oversize edition build record')
    return json.loads(path.read_text(), object_pairs_hook=unique)


def check(build, edition):
    build = canonical(build)
    if edition not in REFERENCE:
        raise ValueError('explicit public/private product edition required')
    provider = load(build / 'lv_port_linux/provider-build-result.json')
    if (provider.get('requirement') != 'REQ-MIMI-PRIVATE-066' or provider.get('status') != 'passed' or
            provider.get('phase') != 'compiled' or provider.get('profile') != edition or
            provider.get('recipe_sha256') != RECIPES[edition] or
            provider.get('provider_edition_source_version') != 7 or
            provider.get('original_source_dependency') is not False or
            provider.get('reference_backend_sha256') != REFERENCE[edition] or
            provider.get('backend_sha256') != REFERENCE[edition]):
        raise ValueError('provider program edition/reference mismatch')
    if edition == 'private' and (provider.get('private_product_source_lock_sha256') != PRIVATE_LOCK_HASH or
                                 provider.get('private_tuya_source_lock_sha256') != TUYA_LOCK_HASH or
                                 provider.get('historical_sdk_pairing_sources_used') is not False):
        raise ValueError('private provider source lock mismatch')
    if edition == 'public' and any(key.startswith('private_') for key in provider):
        raise ValueError('public provider contains private component metadata')
    pinned_file(build / 'lv_port_linux/lv_backend', build, provider['backend_sha256'])
    ui = load(build / 'lv_port_linux/ui-settings-build-result.json')
    expected_writer = ui_writer.inputs(Path(__file__).resolve().parents[1])
    if (ui.get('schema') != 1 or ui.get('protocol') != ui_writer.PROTOCOL or
            ui.get('status') != 'passed' or ui.get('phase') != 'compiled' or
            ui.get('inputs') != expected_writer or provider.get('settings_inputs') != expected_writer):
        raise ValueError('UI/backend settings writer mismatch')
    desktop = build / 'lv_port_linux/lvglsim'
    pinned_file(desktop, build, ui['binary_sha256'])
    if desktop.stat().st_size != ui.get('binary_bytes'):
        raise ValueError('UI program size mismatch')
    with desktop.open('rb') as stream: header = stream.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\xb7\x00':
        raise ValueError('UI program architecture mismatch')
    runtime = load(build / 'tuyaopen/pairing-build-result.json')
    _, expected = tuya.selection('public')
    expected['profile'] = edition
    if edition == 'private':
        expected['private_source_lock_sha256'] = TUYA_LOCK_HASH
        expected['files'][tuya.APP + 'src/mimi_device_pairing.c'] = PRIVATE_RUNTIME
    expected['status'] = 'passed'
    expected['phase'] = 'compiled'
    for key, value in expected.items():
        if runtime.get(key) != value:
            raise ValueError('Tuya program source/edition mismatch: ' + key)
    binary = build / 'tuyaopen/your_chat_bot_QIO_1.0.1.bin'
    pinned_file(binary, build, runtime['binary_sha256'])
    if runtime.get('binary_bytes') != binary.stat().st_size:
        raise ValueError('Tuya program size mismatch')
    with binary.open('rb') as f:
        header = f.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\xb7\x00':
        raise ValueError('Tuya program architecture mismatch')
    return dict(schema=1, requirement='REQ-MIMI-PRIVATE-066', status='passed',
                scope='UI/provider shared writer and Tuya source edition/program hashes; not physical acceptance',
                edition=edition, desktop_sha256=ui['binary_sha256'], settings_protocol=ui_writer.PROTOCOL,
                provider_sha256=provider['backend_sha256'],
                tuya_sha256=runtime['binary_sha256'],
                provider_record_sha256=sha(build / 'lv_port_linux/provider-build-result.json'),
                tuya_record_sha256=sha(build / 'tuyaopen/pairing-build-result.json'),
                release_qualified=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', required=True)
    p.add_argument('--edition', choices=['public', 'private'], required=True)
    a = p.parse_args()
    try:
        print(json.dumps(check(a.build, a.edition), sort_keys=True))
    except (ValueError, KeyError, OSError) as error:
        p.exit(1, 'PRODUCT EDITION BLOCKED: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
