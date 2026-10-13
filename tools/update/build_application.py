#!/usr/bin/env python3
"""Build desktop and device Web UI from a clean checkout and a pinned app SDK.

Run on the controlled application builder. No Tina build, signing key, device
activation or upload is involved. Failed runs retain logs under BUILD-INCOMPLETE.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from application_build import source_state
from application_sdk import verify_sdk
from application_tree import stage
from baseline import digest

ROOT = Path(__file__).resolve().parents[2]


def selected_sdk(root, sdk_id):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}', sdk_id):
        raise ValueError('invalid SDK identifier')
    sdk = root / sdk_id
    if sdk.is_symlink() or not sdk.is_dir() or sdk.resolve().parent != root.resolve():
        raise ValueError('SDK is missing or outside the configured SDK root')
    return sdk.resolve()


def build(sdk, pin, output):
    source = source_state(ROOT)
    if source['dirty']:
        raise ValueError('release builder requires a clean source checkout')
    if output.exists() or output.is_symlink() or ROOT == output.resolve() or ROOT in output.resolve().parents:
        raise ValueError('build output must be a new directory outside the source checkout')
    verify_sdk(sdk, pin)
    output.mkdir(parents=True)
    incomplete = output / 'BUILD-INCOMPLETE'
    incomplete.write_text('Build not completed; do not package or publish.\n')
    epoch = subprocess.check_output(['git', '-C', str(ROOT), 'show', '-s', '--format=%ct', source['commit']], text=True).strip()
    env = dict(os.environ, SOURCE_DATE_EPOCH=epoch)
    def execute(name, command, cwd=ROOT):
        with (output/(name+'.log')).open('wb') as log:
            subprocess.run([str(p) for p in command], cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    execute('public-source-audit', [sys.executable, ROOT/'tools/public_release_audit.py', '--candidate'])
    execute('contracts', [sys.executable, ROOT/'tools/update/check_contracts.py'])
    execute('desktop', [sys.executable, ROOT/'tools/update/application_sdk.py', 'build', '--sdk', sdk,
                        '--expected-sdk-sha256', pin, '--build-dir', output/'desktop'])
    web = ROOT/'apps/ipkvm/upstream/ui'
    execute('web-dependencies', ['npm', 'ci'], web)
    execute('web-device', ['npm', 'run', 'build:device'], web)
    if source_state(ROOT) != source:
        raise ValueError('source changed during desktop/Web build')
    stage(output/'desktop/bin/lvglsim', web.parent/'static', output/'application-tree', output/'desktop/application-build.json')
    execute('sbom', [sys.executable, ROOT/'tools/generate_sbom.py', '--output', output/'sbom'])
    shutil.copyfile(sdk/'sdk-lock.json', output/'sdk-lock.json')
    shutil.copyfile(ROOT/'docs/update/platform-contract-lock.json', output/'platform-contract-lock.json')
    # Bind Web inputs and outputs too, so a stale directory cannot be silently
    # paired with a newly compiled desktop. These records are still unsigned.
    dependencies = {str(p.relative_to(ROOT)): digest(p) for p in [web/'package-lock.json', ROOT/'third_party/components.json']}
    record = dict(schema_version=1, kind='application-candidate-build', source=source,
                  source_date_epoch=int(epoch), sdk_lock_sha256=pin,
                  desktop_receipt_sha256=digest(output/'desktop/application-build.json'),
                  dependency_inputs=dependencies,
                  files={p.relative_to(output/'application-tree').as_posix():digest(p)
                         for p in sorted((output/'application-tree').rglob('*')) if p.is_file()},
                  signed=False, physical_acceptance='not_run', uploaded=False)
    verify_sdk(sdk, pin)
    if source_state(ROOT) != source:raise ValueError('source changed during staging')
    (output/'dependency-lock.json').write_text(json.dumps(dict(schema_version=1,source_commit=source['commit'],files=dependencies),sort_keys=True,indent=2)+'\n')
    (output/'candidate-build.json').write_text(json.dumps(record,sort_keys=True,indent=2)+'\n')
    incomplete.unlink()
    (output/'BUILD-COMPLETE').write_text('Unsigned application build only; signing and acceptance are required.\n')
    return record


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk-root',type=Path,required=True)
    p.add_argument('--sdk-id',required=True)
    p.add_argument('--expected-sdk-sha256',required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    try:result=build(selected_sdk(a.sdk_root,a.sdk_id),a.expected_sdk_sha256,a.output.absolute())
    except (ValueError,OSError,subprocess.CalledProcessError) as error:p.exit(1,'APPLICATION BUILD BLOCKED: '+str(error)+'\n')
    print(json.dumps(dict(source_commit=result['source']['commit'],signed=False,uploaded=False)))


if __name__=='__main__':main()
