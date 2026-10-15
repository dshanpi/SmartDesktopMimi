#!/usr/bin/env python3
"""Product image filenames and deterministic gzip; never certifies hardware."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
from release_identity import valid_version

PREFIX = 'SmartDesktopMimi-SystemImage-v'
LEGACY = 'system.img.gz'


def image_name(version):
    if not valid_version(version):
        raise ValueError('invalid system image version')
    name = PREFIX + version + '.img.gz'
    if len(name) > 140:
        raise ValueError('system image filename is too long')
    return name


def release_image(lock):
    """New locks name the canonical image; historical locks remain readable."""
    expected = image_name(lock['version'])
    assets = lock['assets']
    if not isinstance(assets, dict):
        raise ValueError('system assets must be a map')
    products = {n for n in assets if n.startswith(PREFIX) and n.endswith('.img.gz')}
    named = lock.get('image_asset')
    if named is not None:
        if named != expected or products != {expected} or LEGACY in assets:
            raise ValueError('system image name/version/asset inventory mismatch')
        return expected
    if products or LEGACY not in assets:
        raise ValueError('canonical system image requires its version-bound image_asset')
    return LEGACY


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def raw_digest(path, max_bytes=None):
    h = hashlib.sha256()
    size = 0
    with gzip.open(path, 'rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            size += len(chunk)
            if max_bytes is not None and size > max_bytes:
                raise ValueError("decoded image exceeds pinned raw size")
            h.update(chunk)
    return h.hexdigest(), size


def archive(source, directory, version, expected_raw_sha256=None):
    source, directory = Path(source), Path(directory)
    name = image_name(version)
    for path in (source, directory):
        if not path.is_absolute() or path != path.resolve() or path.is_symlink():
            raise ValueError('canonical absolute image paths required')
    if not source.is_file() or source.stat().st_size == 0:
        raise ValueError('nonempty regular raw image required')
    target = directory / name
    if target.exists() or target.is_symlink():
        raise ValueError('image output already exists; never overwrite a candidate')
    before = source.stat()
    raw = digest(source)
    if expected_raw_sha256 is not None and raw != expected_raw_sha256:
        raise ValueError('raw image differs from external SHA256 pin')
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.mimi-image-', dir=directory) as temp:
        staging = Path(temp) / name
        with source.open('rb') as inp, staging.open('xb') as out:
            with gzip.GzipFile(filename='', mode='wb', fileobj=out, mtime=0) as compressed:
                shutil.copyfileobj(inp, compressed, 1 << 20)
            out.flush()
            os.fsync(out.fileno())
        after = source.stat()
        if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) !=
                (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or digest(source) != raw):
            raise ValueError('raw image source changed during compression')
        decoded, size = raw_digest(staging, before.st_size)
        if decoded != raw or size != before.st_size:
            raise ValueError('compressed image readback differs from raw input')
        result = dict(schema_version=1, kind='system-image-archive', filename=name,
                      system_version=version, raw_sha256=raw, raw_bytes=size,
                      archive_sha256=digest(staging), archive_bytes=staging.stat().st_size,
                      physical_acceptance='not_run')
        # Hard-link publication is atomic and refuses even a racing destination.
        os.link(staging, target)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--expected-raw-sha256')
    parser.add_argument('--record', type=Path)
    args = parser.parse_args()
    try:
        if args.record and (args.record.exists() or args.record.is_symlink()):
            raise ValueError('image archive record already exists')
        result = archive(args.input.absolute(), args.directory.absolute(), args.version, args.expected_raw_sha256)
        if args.record:
            with args.record.open('x') as stream:
                stream.write(json.dumps(result, indent=2, sort_keys=True) + '\n')
        print(json.dumps(result))
    except (ValueError, OSError) as error:
        parser.exit(1, 'SYSTEM IMAGE BLOCKED: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
