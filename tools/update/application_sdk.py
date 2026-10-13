#!/usr/bin/env python3
"""Export/verify a relocatable A133 application SDK and build only the desktop."""
import argparse
import hashlib
import json
import os
import re
import stat
import shlex
from application_build import load, source_state, contract, create_receipt
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]


def digest(path):
    with path.open('rb') as file:
        result=hashlib.sha256()
        for chunk in iter(lambda: file.read(1024*1024), b''):
            result.update(chunk)
        return result.hexdigest()


def inventory(root):
    files={}
    for top in ['toolchain','sysroot']:
        if (root/top).is_symlink() or not (root/top).is_dir():
            raise ValueError('SDK requires real toolchain and sysroot directories')
        for path in sorted((root/top).rglob('*')):
            name=path.relative_to(root).as_posix()
            if path.is_symlink():
                link=os.readlink(path)
                try:
                    path.resolve().relative_to(root.resolve())
                except (ValueError, RuntimeError):
                    raise ValueError('SDK link escapes its portable root or loops: '+name)
                if os.path.isabs(link):
                    raise ValueError('SDK absolute link is not portable: '+name)
                files[name]={'link':link}
            elif path.is_file():
                files[name]={'sha256':digest(path),'mode':path.stat().st_mode & 0o777}
            elif not path.is_dir():
                raise ValueError('SDK contains a special file: '+name)
    if not files:raise ValueError('empty SDK')
    return files


def sdk_target(record, sdk):
    """Validate compiler and OS provenance without executing target code."""
    if record.get('schema_version') == 1:
        if record.get('target_os', 'tina') != 'tina':
            raise ValueError('legacy SDK target OS is Tina only; use schema 2')
        return {'target_os': 'tina', 'compiler_prefix': 'aarch64-openwrt-linux-'}
    if record.get('schema_version') != 2 or record.get('target_os') not in ('debian', 'ubuntu'):
        raise ValueError('unsupported SDK target/schema')
    if record.get('compiler_prefix') != 'aarch64-linux-gnu-':
        raise ValueError('unsupported Debian compiler prefix')
    version = record.get('target_os_version', '')
    if not isinstance(version, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+){0,2}', version):
        raise ValueError('SDK target OS version is required')
    release = sdk/'sysroot/usr/lib/os-release'
    if release.is_symlink() or not release.is_file():
        raise ValueError('SDK requires its target usr/lib/os-release')
    fields = {}
    for line in release.read_text().splitlines():
        if not line or line.lstrip().startswith('#'): continue
        key, sep, value = line.partition('=')
        if not sep or key in fields: raise ValueError('invalid or duplicate target OS field')
        values = shlex.split(value, comments=False)
        if len(values) != 1: raise ValueError('invalid target OS value')
        fields[key] = values[0]
    if fields.get('ID') != record['target_os'] or fields.get('VERSION_ID') != version:
        raise ValueError('SDK target OS differs from sysroot os-release')
    for name in ('gcc', 'g++', 'ar', 'ranlib', 'strip'):
        tool = sdk/'toolchain/bin'/('aarch64-linux-gnu-' + name)
        if not tool.is_file(): raise ValueError('SDK compiler tool missing: ' + name)
    return {key: record[key] for key in ('target_os', 'target_os_version', 'compiler_prefix')}


