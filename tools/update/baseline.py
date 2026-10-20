#!/usr/bin/env python3
"""Prepare/verify the signed factory overlay; never invent device enrollment."""
import argparse
import hashlib
import json
import os
import posixpath
import re
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

def verify_product_edition(root, expected=None):
    marker = root/'etc/smartdesktopmimi/product-edition'
    system = root/'etc/smartdesktopmimi/system-device.json'
    declared = json.loads(system.read_text()).get('product_edition', '') if system.exists() else ''
    if not marker.exists() and not marker.is_symlink():
        if declared or expected is not None:raise ValueError('missing baseline product edition marker')
        return ''
    if marker.is_symlink() or not marker.is_file() or marker.stat().st_size > 16:
        raise ValueError('invalid baseline product edition marker')
    raw=marker.read_text()
    edition=raw[:-1] if raw.endswith('\n') else raw
    if (edition not in {'public','private'} or raw not in (edition,edition+'\n') or
            (system.exists() and declared != edition) or (expected is not None and expected != edition)):
        raise ValueError('baseline product edition descriptor/marker/selection mismatch')
    return edition


def verify_updater(directory, updater):
    """The sealed overlay must not replace the fresh build with an older updater."""
    embedded=directory/'rootfs/usr/bin/mimi-update'
    if updater.is_symlink() or not updater.is_file() or embedded.is_symlink() or not embedded.is_file():
        raise ValueError('updater source and baseline must be regular files')
    if digest(updater)!=digest(embedded):
        raise ValueError('factory baseline updater differs from freshly built updater; prepare a new baseline')


def verify(directory, edition=None):
    seal=json.loads((directory/'baseline-lock.json').read_text())
    if seal.get('schema_version')!=1 or seal.get('policy')!='MIMI-UPDATE-001/1.0.0':raise ValueError('unknown baseline seal')
    root=directory/'rootfs'
    if inventory(root)!=seal['files']:raise ValueError('baseline has changed since preparation')
    factory=json.loads((root/'usr/lib/smartdesktopmimi/factory/manifest.json').read_text())
    device=json.loads((root/'etc/smartdesktopmimi/device.json').read_text())
    if factory['commit']!=seal['factory_commit'] or factory['package_sha256']!=seal['factory_package_sha256']:raise ValueError('factory provenance mismatch')
    if device.get('enrollment_id'):raise ValueError('unit-specific enrollment must not be baked into an image')
    if (root/'overlay').exists():raise ValueError('factory data does not belong in a generic image')
    verify_product_edition(root, edition)
    return seal


def verify_image_owners(image, paths):
    """Inspect original filesystem metadata; extraction may lose ownership."""
    kind=subprocess.check_output(['blkid','-p','-s','TYPE','-o','value',str(image)],text=True).strip()
    protected={'.'}
    for name in paths:
        protected.add(name)
        protected.update(str(parent) for parent in Path(name).parents)
    owners={}
    if kind=='squashfs':
        listing=subprocess.check_output(['unsquashfs','-lln',str(image)],text=True)
        for line in listing.splitlines():
            fields=line.split(None,5)
            if len(fields)!=6 or not re.fullmatch(r'\d+/\d+',fields[1]):continue
            name=fields[5].split(' -> ',1)[0]
            if name=='squashfs-root':name='.'
            elif name.startswith('squashfs-root/'):name=name[len('squashfs-root/'):]
            else:continue
            owners[name]=tuple(int(value) for value in fields[1].split('/'))
    elif kind in ('ext2','ext3','ext4'):
        for name in sorted(protected):
            if any(ord(c)<32 or c in '\\"' for c in name):raise ValueError('unsupported filesystem metadata path')
            path='/' if name=='.' else '/'+name
            result=subprocess.check_output(['debugfs','-R','stat "'+path+'"',str(image)],text=True,stderr=subprocess.DEVNULL)
            match=re.search(r'User:\s*(\d+)\s+Group:\s*(\d+)',result)
            if match:owners[name]=tuple(int(value) for value in match.groups())
    else:raise ValueError('unsupported final filesystem: '+kind)
    for name in sorted(protected):
        if owners.get(name)!=(0,0):raise ValueError('final image protected path is not root-owned: '+name)


