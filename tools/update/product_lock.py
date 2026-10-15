#!/usr/bin/env python3
"""Validate product lock metadata or an extracted rootfs; no source/SDK access."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from lock_product_sources import validate, check_rootfs
from check_product_edition import load
from build_a133_provider import sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--lock', type=Path, required=True)
    p.add_argument('--rootfs', type=Path, required=True)
    p.add_argument('--image', type=Path)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    try:
        lock_path = a.lock.absolute()
        lock_sha = sha(lock_path)
        image_sha = sha(a.image) if a.image else None
        result = check_rootfs(load(lock_path), a.rootfs.resolve())
        if sha(lock_path) != lock_sha or (a.image and sha(a.image) != image_sha):
            raise ValueError('product lock or rootfs image changed during validation')
        result.update(schema_version=1, requirement='REQ-MIMI-EDITION-067', product_lock_sha256=lock_sha,
                      rootfs_image_sha256=image_sha, physical='not_run', release_qualified=False)
        if a.output:
            if not a.image:
                raise ValueError('--image is required for an image readback record')
            output = a.output.absolute()
            if output.resolve() != output or output.is_symlink():
                raise ValueError('aliased rootfs record output')
            output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(mode='w', prefix='.rootfs-record-', dir=output.parent, delete=False) as stream:
                temporary = Path(stream.name)
                try:
                    stream.write(json.dumps(result, sort_keys=True, indent=2) + '\n')
                    stream.flush(); os.fsync(stream.fileno())
                    temporary.chmod(0o644)
                    os.replace(temporary, output)
                finally:
                    if temporary.exists():temporary.unlink()
    except (ValueError, KeyError, TypeError, OSError) as error:
        p.exit(1, 'PRODUCT ROOTFS BLOCKED: ' + str(error) + '\n')
    print(json.dumps(result, sort_keys=True))


if __name__ == '__main__':
    main()
