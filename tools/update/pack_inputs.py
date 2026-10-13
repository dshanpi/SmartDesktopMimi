#!/usr/bin/env python3
"""Check restored A133 packing dependencies before an expensive product build."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex

GOLDEN_SHA256 = '15854172acfad7aa4c6b0dbed9a446aff989bc2c2c9825bab0bb20a41b3ff41d'
GOLDEN_NAME = 'a133_linux_b6_uart0-baseline-20260903.img'

def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()

def verify(sdk):
    sdk = sdk.resolve(strict=True)
    config = sdk / '.buildconfig'
    text = config.read_text()
    values = {}
    for line in text.splitlines():
        fields = shlex.split(line, comments=True)
        if not fields:
            continue
        if len(fields) != 2 or fields[0] != 'export' or '=' not in fields[1]:
            raise ValueError('unsupported build configuration syntax')
        key, value = fields[1].split('=', 1)
        if key in values:
            raise ValueError('duplicate build configuration key: ' + key)
        values[key] = value
    out = sdk / 'out/a133/b6/openwrt'
    expected = {'LICHEE_IC': 'a133', 'LICHEE_BOARD': 'b6',
                'LICHEE_LINUX_DEV': 'openwrt', 'LICHEE_TOP_DIR': str(sdk),
                'LICHEE_PLAT_OUT': str(out)}
    for key, value in expected.items():
        if values.get(key) != value:
            raise ValueError('wrong restored SDK target/path: ' + key)
    output_config = out / '.buildconfig'
    if output_config.read_bytes() != config.read_bytes():
        raise ValueError('output .buildconfig differs from selected SDK configuration')
    golden = out / GOLDEN_NAME
    if digest(golden) != GOLDEN_SHA256:
        raise ValueError('verified golden boot-chain image hash mismatch')
    return {'schema_version': 1, 'target': 'a133/b6/openwrt',
            'buildconfig_sha256': digest(config),
            'golden_image_sha256': GOLDEN_SHA256,
            'golden_image_bytes': golden.stat().st_size}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--expect', type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.sdk)
        if args.expect and json.loads(args.expect.read_text()) != result:
            raise ValueError('packing inputs changed during build')
        if args.output:
            args.output.write_text(json.dumps(result, indent=2) + '\n')
        print('PASS restored A133 packing inputs')
    except (OSError, ValueError) as error:
        parser.exit(1, 'PACK INPUT BLOCKED: ' + str(error) + '\n')

if __name__ == '__main__':
    main()
