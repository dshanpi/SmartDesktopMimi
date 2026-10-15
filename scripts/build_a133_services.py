#!/usr/bin/env python3
"""Use the fixed SDK platform services instead of application-side source copies."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

PREFIX = 'openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-services'
LOCK_SHA256 = '03cd9bd5e9b1661be82dc78f39606b6a65189b59f107ece6722b5d94982d3f58'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(sdk):
    sdk = Path(sdk)
    if not sdk.is_absolute() or sdk != sdk.resolve() or not (sdk / '.repo').is_dir():
        raise ValueError('absolute canonical A133 SDK root required')
    component = sdk / PREFIX
    path = component / 'source-lock.json'
    if path.resolve() != path or not path.is_file() or sha(path) != LOCK_SHA256:
        raise ValueError('missing or changed fixed SDK platform-services source lock')
    lock = json.loads(path.read_text())
    for name, digest in lock['files'].items():
        file = component / name
        if file.resolve() != file or component not in file.parents or not file.is_file() or sha(file) != digest:
            raise ValueError('fixed SDK platform-services source changed: ' + name)
    return sdk, component, lock


def run(args):
    sdk, component, lock = validate(args.sdk)
    command = [sys.executable, str(component / 'build_services.py'), '--sdk', str(sdk), '--component', str(component)]
    if args.check:
        command.append('--check')
    elif args.output:
        command += ['--output', args.output]
    else:
        raise ValueError('--output required')
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    result = json.loads(completed.stdout)
    if result.get('status') != 'passed' or result.get('source_lock_sha256') != LOCK_SHA256:
        raise ValueError('SDK services build receipt mismatch')
    if not args.check:
        if result.get('phase') != 'compiled' or result.get('artifacts') != lock['expected_artifacts'] or not result.get('no_original_source_dependency'):
            raise ValueError('SDK services artifact/source mismatch')
        for name, metadata in result['artifacts'].items():
            if sha(Path(args.output) / name) != metadata['sha256']:
                raise ValueError('SDK services artifact changed after build')
    validate(args.sdk)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output')
    args = parser.parse_args()
    try:
        print(json.dumps(run(args)))
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        detail = error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error)
        parser.exit(1, 'A133 SDK SERVICES BLOCKED: ' + str(detail).strip() + '\n')


if __name__ == '__main__':
    main()
