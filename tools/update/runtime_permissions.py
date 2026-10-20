#!/usr/bin/env python3
"""Read-only admission of A133 nonroot boot dependencies in a final filesystem."""
import argparse
from functools import lru_cache
from pathlib import Path, PurePosixPath
import posixpath
import re
import subprocess


# Minimum dependencies implicated in the real SYSTEM-070 ubus boot failure.
# This is not a claim that all dynamic dependencies or services are qualified.
REQUIRED = {
    '/lib/ld-linux-aarch64.so.1': 0o005,
    '/lib/libc.so.6': 0o004,
    '/lib/libgcc_s.so.1': 0o004,
    '/lib/libnss_files.so.2': 0o004,
    '/lib/libnss_dns.so.2': 0o004,
    '/etc/passwd': 0o004,
    '/etc/group': 0o004,
}


def text_mode(value):
    if not re.fullmatch(r'[dl-][rwxstST-]{9}', value):
        raise ValueError('unsupported filesystem mode')
    mode = 0
    for index, bit in enumerate((0o400, 0o200, 0o100, 0o040, 0o020,
                                 0o010, 0o004, 0o002, 0o001)):
        if value[index + 1] not in '-ST':
            mode |= bit
    for index, bit in ((3, 0o4000), (6, 0o2000), (9, 0o1000)):
        if value[index] in 'sStT':
            mode |= bit
    return mode


def reader(image):
    kind = subprocess.check_output(
        ['blkid', '-p', '-s', 'TYPE', '-o', 'value', str(image)], text=True).strip()
    if kind in ('ext2', 'ext3', 'ext4'):
        @lru_cache(None)
        def entry(path):
            if not re.fullmatch(r'/[A-Za-z0-9_./+-]*', path):
                raise ValueError('unsupported runtime path')
            value = subprocess.check_output(
                ['debugfs', '-R', 'stat "' + path + '"', str(image)],
                text=True, stderr=subprocess.DEVNULL)
            info = re.search(r'Type:\s*(\S+)\s+Mode:\s*([0-7]+)', value)
            owner = re.search(r'User:\s*(\d+)\s+Group:\s*(\d+)', value)
            if not info or not owner:
                raise ValueError('missing runtime path: ' + path)
            target = re.search(r'Fast link dest: "([^"]*)"', value)
            return (info[1], int(info[2], 8), int(owner[1]), int(owner[2]),
                    target[1] if target else None)
        return entry
    if kind == 'squashfs':
        entries = {}
        listing = subprocess.check_output(['unsquashfs', '-lln', str(image)], text=True)
        for line in listing.splitlines():
            fields = line.split(None, 5)
            if len(fields) != 6 or not re.fullmatch(r'\d+/\d+', fields[1]):
                continue
            name, _, target = fields[5].partition(' -> ')
            if name != 'squashfs-root' and not name.startswith('squashfs-root/'):
                continue
            path = name[len('squashfs-root'):] or '/'
            kind = {'-': 'regular', 'd': 'directory', 'l': 'symlink'}.get(fields[0][0])
            if kind:
                uid, gid = map(int, fields[1].split('/'))
                entries[path] = (kind, text_mode(fields[0]), uid, gid, target or None)
        def entry(path):
            if path not in entries:
                raise ValueError('missing runtime path: ' + path)
            return entries[path]
        return entry
    raise ValueError('unsupported runtime filesystem: ' + kind)


def verify(image):
    image = Path(image)
    if image.is_symlink() or not image.is_file() or image.stat().st_size == 0:
        raise ValueError('runtime filesystem must be a nonempty regular image')
    entry = reader(image)
    checked = {}

    def resolve(path, depth=0):
        if depth > 16:
            raise ValueError('runtime link cycle: ' + path)
        components = PurePosixPath(path).parts[1:]
        parent = '/'
        root = entry(parent)
        if root[0] != 'directory' or root[2:4] != (0, 0) or root[1] & 0o022 or not root[1] & 1:
            raise ValueError('unsafe runtime directory: /')
        for index, component in enumerate(components):
            path = posixpath.join(parent, component)
            item = entry(path)
            if item[2:4] != (0, 0):
                raise ValueError('runtime path is not root-owned: ' + path)
            if item[0] == 'symlink':
                if not item[4]:
                    raise ValueError('unsupported runtime link: ' + path)
                target = item[4] if item[4].startswith('/') else posixpath.join(parent, item[4])
                target = posixpath.normpath(posixpath.join(target, *components[index + 1:]))
                return resolve(target, depth + 1)
            if item[1] & 0o022:
                raise ValueError('runtime path writable by non-owner: ' + path)
            if index != len(components) - 1:
                if item[0] != 'directory' or not item[1] & 1:
                    raise ValueError('runtime directory not traversable: ' + path)
            parent = path
        return path, item

    for path, required in REQUIRED.items():
        resolved, item = resolve(path)
        if item[0] != 'regular' or item[1] & required != required or item[1] & 0o7000:
            raise ValueError('nonroot runtime access denied or unsafe mode: ' + path)
        checked[path] = {'resolved': resolved, 'mode': oct(item[1])}
    resolved, shadow = resolve('/etc/shadow')
    if shadow[0] != 'regular' or shadow[1] & 0o7177 or not shadow[1] & 0o400:
        raise ValueError('shadow must remain private and non-executable')
    checked['/etc/shadow'] = {'resolved': resolved, 'mode': oct(shadow[1])}
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.image)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'RUNTIME PERMISSIONS BLOCKED: ' + str(error) + '\n')
    print('PASS final filesystem runtime permissions: ' + str(len(result)) +
          ' paths; physical boot qualification still required')


if __name__ == '__main__':
    main()
