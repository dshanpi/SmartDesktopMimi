"""Synthetic metadata for host tests only, never a deployable release."""
import hashlib
import json
from build_provenance import inventory_digest
from release_identity import HARDWARE_REPOSITORY

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,value):path.write_text(json.dumps(value,sort_keys=True))

def application(directory, manifest, lock):
    identity=manifest['targets'][0]['identity']
    identity.setdefault('architecture','aarch64')
    sdk=dict(schema_version=1,platform_id=identity['platform_id'],architecture=identity['architecture'],system_baseline='host-fixture-only',
             files={'toolchain/bin/compiler':{'sha256':'a'*64,'mode':493},'sysroot/include/fixture.h':{'sha256':'b'*64,'mode':420}})
    write(directory/'sdk-lock.json',sdk);write(directory/'dependency-lock.json',{'host_fixture':True})
    files=manifest.setdefault('files',{})
    files.setdefault('bin/lvglsim',{'sha256':'c'*64,'size':1,'mode':493})
    receipt=dict(schema_version=1,kind='application-build',platform_id=sdk['platform_id'],architecture=sdk['architecture'],system_baseline=sdk['system_baseline'],
                 sdk_lock_sha256=sha(directory/'sdk-lock.json'),source=dict(commit=lock['source_commit'],dirty=False),
                 interface=dict(id=lock['ipc_contract'],sha256=sha(directory/'platform-contract-lock.json')),
                 build=dict(target='lvglsim',application_only=True,cloud_provider=False,compile_commands_sha256='d'*64),desktop_sha256=files['bin/lvglsim']['sha256'])
    write(directory/'application-build.json',receipt)
    files['resources/application-build.json']=dict(sha256=sha(directory/'application-build.json'),size=(directory/'application-build.json').stat().st_size,mode=420)
    lock.update(sdk_lock_sha256=receipt['sdk_lock_sha256'],toolchain_sha256=inventory_digest(sdk['files'],'toolchain'),
                sysroot_sha256=inventory_digest(sdk['files'],'sysroot'),dependency_lock_sha256=sha(directory/'dependency-lock.json'))
    return receipt

def system(directory, manifest, lock, factory):
    lock.update(hardware_repository=HARDWARE_REPOSITORY,sdk_baseline='host-sdk-only',sdk_local_inputs_sha256='f'*64)
    write(directory/'sdk-components.json',dict(schema_version=1,sdk_baseline='host-sdk-only',manifest_sha256='e'*64,
        components=[dict(path='host/component',commit='a'*40,local_inputs_sha256='b'*64)]))
    manifest['sdk_components_sha256']=sha(directory/'sdk-components.json')
    record={key:lock[key] for key in ['source_commit','hardware_commit','platform_id','hardware_repository','sdk_baseline','sdk_local_inputs_sha256','partition_sha256','toolchain_sha256','sysroot_sha256','dependency_lock_sha256']}
    record.update(schema_version=1,kind='system-build',image_sha256=sha(directory/'system.img.gz'),sdk_components_sha256=sha(directory/'sdk-components.json'),
                  factory_application=dict(commit=factory['commit'],version=factory['version'],sha256=factory['package_sha256']))
    write(directory/'system-build.json',record)
    manifest['build_provenance_sha256']=sha(directory/'system-build.json')


def complete_synthetic_acceptance(directory):
    """Synthetic gate data ONLY, never real-device evidence or release inputs."""
    lock=json.loads((directory/'release-lock.json').read_text())
    prefixes=[''] if lock['schema_version']==1 else [i+'--' for i in lock['packages']]
    for prefix in prefixes:
        report=json.loads((directory/(prefix+'validation.json')).read_text())
        for target in report['targets']:
            for evidence in target['checks'].values():
                evidence.update(result='passed',evidence='host.log')
                evidence.pop('reason',None)
        write(directory/(prefix+'validation.json'),report)
        if prefix:
            child=json.loads((directory/(prefix+'release-lock.json')).read_text())
            child['assets']['validation.json']=sha(directory/(prefix+'validation.json'))
            write(directory/(prefix+'release-lock.json'),child)
    lock['assets']={name:sha(directory/name) for name in lock['assets']}
    write(directory/'release-lock.json',lock)
