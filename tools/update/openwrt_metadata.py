#!/usr/bin/env python3
"""Reject incomplete product metadata even when the vendor scanner exits zero."""
import argparse
import hashlib
import json
from pathlib import Path

PACKAGES = ('aitvbox-ipkvm', 'aitvbox-platform', 'aitvbox-suite',
            'aitvbox-mimi-update', 'aitvbox-usb-hid')


def verify(path):
    if not path.is_file() or path.is_symlink() or path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('missing, linked or oversized OpenWrt package metadata')
    raw = path.read_bytes()
    source = None
    found = {name: [] for name in PACKAGES}
    for line in raw.decode('utf-8').splitlines():
        if line.startswith('Source-Makefile: '):
            source = line[len('Source-Makefile: '):]
        elif line.startswith('Package: '):
            package = line[len('Package: '):]
            if package in found:
                found[package].append(source)
    for name, sources in found.items():
        expected = 'package/subpackage/allwinner/custom/' + name + '/Makefile'
        if sources != [expected]:
            raise ValueError('missing, duplicate or wrong-source product package: ' + name)
    return dict(status='passed', packageinfo_sha256=hashlib.sha256(raw).hexdigest(),
                packages=list(PACKAGES), scope='metadata completeness; not package build or device acceptance')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packageinfo', type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.packageinfo)
    except (ValueError, OSError) as error:
        parser.exit(1, 'OPENWRT METADATA BLOCKED: ' + str(error) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