def verify_rootfs(directory, rootfs, image=None, edition=None):
    """Compare the final filesystem with the prepared, verified factory input.

    This is a packaging readback, not a replacement for signature verification
    during prepare or for physical enrollment and upgrade acceptance.
    """
    seal=verify(directory, edition)
    rootfs=rootfs.resolve(strict=True)

    def regular(relative):
        path=rootfs
        for part in Path(relative).parts:
            path=path/part
            if path.is_symlink():raise ValueError('final rootfs link in protected path: '+relative)
            if not path.exists():raise ValueError('final rootfs missing: '+relative)
            if path.stat().st_mode&0o022:raise ValueError('final rootfs writable protected path: '+relative)
        if not path.is_file():raise ValueError('final rootfs is not a regular file: '+relative)
        return path

    for relative,expected in seal['files'].items():
        actual=regular(relative)
        if digest(actual)!=expected['sha256'] or actual.stat().st_mode&0o777!=expected['mode']:
            raise ValueError('final rootfs differs from signed baseline: '+relative)
    # Do not allow an unsigned extra library or configuration alongside the seal.
    for prefix in ['etc/smartdesktopmimi','usr/lib/smartdesktopmimi/factory']:
        actual=inventory(rootfs/prefix)
        expected={p[len(prefix)+1:]:v for p,v in seal['files'].items() if p.startswith(prefix+'/')}
        if actual!=expected:raise ValueError('final rootfs has unexpected baseline files: '+prefix)
    for source,target in [('aitvbox.init','etc/init.d/aitvbox'),('start-ui.sh','usr/bin/aitvbox-start-ui')]:
        path=regular(target)
        if digest(path)!=digest(ROOT/'packaging/aitvbox-suite/files'/source) or path.stat().st_mode&0o777!=0o755:
            raise ValueError('final rootfs has stale desktop launcher: '+target)
    guard=regular('lib/preinit/78_mimi_storage_guard')
    if digest(guard)!=digest(ROOT/'packaging/aitvbox-mimi-update/files/78_mimi_storage_guard') or guard.stat().st_mode&0o777!=0o755:
        raise ValueError('final rootfs has stale storage preservation guard')
    # OpenWrt sorts rc.d lexically: S100 would run before S98 and S99.
    links={'S98mimi-update':'mimi-update','S99aitvbox':'aitvbox','S99mimi-desktop':'mimi-desktop'}
    rc=rootfs/'etc/rc.d'
    if rc.is_symlink() or not rc.is_dir() or rc.stat().st_mode&0o022:
        raise ValueError('final rootfs has unsafe or missing rc.d')
    for name,service in links.items():
        path=rc/name
        if not path.is_symlink():raise ValueError('final rootfs missing enabled service: '+name)
        target=os.readlink(path)
        resolved=posixpath.normpath(target if target.startswith('/') else '/etc/rc.d/'+target)
        if resolved!='/etc/init.d/'+service:
            raise ValueError('final rootfs wrong service target: '+name)
    for path in rc.iterdir():
        if path.name.startswith('S') and any(path.name.endswith(service) for service in links.values()) and path.name not in links:
            raise ValueError('final rootfs stale service ordering: '+path.name)
    if (rootfs/'overlay/smartdesktopmimi-platform/enrollment.json').exists():
        raise ValueError('unit-specific enrollment must not be baked into final rootfs')
    if image is not None:
        verify_image_owners(image,list(seal['files'])+['etc/init.d/aitvbox','usr/bin/aitvbox-start-ui']+
                            ['etc/rc.d/'+name for name in links]+['lib/preinit/78_mimi_storage_guard'])
    return seal

def prepare(args):
    if args.output.exists():raise ValueError('output already exists')
    edition=getattr(args,'product_edition',None)
    if edition is not None and edition not in {'public','private'}:raise ValueError('unknown product edition')
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
            declared=system.get('product_edition','')
            if declared and declared != edition:
                raise ValueError('system descriptor requires matching explicit --product-edition')
            if edition is not None:system['product_edition']=edition
            write(config/'system-device.json',system)
            updater['system_repository']=updater['repository']
            updater['system_device_file']='/etc/smartdesktopmimi/system-device.json'
        if edition is not None:
            marker=config/'product-edition';marker.write_text(edition+'\n');marker.chmod(0o444)
        write(config/'updater.json',updater)
        binary=root/'usr/bin/mimi-update';binary.parent.mkdir(parents=True);shutil.copyfile(args.updater,binary);binary.chmod(0o755)
        for source,target in [('mimi-update.init','etc/init.d/mimi-update'),('mimi-desktop.init','etc/init.d/mimi-desktop'),('mimi-platform-provider','usr/libexec/mimi-platform-provider'),('78_mimi_storage_guard','lib/preinit/78_mimi_storage_guard')]:
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
    parser.add_argument('operation',choices=['prepare','verify','verify-rootfs'])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--rootfs',type=Path,help='extracted final filesystem, read-only verification')
    parser.add_argument('--image',type=Path,help='original ext4/SquashFS image for owner verification')
    for name in ['device','trust','application','updater','checker']:parser.add_argument('--'+name,type=Path)
    parser.add_argument('--product-edition',choices=['public','private'],help='explicit product selection on build host only')
    parser.add_argument('--system-device',type=Path,help='controlled A133 system descriptor with measured partition geometry')
    args=parser.parse_args();args.output=args.output.resolve()
    if args.operation=='prepare':
        for name in ['device','trust','application','updater','checker']:
            value=getattr(args,name)
            if value is None:parser.error('prepare requires --'+name)
            setattr(args,name,value.resolve())
    if args.operation=='verify-rootfs' and (args.rootfs is None or args.image is None):parser.error('verify-rootfs requires --rootfs and --image')
    try:
        result=prepare(args) if args.operation=='prepare' else verify_rootfs(args.output,args.rootfs,args.image,args.product_edition) if args.operation=='verify-rootfs' else verify(args.output,args.product_edition)
    except (ValueError,KeyError,OSError,subprocess.CalledProcessError) as error:parser.exit(1,'BASELINE BLOCKED: '+str(error)+'\n')
    if args.operation!='prepare' and args.updater is not None:
        try:verify_updater(args.output,args.updater.absolute())
        except (ValueError,OSError) as error:parser.exit(1,'BASELINE BLOCKED: '+str(error)+'\n')
    print('PASS signed factory baseline '+result['factory_version']+'; physical enrollment and acceptance still required')
if __name__=='__main__':main()
