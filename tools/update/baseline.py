#!/usr/bin/env python3
"""Prepare/verify the signed factory overlay; never invent device enrollment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()

def inventory(root):
    result={}
    for p in sorted(root.rglob('*')):
        if p.is_symlink():raise ValueError('baseline cannot contain symbolic links')
        if p.is_file():
            if p.stat().st_mode&0o022:raise ValueError('baseline file is writable by non-owner')
            result[p.relative_to(root).as_posix()]={'sha256':digest(p),'mode':p.stat().st_mode&0o777}
        elif not p.is_dir():raise ValueError('baseline contains a special file')
    return result

def write(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n');path.chmod(0o644)

def verify(directory):
    seal=json.loads((directory/'baseline-lock.json').read_text())
    if seal.get('schema_version')!=1 or seal.get('policy')!='MIMI-UPDATE-001/1.0.0':raise ValueError('unknown baseline seal')
    root=directory/'rootfs'
    if inventory(root)!=seal['files']:raise ValueError('baseline has changed since preparation')
    factory=json.loads((root/'usr/lib/smartdesktopmimi/factory/manifest.json').read_text())
    device=json.loads((root/'etc/smartdesktopmimi/device.json').read_text())
    if factory['commit']!=seal['factory_commit'] or factory['package_sha256']!=seal['factory_package_sha256']:raise ValueError('factory provenance mismatch')
    if device.get('enrollment_id'):raise ValueError('unit-specific enrollment must not be baked into an image')
    if (root/'overlay').exists():raise ValueError('factory data does not belong in a generic image')
    return seal

def prepare(args):
    if args.output.exists():raise ValueError('output already exists')
    device=json.loads(args.device.read_text())
    # This reference provider implements only A133; other providers must be qualified separately.
    if device['identity']['platform_id']!='a133' or device['identity']['architecture']!='aarch64':raise ValueError('A133 provider cannot be used for another platform')
    with args.updater.open('rb') as f:header=f.read(20)
    if len(header)!=20 or header[:6]!=b'\x7fELF\x02\x01' or struct.unpack('<H',header[18:20])[0]!=183:raise ValueError('updater must be AArch64 little-endian ELF64')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.baseline-',dir=args.output.parent) as temp:
        stage=Path(temp);root=stage/'rootfs'
        config=root/'etc/smartdesktopmimi';config.mkdir(parents=True)
        factory=root/'usr/lib/smartdesktopmimi/factory';factory.parent.mkdir(parents=True)
        subprocess.run([str(args.checker),'extract-factory','--archive',str(args.application/'application.tar.gz'),
            '--manifest',str(args.application/'manifest.json'),'--signature',str(args.application/'manifest.sig'),
            '--trust',str(args.trust),'--device',str(args.device),'--output',str(factory)],check=True)
        device['enrollment_id']=''
        write(config/'device.json',device)
        write(config/'trust.json',json.loads(args.trust.read_text()))
        updater=json.loads((ROOT/'docs/update/updater.example.json').read_text())
        if getattr(args, 'system_device', None):
            system=json.loads(args.system_device.read_text())
            system['device']['enrollment_id']=''
            if system['device'] != device:
                raise ValueError('system and application device baselines differ')
            if system.get('storage')!='emmc' or system.get('boot_mode')!='ab':
                raise ValueError('system OTA provider requires A133 eMMC A/B')
            write(config/'system-device.json',system)
            updater['system_repository']=updater['repository']
            updater['system_device_file']='/etc/smartdesktopmimi/system-device.json'
        write(config/'updater.json',updater)
        binary=root/'usr/bin/mimi-update';binary.parent.mkdir(parents=True);shutil.copyfile(args.updater,binary);binary.chmod(0o755)
        for source,target in [('mimi-update.init','etc/init.d/mimi-update'),('mimi-desktop.init','etc/init.d/mimi-desktop'),('mimi-platform-provider','usr/libexec/mimi-platform-provider')]:
            dest=root/target;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/'packaging/aitvbox-mimi-update/files'/source,dest);dest.chmod(0o755)
        for directory in [root]+[p for p in root.rglob('*') if p.is_dir()]:directory.chmod(0o755)
        manifest=json.loads((factory/'manifest.json').read_text())
        write(stage/'baseline-lock.json',{'schema_version':1,'policy':'MIMI-UPDATE-001/1.0.0',
              'factory_commit':manifest['commit'],'factory_version':manifest['version'],'factory_package_sha256':manifest['package_sha256'],
              'device_input_sha256':digest(args.device),'files':inventory(root)})
        verify(stage)
        os.rename(stage,args.output)
    return verify(args.output)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['prepare','verify'])
    parser.add_argument('--output',type=Path,required=True)
    for name in ['device','trust','application','updater','checker']:parser.add_argument('--'+name,type=Path)
    parser.add_argument('--system-device',type=Path,help='controlled A133 system descriptor with measured partition geometry')
    args=parser.parse_args();args.output=args.output.resolve()
    if args.operation=='prepare':
        for name in ['device','trust','application','updater','checker']:
            value=getattr(args,name)
            if value is None:parser.error('prepare requires --'+name)
            setattr(args,name,value.resolve())
    try:result=prepare(args) if args.operation=='prepare' else verify(args.output)
    except (ValueError,KeyError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,'BASELINE BLOCKED: '+str(error)+'\n')
    print('PASS signed factory baseline '+result['factory_version']+'; physical enrollment and acceptance still required')
if __name__=='__main__':main()
