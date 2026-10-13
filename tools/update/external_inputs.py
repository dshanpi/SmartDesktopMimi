#!/usr/bin/env python3
"""Fingerprint external platform inputs without copying their contents into source."""
import argparse, hashlib, json, os
from pathlib import Path

def inventory(root):
    root = root.resolve(strict=True)
    if not root.is_dir(): raise ValueError('input must be a directory')
    entries = {}
    for p in sorted(root.rglob('*')):
        relative = p.relative_to(root)
        if '.git' in relative.parts: continue
        name = relative.as_posix()
        mode = p.lstat().st_mode & 0o777
        if p.is_symlink():
            target = os.readlink(p)
            if os.path.isabs(target): raise ValueError('absolute input link: '+name)
            p.resolve(strict=True).relative_to(root)
            entries[name] = {'link': target}
        elif p.is_file():
            h = hashlib.sha256()
            with p.open('rb') as f:
                for chunk in iter(lambda: f.read(1024*1024), b''): h.update(chunk)
            entries[name] = {'sha256': h.hexdigest(), 'bytes': p.stat().st_size, 'mode': mode}
        elif p.is_dir(): entries[name] = {'directory': True, 'mode': mode}
        else: raise ValueError('unsupported input node: '+name)
    if not entries: raise ValueError('empty external input')
    return {'schema_version': 1, 'files': entries}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root',type=Path);parser.add_argument('--expect');parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        data=(json.dumps(inventory(args.root),sort_keys=True,indent=2)+'\n').encode()
        digest=hashlib.sha256(data).hexdigest()
        if args.expect and digest != args.expect: raise ValueError('external platform inputs changed')
        if args.output: args.output.write_bytes(data)
        print(digest)
    except (OSError,ValueError,RuntimeError) as e: parser.exit(1,'EXTERNAL INPUT BLOCKED: '+str(e)+'\n')
if __name__=='__main__':main()
