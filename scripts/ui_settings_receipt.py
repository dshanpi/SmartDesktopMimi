#!/usr/bin/env python3
"""Selected shared-writer evidence, not full desktop/source provenance."""
import argparse
import hashlib
import json
from pathlib import Path

INPUTS = ('apps/lv_port_linux/src/system/settings.c', 'apps/lv_port_linux/src/system/settings.h')
PROTOCOL = 'atomic-sidecar-v1'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def inputs(root): return {name: sha(root / name) for name in INPUTS}
def load(path):
    if path.is_symlink() or not path.is_file(): raise ValueError('missing or aliased UI writer receipt')
    return json.loads(path.read_text())
def prepare(root, build):
    value = dict(schema=1, protocol=PROTOCOL, inputs=inputs(root))
    (build / 'settings-inputs-before.json').write_text(json.dumps(value, indent=2))
def record(root, build):
    before = load(build / 'settings-inputs-before.json')
    if before != dict(schema=1, protocol=PROTOCOL, inputs=inputs(root)):
        raise ValueError('shared settings inputs changed during UI build')
    commands = load(build / 'compile_commands.json')
    selected = [item for item in commands if Path(item['file']) == root / INPUTS[0]]
    if len(selected) != 1: raise ValueError('UI settings compilation missing or duplicated')
    binary = build / 'bin/lvglsim'
    if binary.is_symlink(): raise ValueError('aliased UI program')
    header = binary.read_bytes()[:20]
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\xb7\x00':
        raise ValueError('UI program is not AArch64 ELF')
    result = dict(before, status='passed', phase='compiled', binary_sha256=sha(binary),
                  binary_bytes=binary.stat().st_size, compile_commands_sha256=sha(build/'compile_commands.json'))
    target = build / 'settings-build-result.json'
    temp = build / '.settings-build-result.tmp'
    temp.write_text(json.dumps(result, indent=2));temp.replace(target)
    return result
if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase',choices=['prepare','record']);parser.add_argument('--root',type=Path,required=True);parser.add_argument('--build',type=Path,required=True)
    args=parser.parse_args()
    try:
        root=args.root.resolve();build=args.build.resolve()
        if args.phase=='prepare': prepare(root,build)
        else: print(json.dumps(record(root,build)))
    except (ValueError,OSError,KeyError) as error: parser.exit(1,'UI WRITER BLOCKED: '+str(error)+'\n')
