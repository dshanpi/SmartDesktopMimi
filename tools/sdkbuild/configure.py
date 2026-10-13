#!/usr/bin/env python3
"""Developer-only SDK configuration wizard; never runs on the desktop UI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
from application_sdk import verify_sdk, sdk_target, compiler_environment

PLATFORMS = {'a133': 'integrated', 't527': 'pending', 'rk3576': 'pending'}
FORMATS = {'tina': 'mimi-application', 'debian': 'deb', 'ubuntu': 'deb'}


def resolutions(values):
    result = []
    for value in values:
        match = re.fullmatch(r'([1-9][0-9]{2,3})[xX×]([1-9][0-9]{2,3})', value.strip())
        if not match:
            raise ValueError('Resolution must be WIDTHxHEIGHT: ' + value)
        w, h = map(int, match.groups())
        if not (480 <= w <= 7680 and 480 <= h <= 4320):
            raise ValueError('Resolution outside the desktop layout bounds')
        item = {'width': w, 'height': h}
        if item in result:
            raise ValueError('Duplicate resolution')
        result.append(item)
    if not result or len(result) > 16:
        raise ValueError('Select between 1 and 16 screen configurations')
    return result


def validate(profile):
    required = {'schema_version','platform_id','target_os','package_format','architecture',
                'sdk_lock_sha256','system_baseline','screens','voice_control'}
    if set(profile) != required or profile['schema_version'] != 1:
        raise ValueError('Unknown or incomplete build profile')
    if PLATFORMS.get(profile['platform_id']) != 'integrated':
        raise ValueError('Platform requires its SDK, Provider and board qualification first')
    if profile['target_os'] not in FORMATS or profile['package_format'] != FORMATS[profile['target_os']]:
        raise ValueError('Package format does not match target OS')
    if profile['architecture'] != 'aarch64':
        raise ValueError('Unsupported architecture for the selected platform')
    if not re.fullmatch(r'[a-f0-9]{64}', profile['sdk_lock_sha256']):
        raise ValueError('SDK must have an externally pinned SHA256')
    if not isinstance(profile['system_baseline'],str) or not profile['system_baseline']:
        raise ValueError('SDK system baseline required')
    if type(profile['voice_control']) is not bool:
        raise ValueError('voice_control must be a boolean')
    if not isinstance(profile['screens'],list):
        raise ValueError('screens must be a list')
    values=[]
    for screen in profile['screens']:
        if not isinstance(screen,dict) or set(screen)!={'width','height'} or any(type(screen[k]) is not int for k in screen):
            raise ValueError('Invalid screen dimensions')
        values.append(f"{screen['width']}x{screen['height']}")
    resolutions(values)
    return profile


def check_sdk(profile, sdk, link_probe=True):
    validate(profile)
    sdk = sdk.resolve()
    record = verify_sdk(sdk, profile['sdk_lock_sha256'])
    # Legacy SDK schema 1 was exported exclusively from A133 Tina.
    target_os = record.get('target_os', 'tina')
    for field in ['platform_id','architecture','system_baseline']:
        if profile[field] != record[field]:
            raise ValueError('SDK/profile mismatch: ' + field)
    if profile['target_os'] != target_os:
        raise ValueError('SDK target OS does not match the package target; Tina SDK cannot produce a Debian delivery')
    required = ['alsa/asoundlib.h','json-c/json.h','curl/curl.h',
                'libevdev-1.0/libevdev/libevdev.h']
    include_roots = [sdk/'sysroot/usr/include', sdk/'sysroot/usr/include/aarch64-linux-gnu']
    missing = [p for p in required if not any((root/p).is_file() for root in include_roots)]
    prefix = sdk_target(record, sdk)['compiler_prefix']
    compiler = sdk/'toolchain/bin'/(prefix+'gcc')
    if not compiler.is_file(): missing.append('toolchain/bin/'+prefix+'gcc')
    if missing: raise ValueError('Missing SDK inputs: ' + ', '.join(missing))
    machine = subprocess.check_output([str(compiler),'-dumpmachine'],text=True,env=compiler_environment()).strip()
    machines = ('aarch64-openwrt-linux', 'aarch64-openwrt-linux-gnu') if target_os == 'tina' else ('aarch64-linux-gnu',)
    if machine not in machines:
        raise ValueError('Compiler target differs from locked SDK')
    if link_probe:
        with tempfile.TemporaryDirectory(prefix='mimi-dependency-check-') as temporary:
            p=Path(temporary)
            (p/'probe.c').write_text('#include <alsa/asoundlib.h>\n#include <curl/curl.h>\n#include <json-c/json.h>\n#include <libevdev/libevdev.h>\nint main(void){CURL*c=curl_easy_init();curl_easy_cleanup(c);struct libevdev *d=libevdev_new();libevdev_free(d);struct json_object *j=json_object_new_object();json_object_put(j);return snd_asoundlib_version()==0;}\n')
            sysroot=sdk/'sysroot'
            command=[str(compiler),f'--sysroot={sysroot}',f'-I{sysroot}/usr/include',f'-I{sysroot}/usr/include/aarch64-linux-gnu',f'-I{sysroot}/usr/include/libevdev-1.0',str(p/'probe.c'),f'-L{sysroot}/usr/lib/aarch64-linux-gnu',f'-L{sysroot}/usr/lib',f'-Wl,-rpath-link,{sysroot}/lib',f'-Wl,-rpath-link,{sysroot}/usr/lib',f'-Wl,-rpath-link,{sysroot}/lib/aarch64-linux-gnu',f'-Wl,-rpath-link,{sysroot}/usr/lib/aarch64-linux-gnu','-lasound','-ljson-c','-levdev','-lcurl','-o',str(p/'probe')]
            result=subprocess.run(command,env={**compiler_environment(),'STAGING_DIR':str(sdk)},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            if result.returncode: raise ValueError('Target dependency link failed:\n'+result.stdout[-4000:])
    return {'sdk_verified':True,'compiler':machine,'dependency_link':'passed' if link_probe else 'not_run',
            'physical_acceptance':'not_run','selected_screens':profile['screens']}


def configure(args):
    if args.interactive:
        args.sdk=Path(input('SDK directory: ').strip())
        args.sdk_sha256=input('Reviewed sdk-lock.json SHA256: ').strip()
        args.platform=input('Platform [a133; t527/rk3576 pending]: ').strip() or 'a133'
        args.target_os=input('Target OS [tina/debian/ubuntu]: ').strip() or 'tina'
        args.screen=(input('Screen list, comma separated [1024x768]: ').strip() or '1024x768').split(',')
    if not args.sdk or not args.sdk_sha256 or not args.screen:
        raise ValueError('SDK, reviewed SDK hash and screen list are required')
    record=verify_sdk(args.sdk.resolve(),args.sdk_sha256)
    p=validate({'schema_version':1,'platform_id':args.platform,'target_os':args.target_os,
       'package_format':FORMATS[args.target_os],'architecture':record['architecture'],
       'sdk_lock_sha256':args.sdk_sha256,'system_baseline':record['system_baseline'],
       'screens':resolutions(args.screen),'voice_control':args.voice_control})
    report=check_sdk(p,args.sdk)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    # A reviewed configuration is never silently replaced.
    with args.output.open('x') as f: json.dump(p,f,ensure_ascii=False,sort_keys=True,indent=2);f.write('\n')
    print(json.dumps(report,ensure_ascii=False))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    c=sub.add_parser('configure');c.add_argument('--interactive',action='store_true')
    c.add_argument('--sdk',type=Path);c.add_argument('--sdk-sha256')
    c.add_argument('--platform',choices=PLATFORMS,default='a133')
    c.add_argument('--target-os',choices=FORMATS,default='tina')
    c.add_argument('--screen',action='append');c.add_argument('--voice-control',action='store_true')
    c.add_argument('--output',required=True,type=Path)
    for operation in ['check','build']:
        q=sub.add_parser(operation);q.add_argument('--profile',type=Path,required=True);q.add_argument('--sdk',type=Path,required=True)
        if operation=='build':q.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    try:
        if a.command=='configure':configure(a);return
        profile=validate(json.loads(a.profile.read_text()))
        print(json.dumps(check_sdk(profile,a.sdk),ensure_ascii=False))
        if a.command=='build':
            env={**os.environ,'MIMI_ENABLE_VOICE_CONTROL':'ON' if profile['voice_control'] else 'OFF','MIMI_DISPLAY_PROFILES':','.join(f"{s['width']}x{s['height']}" for s in profile['screens'])}
            subprocess.run([sys.executable,str(ROOT/'tools/update/build_application.py'),'--sdk-root',str(a.sdk.resolve().parent),'--sdk-id',a.sdk.resolve().name,'--expected-sdk-sha256',profile['sdk_lock_sha256'],'--output',str(a.output.resolve())],env=env,check=True)
            (a.output/'build-profile.json').write_text(json.dumps(profile,sort_keys=True,indent=2)+'\n')
    except (ValueError,OSError,subprocess.SubprocessError,KeyError,TypeError) as e:p.exit(1,'BUILD CONFIGURATION BLOCKED: '+str(e)+'\n')

if __name__=='__main__':main()
