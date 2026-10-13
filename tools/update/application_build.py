"""Build receipts bind the desktop binary to pinned SDK and interface inputs.

Receipts are build evidence, not signatures or physical acceptance. The signed
application manifest covers the receipt when the application tree is packed.
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import source_ownership


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(1<<20),b''):h.update(chunk)
    return h.hexdigest()


def load(path):
    def unique(items):
        result={}
        for key,value in items:
            if key in result:raise ValueError('duplicate JSON key: '+key)
            result[key]=value
        return result
    return json.loads(path.read_text(),object_pairs_hook=unique)


def contract(root):
    path=root/'docs/update/platform-contract-lock.json'
    record=load(path)
    if record.get('schema_version')!=1 or not record.get('contract_id') or not record.get('headers'):
        raise ValueError('invalid interface lock')
    for name,want in record['headers'].items():
        source=root/name
        if Path(name).is_absolute() or '..' in Path(name).parts or source.is_symlink() or digest(source)!=want:
            raise ValueError('interface input differs from lock: '+name)
    return dict(id=record['contract_id'],sha256=digest(path))


def source_state(root):
    commit=subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()
    status=subprocess.check_output(['git','-C',str(root),'status','--porcelain=v1','--untracked-files=normal'])
    return dict(commit=commit,dirty=bool(status.strip()))


def create_receipt(root, sdk, sdk_hash, build_dir, before_source, before_contract):
    if source_state(root)!=before_source or contract(root)!=before_contract:
        raise ValueError('source identity or interface changed during build')
    # Record the executable and actual compiler command inventory, not merely
    # a successful command exit status. Neither replaces source distribution.
    binary=build_dir/'bin/lvglsim'
    commands=load(build_dir/'compile_commands.json')
    if not commands:raise ValueError('empty application compilation inventory')
    for entry in commands:
        name=Path(entry['file']).as_posix()
        if '/src/backend/' in name or name.endswith('/backend_main.c'):
            raise ValueError('platform backend entered application-only build')
    ownership = source_ownership.audit_commands(root, commands)
    record=dict(schema_version=1,kind='application-build',platform_id=sdk['platform_id'],
                target_os=sdk.get('target_os','tina'),
                architecture=sdk['architecture'],system_baseline=sdk['system_baseline'],
                sdk_lock_sha256=sdk_hash,source=before_source,interface=before_contract,
                build=dict(target='lvglsim',application_only=True,cloud_provider=False,
                           compile_commands_sha256=digest(build_dir/'compile_commands.json'), **ownership),
                desktop_sha256=digest(binary))
    cache=(build_dir/'CMakeCache.txt').read_text()
    display=re.search(r'^MIMI_DISPLAY_PROFILES:STRING=(.+)$',cache,re.MULTILINE)
    record['build']['display_profiles']=display.group(1).split(',') if display else ['1024x768']
    voice=re.search(r'^MIMI_ENABLE_VOICE_CONTROL:BOOL=(ON|OFF)$',cache,re.MULTILINE)
    record['build']['voice_control']=bool(voice and voice.group(1)=='ON')
    destination=build_dir/'application-build.json'
    temporary=build_dir/'.application-build.json.tmp'
    temporary.write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    temporary.replace(destination)
    return record


def verify_receipt(receipt, desktop, root):
    if receipt.is_symlink() or not receipt.is_file():raise ValueError('build receipt must be a regular file')
    r=load(receipt)
    if r.get('schema_version')!=1 or r.get('kind')!='application-build':raise ValueError('unknown application build receipt')
    if r.get('platform_id')!='a133' or r.get('architecture')!='aarch64':raise ValueError('unqualified application build platform')
    for value in [r.get('sdk_lock_sha256',''),r.get('desktop_sha256',''),r.get('build',{}).get('compile_commands_sha256','')]:
        if not re.fullmatch('[a-f0-9]{64}',value):raise ValueError('missing build input/output hash')
    if not re.fullmatch('[a-f0-9]{40}',r.get('source',{}).get('commit','')) or type(r['source'].get('dirty')) is not bool:
        raise ValueError('invalid build source identity')
    if not r.get('system_baseline') or r['interface']!=contract(root):raise ValueError('build/interface baseline mismatch')
    if r['build'].get('target')!='lvglsim' or r['build'].get('application_only') is not True or r['build'].get('cloud_provider') is not False:
        raise ValueError('not an independent application build')
    source_ownership.verify(root)
    if r['build'].get('source_ownership_sha256') != digest(root/source_ownership.LOCK):
        raise ValueError('source ownership/build receipt mismatch')
    if desktop.is_symlink() or not desktop.is_file() or r['desktop_sha256']!=digest(desktop):raise ValueError('desktop differs from build receipt')
    return r
