#!/usr/bin/env python3
"""Check paired system services and appctl before package mutation."""
import argparse
import json
from pathlib import Path
from check_product_edition import load
from build_a133_provider import canonical, pinned_file, sha
from build_a133_services import LOCK_SHA256

ROOT = Path(__file__).resolve().parents[1]

def check(build, product):
    build, product = canonical(build), canonical(product)
    ref = load(ROOT / 'docs/update/services-program-reference.json')
    if ref.get('schema') != 1 or ref.get('source_lock_sha256') != LOCK_SHA256:
        raise ValueError('services reference lock mismatch')
    receipt = load(build / 'platform/services-build-result.json')
    if (receipt.get('status') != 'passed' or receipt.get('phase') != 'compiled' or
            receipt.get('source_lock_sha256') != LOCK_SHA256 or
            receipt.get('no_original_source_dependency') is not True or
            receipt.get('artifacts') != ref['artifacts']):
        raise ValueError('services program/reference mismatch')
    for name, metadata in ref['artifacts'].items():
        binary = build / 'platform' / name
        pinned_file(binary, build, metadata['sha256'])
        if binary.stat().st_size != metadata['bytes']:
            raise ValueError('service program size mismatch: ' + name)
        with binary.open('rb') as stream: header = stream.read(20)
        if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\xb7\x00':
            raise ValueError('service program architecture mismatch: ' + name)
    pinned_file(product / 'platform/runtime/aitvbox-appctl', product, ref['appctl_sha256'])
    return dict(status='passed', source_lock_sha256=LOCK_SHA256,
                programs=len(ref['artifacts']), appctl_sha256=ref['appctl_sha256'],
                release_qualified=False)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True);parser.add_argument('--product', required=True)
    args=parser.parse_args()
    try: print(json.dumps(check(args.build,args.product)))
    except (ValueError, OSError, KeyError) as error: parser.exit(1,'SERVICE PROGRAMS BLOCKED: '+str(error)+'\n')
