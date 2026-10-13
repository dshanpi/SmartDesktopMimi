#!/usr/bin/env python3
"""Read-only filesystem admission before firmware packing; never repair inputs."""
import argparse
from pathlib import Path
import subprocess


def verify(image):
    if image.is_symlink() or not image.is_file() or image.stat().st_size == 0:
        raise ValueError('filesystem image must be a nonempty regular file')
    kind = subprocess.check_output(['blkid','-p','-s','TYPE','-o','value',str(image)],text=True).strip()
    if kind in {'ext2','ext3','ext4'}:
        check = subprocess.run(['e2fsck','-f','-n',str(image)],text=True,
                               stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if check.returncode != 0:
            raise ValueError('filesystem integrity check failed (read-only exit '+str(check.returncode)+'):\n'+check.stdout)
    elif kind == 'squashfs':
        subprocess.run(['unsquashfs','-s',str(image)],check=True,stdout=subprocess.DEVNULL)
    else:
        raise ValueError('unsupported final filesystem: '+kind)
    # SquashFS extraction/readback remains a later firmware gate.
    return kind


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image',type=Path)
    args=parser.parse_args()
    try: kind=verify(args.image)
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        parser.exit(1,'FILESYSTEM BLOCKED: '+str(error)+'\n')
    print('Filesystem pre-pack check passed: '+kind+'; product readback and physical acceptance still required')


if __name__=='__main__': main()
