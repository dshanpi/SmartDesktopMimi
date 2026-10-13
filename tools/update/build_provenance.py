"""Check cross-references between hashed build records and signed manifests."""
import hashlib
import json
from release_identity import HARDWARE_REPOSITORY


def inventory_digest(files, prefix):
    selected={k:v for k,v in files.items() if k.startswith(prefix+'/')}
    if not selected:raise ValueError('SDK inventory missing '+prefix)
    return hashlib.sha256(json.dumps(selected,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def application(directory, lock, manifest, stage, load):
    assets=lock['assets']
    required={'application-build.json','sdk-lock.json','dependency-lock.json'}
    if not required<=assets.keys():raise ValueError('missing application build provenance assets')
    receipt=load(directory/'application-build.json');sdk=load(directory/'sdk-lock.json')
    if receipt.get('schema_version')!=1 or receipt.get('kind')!='application-build' or sdk.get('schema_version')!=1:
        raise ValueError('unknown build/SDK provenance schema')
    if receipt['source']['commit']!=lock['source_commit'] or type(receipt['source'].get('dirty')) is not bool:
        raise ValueError('build/source commit mismatch')
    if stage!='draft' and receipt['source']['dirty']:
        raise ValueError('dirty build cannot be published')
    if receipt['sdk_lock_sha256']!=assets['sdk-lock.json'] or lock.get('sdk_lock_sha256')!=assets['sdk-lock.json']:
        raise ValueError('build/SDK lock mismatch')
    if not sdk.get('system_baseline') or receipt['system_baseline']!=sdk['system_baseline']:
        raise ValueError('SDK/build system baseline mismatch')
    if receipt['interface']!={'id':lock['ipc_contract'],'sha256':assets['platform-contract-lock.json']}:
        raise ValueError('build/interface provenance mismatch')
    if receipt['build'].get('target')!='lvglsim' or receipt['build'].get('application_only') is not True or receipt['build'].get('cloud_provider') is not False:
        raise ValueError('build is not application-only')
    for target in manifest['targets']:
        for field in ['platform_id','architecture']:
            if target['identity'][field]!=sdk[field] or receipt[field]!=sdk[field]:
                raise ValueError('SDK/build/package target mismatch')
    files=manifest['files']
    if files['bin/lvglsim']['sha256']!=receipt['desktop_sha256'] or files['resources/application-build.json']['sha256']!=assets['application-build.json']:
        raise ValueError('signed payload/build receipt mismatch')
    if lock['toolchain_sha256']!=inventory_digest(sdk['files'],'toolchain') or lock['sysroot_sha256']!=inventory_digest(sdk['files'],'sysroot'):
        raise ValueError('SDK inventory/build lock mismatch')
    if lock['dependency_lock_sha256']!=assets['dependency-lock.json']:
        raise ValueError('dependency lock mismatch')


def system(directory, lock, manifest, factory, load):
    assets=lock['assets']
    if lock.get('platform_id')!='a133':raise ValueError('system SDK provenance registry currently qualifies A133 only')
    if 'system-build.json' not in assets:raise ValueError('missing system build provenance')
    record=load(directory/'system-build.json')
    if record.get('schema_version')!=1 or record.get('kind')!='system-build':raise ValueError('unknown system build record')
    if lock.get('hardware_repository')!=HARDWARE_REPOSITORY or record.get('hardware_repository')!=HARDWARE_REPOSITORY:
        raise ValueError('A133 private SDK repository mismatch')
    if manifest.get('build_provenance_sha256')!=assets['system-build.json']:
        raise ValueError('signed system/build provenance mismatch')
    for name in ['source_commit','hardware_commit','platform_id','toolchain_sha256','sysroot_sha256','dependency_lock_sha256']:
        if record.get(name)!=lock.get(name) or not record.get(name):raise ValueError('system build lock mismatch: '+name)
    if not record.get('sdk_baseline') or not lock.get('sdk_baseline') or record['sdk_baseline']!=lock['sdk_baseline']:
        raise ValueError('missing/mismatched full SDK baseline')
    if record.get('sdk_components_sha256')!=assets['sdk-components.json']:
        raise ValueError('full SDK component inventory mismatch')
    components=load(directory/'sdk-components.json')
    if components.get('schema_version')!=1 or components.get('sdk_baseline')!=record['sdk_baseline'] or not components.get('components'):
        raise ValueError('missing full SDK component records')
    from re import fullmatch
    from pathlib import PurePosixPath
    if not fullmatch('[a-f0-9]{64}',components.get('manifest_sha256','')):
        raise ValueError('missing pinned SDK manifest hash')
    seen=set()
    for component in components['components']:
        name=component['path'];path=PurePosixPath(name)
        if not name or name in seen or path.is_absolute() or '..' in path.parts or str(path)!=name:
            raise ValueError('invalid/duplicate SDK component path')
        seen.add(name)
        if not fullmatch('[a-f0-9]{40}',component.get('commit','')) or not fullmatch('[a-f0-9]{64}',component.get('local_inputs_sha256','')):
            raise ValueError('unlocked SDK component inputs')
    # Public records contain only hashes, never private patches or vendor trees.
    if not fullmatch('[a-f0-9]{64}',record.get('sdk_local_inputs_sha256','')) or record['sdk_local_inputs_sha256']!=lock.get('sdk_local_inputs_sha256'):
        raise ValueError('missing private SDK local-input snapshot hash')
    if record.get('image_sha256')!=assets['system.img.gz'] or record.get('partition_sha256')!=lock['partition_sha256']:
        raise ValueError('system build image/partition mismatch')
    if record.get('factory_application')!={'commit':factory['commit'],'version':factory['version'],'sha256':factory['package_sha256']}:
        raise ValueError('system build factory application mismatch')
