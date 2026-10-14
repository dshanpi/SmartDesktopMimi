#!/usr/bin/env python3
"""Capture package-owned Ubuntu x86_64 build inputs; never install host packages.

This private development SDK is not a qualified platform or redistribution grant.
The build host compiler runtime is pinned separately from the target sysroot.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
from application_sdk import inventory, digest, verify_sdk

ROOT_PACKAGES=('gcc-9','g++-9','binutils-x86-64-linux-gnu','libc6-dev',
               'linux-libc-dev','libsdl2-dev','libsdl2-image-dev',
               'libasound2-dev','libjson-c-dev','libcurl4-openssl-dev','base-files')

def installed_inputs():
    raw=subprocess.check_output(['dpkg-query','-W','-f=${binary:Package}\t${Version}\t${db:Status-Status}\t${Depends}\t${Pre-Depends}\t${Provides}\n'],text=True)
    packages={};providers={}
    for line in raw.splitlines():
        name,version,status,depends,pre,provides=line.split('\t')
        if status=='installed':
            packages[name.split(':')[0]]=(name,version,','.join([depends,pre]))
            for item in provides.split(','):
                virtual=re.split(r'[:\s(]',item.strip())[0]
                if virtual:providers.setdefault(virtual,[]).append(name.split(':')[0])
    chosen={};pending=list(ROOT_PACKAGES)
    while pending:
        name=pending.pop()
        if name in chosen:continue
        if name not in packages:raise ValueError('Missing installed development package: '+name)
        full,version,depends=packages[name];chosen[name]={'package':full,'version':version}
        for group in depends.split(','):
            if not group.strip():continue
            alternatives=[re.split(r'[:\s(]',part.strip())[0] for part in group.split('|')]
            selected=next((p if p in packages else sorted(providers[p])[0] for p in alternatives if p in packages or p in providers),None)
            if selected is None:raise ValueError('Unresolved installed package dependency: '+group)
            pending.append(selected)
    paths=set()
    for record in chosen.values():
        for name in subprocess.check_output(['dpkg-query','-L',record['package']],text=True).splitlines():
            p=Path(name)
            if not p.is_absolute():continue
            # No host configuration, command history, state, credentials or home.
            allowed=(name.startswith(('/usr/include/','/usr/lib/gcc/x86_64-linux-gnu/9/')) or name=='/usr/lib/os-release' or
                     (name.startswith(('/usr/lib/','/lib/','/usr/share/pkgconfig/')) and re.search(r'(\.so(?:\.[0-9]+)*|\.a|\.o|\.pc)$',name)))
            if allowed and not p.is_dir() and (p.is_file() or p.is_symlink()):
                paths.add(p)
    return chosen,paths

def copy_input(source,dest):
    dest.parent.mkdir(parents=True,exist_ok=True)
    if source.is_symlink():
        dest.symlink_to(os.readlink(source))
    elif source.is_file():shutil.copy2(source,dest)
    else:raise ValueError('Missing/special build input: '+str(source))

def capture(destination,baseline,dry_run=False):
    if platform.system()!='Linux' or platform.machine()!='x86_64':raise ValueError('Requires Ubuntu x86_64 host')
    os_release=Path('/usr/lib/os-release').read_text()
    if not re.search(r'^ID=ubuntu$',os_release,re.M):raise ValueError('Requires Ubuntu host')
    version=re.search(r'^VERSION_ID="?([0-9.]+)"?$',os_release,re.M).group(1)
    if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9._-]{0,191}',baseline):raise ValueError('Invalid baseline ID')
    packages,paths=installed_inputs()
    tools={name:Path(shutil.which('x86_64-linux-gnu-'+name) or '/missing').resolve()
           for name in ('gcc','g++','ar','ranlib','strip','as','ld')}
    if subprocess.check_output([str(tools['gcc']),'-dumpfullversion'],text=True).strip().split('.')[0]!='9':
        raise ValueError('This capture recipe pins GCC9; add and qualify a new recipe for other compiler versions')
    runtime={}
    for program in list(tools.values())+[Path('/usr/lib/gcc/x86_64-linux-gnu/9/cc1'),Path('/usr/lib/gcc/x86_64-linux-gnu/9/cc1plus'),Path('/usr/lib/gcc/x86_64-linux-gnu/9/collect2')]:
        linked=subprocess.check_output(['ldd',str(program)],text=True)
        if 'not found' in linked:raise ValueError('Missing compiler host runtime')
        for line in linked.splitlines():
            parts=line.split();path=parts[2] if len(parts)>2 and parts[1]=='=>' else parts[0]
            if path.startswith('/'):runtime[path]=digest(Path(path))
    report=dict(packages=len(packages),files=len(paths),source_bytes=sum(p.stat().st_size for p in paths if not p.is_symlink()),host_runtime=runtime)
    if dry_run:return report
    if destination.exists() or destination.is_symlink():raise ValueError('Destination already exists')
    if subprocess.check_output(['git','status','--porcelain'],cwd=ROOT,text=True).strip():raise ValueError('Commit capture recipe first')
    source=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.ubuntu-sdk-',dir=destination.parent) as temporary:
        stage=Path(temporary);origin={}
        for path in sorted(paths):
            dest=stage/'sysroot'/str(path).lstrip('/');copy_input(path,dest);origin[dest]=path
            if str(path).startswith('/usr/lib/gcc/x86_64-linux-gnu/9/') or str(path).startswith('/usr/include/c++/9/') or str(path).startswith('/usr/include/x86_64-linux-gnu/c++/9/'):
                dest=stage/'toolchain'/str(path)[5:]
                copy_input(path,dest);origin[dest]=path
        for name,path in tools.items():
            dest=stage/'toolchain/bin'/('x86_64-linux-gnu-'+name);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,dest);origin[dest]=path
        # Resolve host absolute/relative links into captured SDK inputs only.
        # A missing dependency is an error, never a host-library fallback.
        resolved={}
        for dest,path in origin.items():
            if not dest.is_symlink():resolved.setdefault(str(path.resolve(strict=True)),[]).append(dest)
        for dest,path in origin.items():
            if not dest.is_symlink():continue
            actual=path.resolve(strict=True)
            candidates=resolved.get(str(actual),[])
            if not candidates:raise ValueError('Captured input link target missing: '+str(path)+' -> '+str(actual))
            kind='toolchain' if str(dest).startswith(str(stage/'toolchain')+'/') else 'sysroot'
            target=next((p for p in candidates if str(p).startswith(str(stage/kind)+'/')),candidates[0])
            dest.unlink();dest.symlink_to(os.path.relpath(target,dest.parent))
        record=dict(schema_version=2,platform_id='ubuntu-host',architecture='x86_64',target_os='ubuntu',target_os_version=version,
                    compiler_prefix='x86_64-linux-gnu-',system_baseline=baseline,files=inventory(stage),
                    capture=dict(recipe_commit=source,kind='installed-package-input-snapshot',packages=packages,host_runtime=runtime,
                                 redistribution_audit='not_run',platform_acceptance='not_run'))
        # GCC's executable itself and subprocess runtime are host-side inputs.
        # They are checked at build time, not confused with target runtime.
        (stage/'sdk-lock.json').write_text(json.dumps(record,sort_keys=True,indent=2)+'\n')
        verify_sdk(stage)
        packages_after,_=installed_inputs()
        if packages_after!=packages:raise ValueError('Installed package state changed during capture')
        os.rename(stage,destination)
    return dict(**report,sdk_lock_sha256=digest(destination/'sdk-lock.json'),sdk=str(destination),platform_acceptance='not_run')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',required=True);p.add_argument('--dry-run',action='store_true');a=p.parse_args()
    try:print(json.dumps(capture(a.output.absolute(),a.baseline,a.dry_run),indent=2))
    except (OSError,ValueError,subprocess.SubprocessError) as e:p.exit(1,'SDK CAPTURE BLOCKED: '+str(e)+'\n')

if __name__=='__main__':main()