def verify_sdk(sdk, expected=None):
    lock=sdk/'sdk-lock.json'
    if lock.is_symlink() or not lock.is_file():raise ValueError('SDK lock must be a regular file')
    if expected is not None and (not re.fullmatch('[a-f0-9]{64}',expected) or digest(lock)!=expected):
        raise ValueError('SDK lock does not match externally pinned SHA256')
    record=load(lock)
    if record.get('schema_version') not in (1,2) or record.get('platform_id')!='a133' or record.get('architecture')!='aarch64':
        raise ValueError('unsupported SDK target/schema')
    if not isinstance(record.get('system_baseline'),str) or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9._-]{0,191}',record['system_baseline']):
        raise ValueError('missing/invalid SDK system baseline')
    if inventory(sdk)!=record['files']:raise ValueError('SDK input differs from its lock')
    sdk_target(record, sdk)
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('operation',choices=['export','verify','build'])
    p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--toolchain',type=Path)
    p.add_argument('--sysroot',type=Path)
    p.add_argument('--baseline')
    p.add_argument('--target-os',choices=['tina','debian','ubuntu'],default='tina')
    p.add_argument('--target-os-version',help='Required for Debian/Ubuntu; must match target sysroot os-release')
    p.add_argument('--expected-sdk-sha256',help='externally pinned sdk-lock.json hash; required for build')
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/application-only')
    a=p.parse_args();sdk=a.sdk.resolve()
    if a.operation=='build' and not a.expected_sdk_sha256:p.error('build requires --expected-sdk-sha256 from the selected SDK version')
    if a.operation=='export':
        if not a.toolchain or not a.sysroot or not a.baseline:p.error('export requires --toolchain, --sysroot and --baseline')
        if sdk.exists():p.error('SDK destination already exists')
        sdk.parent.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.sdk-',dir=sdk.parent) as temporary:
            staging=Path(temporary)
            shutil.copytree(a.toolchain,staging/'toolchain',symlinks=True)
            # The vendor toolchain contains a self-referential lib/lib link.
            # It has no resolvable content and is not a compiler input.
            loop=staging/'toolchain/lib/lib'
            excluded=[]
            if loop.is_symlink() and os.readlink(loop)=='lib':
                loop.unlink();excluded.append('toolchain/lib/lib: vendor self-referential link')
            (staging/'sysroot').mkdir()
            # Export build inputs only: never clone the assembled rootfs, /etc,
            # host tools, factory licenses, network settings or credentials.
            for relative in ['usr/include','usr/lib','lib','usr/share/pkgconfig']:
                source=a.sysroot/relative
                if source.exists():
                    destination=staging/'sysroot'/relative
                    destination.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copytree(source,destination,symlinks=True)
            record={'schema_version':1,'platform_id':'a133','architecture':'aarch64','system_baseline':a.baseline,'excluded':excluded,'files':inventory(staging)}
            if a.target_os != 'tina':
                record.update(schema_version=2,target_os=a.target_os,target_os_version=a.target_os_version,
                              compiler_prefix='aarch64-linux-gnu-')
            sdk_target(record, staging)
            (staging/'sdk-lock.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
            os.rename(staging,sdk)
    record=verify_sdk(sdk,a.expected_sdk_sha256)
    print('SDK SHA256',digest(sdk/'sdk-lock.json'))
    if a.operation=='build':
        a.build_dir=a.build_dir.resolve()
        before_source=source_state(ROOT)
        before_contract=contract(ROOT)
        # A failed rebuild must not leave an earlier successful receipt usable.
        (a.build_dir/'application-build.json').unlink(missing_ok=True)
        target=sdk_target(record,sdk)
        env={**os.environ,'MIMI_SDK_ROOT':str(sdk),'STAGING_DIR':str(sdk),
             'MIMI_COMPILER_PREFIX':target['compiler_prefix']}
        screens=os.environ.get('MIMI_DISPLAY_PROFILES','1024x768')
        if not re.fullmatch(r'[1-9][0-9]{2,3}x[1-9][0-9]{2,3}(,[1-9][0-9]{2,3}x[1-9][0-9]{2,3}){0,15}',screens):
            p.error('invalid build-time display profile list')
        voice=os.environ.get('MIMI_ENABLE_VOICE_CONTROL','OFF')
        if voice not in ('ON','OFF'):p.error('voice option must be ON or OFF')
        cache=a.build_dir/'CMakeCache.txt'
        if cache.exists() and ('MIMI_SDK_ROOT:PATH='+str(sdk)) not in cache.read_text():
            p.error('build directory belongs to a different SDK; select a fresh --build-dir')
        # Reconfigure existing caches too, so an older SDK build cannot retain
        # the backend target or its private source/library requirements.
        subprocess.run(['cmake','-S',str(ROOT/'apps/lv_port_linux'),'-B',str(a.build_dir),
                        '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/application-sdk.cmake'),
                        '-DAITVBOX_ENABLE_100ASK_CLOUD=OFF','-DAITVBOX_PLATFORM=a133',
                        '-DMIMI_APPLICATION_ONLY=ON','-DMIMI_DISPLAY_PROFILES='+screens,'-DMIMI_ENABLE_VOICE_CONTROL='+voice],env=env,check=True)
        subprocess.run(['cmake','--build',str(a.build_dir),'--target','lvglsim','-j4'],env=env,check=True)
        verify_sdk(sdk,a.expected_sdk_sha256)
        receipt=create_receipt(ROOT,record,a.expected_sdk_sha256,a.build_dir,before_source,before_contract)
        print('BUILD RECEIPT',a.build_dir/'application-build.json','desktop',receipt['desktop_sha256'])


if __name__=='__main__':main()
