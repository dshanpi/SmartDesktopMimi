"""Signed host fixtures only; these tests never certify a physical platform."""
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
import provenance_fixture
import release_gate as gate
import release_collection as collection
import promote_release as promotion
import publish_draft as publishing
import prepare_release as preparation
from github_release import GitHubRelease, verify_remote


class SignedCollectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='mimi-host-test-')
        cls.root=Path(cls.temp.name)
        cls.checker=cls.root/'checker'
        subprocess.run(['go','build','-o',str(cls.checker),'./cmd/mimi-update'],cwd=ROOT/'services/mimi-update',check=True)
        cls.key=cls.root/'host-fixture-key.pem'
        subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',str(cls.key)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        public=subprocess.check_output(['openssl','pkey','-in',str(cls.key),'-pubout']).decode()
        cls.trust=cls.root/'trust.json'
        cls.trust.write_text(json.dumps(dict(schema_version=1,keys={'host-test':public},revoked=[],min_sequence=0)))

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def fixture(self, path, platform):
        path.mkdir()
        payload=('host-fixture-only-'+platform).encode()
        with tarfile.open(path/'application.tar.gz','w:gz',format=tarfile.USTAR_FORMAT) as tar:
            info=tarfile.TarInfo('bin/lvglsim');info.size=len(payload);info.mode=0o755
            tar.addfile(info,io.BytesIO(payload))
        identity=dict(product_id='smartdesktopmimi',platform_id=platform,soc_id=platform,board_id='host-board',
                      hardware_revision='host-only-r1',variant='host',architecture='aarch64',isa='armv8-a',bits='64',
                      endian='little',abi='aapcs64',libc='glibc',loader='/lib/ld-linux-aarch64.so.1',ports_abi='1',ipc_contract='host-v1')
        runtime={k:'1.0.0' for k in ['system','provider','updater','libc']}
        device=dict(schema_version=1,identity=identity,runtime=runtime,capabilities=['desktop'],data_schema=1,
                    system_baseline='host-fixture',enrollment_id='not-a-real-device')
        manifest=dict(schema_version=1,policy=gate.POLICY,release_type='application',version='1.1.0-rc.1',commit='a'*40,
                      channel='testing',sequence=2,key_id='host-test',package_sha256=gate.sha(path/'application.tar.gz'),
                      package_bytes=(path/'application.tar.gz').stat().st_size,expanded_bytes=len(payload),max_files=1,
                      files={'bin/lvglsim':dict(sha256=hashlib.sha256(payload).hexdigest(),size=len(payload),mode=0o755)},
                      targets=[dict(identity=identity,runtime={k:dict(min='1.0.0',max_exclusive='2.0.0',exclude=[]) for k in runtime},capabilities=['desktop'])],
                      data=dict(min=1,max=1,write=1))
        (path/'manifest.json').write_text(json.dumps(manifest))
        subprocess.run(['openssl','dgst','-sha256','-sign',str(self.key),'-out',str(path/'manifest.sig'),str(path/'manifest.json')],check=True)
        (path/'device.json').write_text(json.dumps(device))
        (path/'source.tar.gz').write_bytes(b'host-test-source-not-for-release')
        (path/'sbom.spdx.json').write_text('{}')
        (path/'platform-contract-lock.json').write_text(json.dumps(dict(contract_id='host-v1')))
        (path/'host.log').write_text('Host fixture results only; no physical validation.')
        checks={k:dict(result='passed',method='host',evidence='host.log') for k in gate.AUTOMATED}
        checks.update({k:dict(result='not_run',method='physical-device',reason='No hardware in unit test') for k in gate.HARDWARE})
        report=dict(schema_version=1,source_commit='a'*40,artifact_sha256=manifest['package_sha256'],targets=[dict(device_file='device.json',checks=checks)])
        (path/'validation.json').write_text(json.dumps(report))
        lock=dict(schema_version=1,policy=gate.POLICY,release_type='application',source_commit='a'*40,version=manifest['version'],
                  tag='v'+manifest['version'],ipc_contract='host-v1',toolchain_sha256='b'*64,sysroot_sha256='c'*64,dependency_lock_sha256='d'*64,
                  assets={p.name:gate.sha(p) for p in path.iterdir()})
        provenance_fixture.application(path,manifest,lock)
        with tarfile.open(path/'application.tar.gz','w:gz',format=tarfile.USTAR_FORMAT) as tar:
            for name,data,mode in [('bin/lvglsim',payload,0o755),('resources/application-build.json',(path/'application-build.json').read_bytes(),0o644)]:
                info=tarfile.TarInfo(name);info.size=len(data);info.mode=mode;tar.addfile(info,io.BytesIO(data))
        manifest.update(package_sha256=gate.sha(path/'application.tar.gz'),package_bytes=(path/'application.tar.gz').stat().st_size,
                        expanded_bytes=sum(f['size'] for f in manifest['files'].values()),max_files=len(manifest['files']))
        (path/'manifest.json').write_text(json.dumps(manifest))
        subprocess.run(['openssl','dgst','-sha256','-sign',str(self.key),'-out',str(path/'manifest.sig'),str(path/'manifest.json')],check=True)
        report['artifact_sha256']=manifest['package_sha256'];(path/'validation.json').write_text(json.dumps(report))
        lock['assets']={p.name:gate.sha(p) for p in path.iterdir() if p.name!='release-lock.json'}
        (path/'release-lock.json').write_text(json.dumps(lock))
        return lock

    def build(self, root, same_board=False):
        self.fixture(root/'a','a133')
        self.fixture(root/'b','a133' if same_board else 'rk3576')
        out=root/'release'
        collection.assemble([('a133',root/'a'),('rk3576',root/'b')],out,'dshanpi/SmartDesktopMimi','host-test',self.key,self.checker,self.trust)
        return out

    def test_signed_multitarget_host_roundtrip_and_stable_block(self):
        with tempfile.TemporaryDirectory() as temp:
            out=self.build(Path(temp))
            gate.check(out,'prerelease',self.checker,self.trust)
            with self.assertRaisesRegex(ValueError,'required acceptance|stable requires'):
                gate.check(out,'stable',self.checker,self.trust)
            (out/'a133--application.tar.gz').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                gate.check(out,'draft',self.checker,self.trust)

    def test_signed_system_release_through_real_shared_checker(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)/'system';lock=self.fixture(root,'a133')
            app=gate.load(root/'manifest.json');source=gate.load(root/'device.json')
            destination=copy.deepcopy(source);destination['enrollment_id']=''
            destination['runtime']['system']=lock['version'];destination['system_baseline']='host-next'
            (root/'factory-manifest.json').write_bytes((root/'manifest.json').read_bytes())
            (root/'factory-manifest.sig').write_bytes((root/'manifest.sig').read_bytes())
            from system_image import image_name
            image_asset=image_name(lock['version'])
            (root/image_asset).write_bytes(b'host-image-not-flashable')
            (root/'system.swu').write_bytes(b'host-ota-not-flashable')
            device=dict(device=source,bootloader='1.0.0',partition_sha256='e'*64,storage='emmc',storage_bytes=2048,boot_mode='ab')
            (root/'device.json').write_text(json.dumps(device))
            system=dict(schema_version=1,policy=gate.POLICY,release_type='system',version=lock['version'],commit=lock['source_commit'],
                        key_id='host-test',sequence=2,image_sha256=gate.sha(root/image_asset),image_bytes=(root/image_asset).stat().st_size,
                        targets=app['targets'],bootloader=dict(min='1.0.0',max_exclusive='2.0.0',exclude=[]),partition_sha256='e'*64,
                        storage='emmc',min_storage_bytes=1024,boot_mode='ab',factory_manifest_sha256=gate.sha(root/'factory-manifest.json'),
                        factory_application_sha256=app['package_sha256'],
                        ota=dict(channel='testing',package_sha256=gate.sha(root/'system.swu'),package_bytes=(root/'system.swu').stat().st_size,destination_device=destination))
            lock.update(release_type='system',platform_id='a133',image_asset=image_asset,tag='system-a133-v'+lock['version'],hardware_commit='a'*40,
                        factory_application_commit=app['commit'],factory_application_sha256=app['package_sha256'],partition_sha256='e'*64)
            provenance_fixture.system(root,system,lock,app)
            (root/'system-manifest.json').write_text(json.dumps(system))
            subprocess.run(['openssl','dgst','-sha256','-sign',str(self.key),'-out',str(root/'system-manifest.sig'),str(root/'system-manifest.json')],check=True)
            report=gate.load(root/'validation.json');report['artifact_sha256']=system['image_sha256'];(root/'validation.json').write_text(json.dumps(report))
            lock['assets']={p.name:gate.sha(p) for p in root.iterdir() if p.name!='release-lock.json'}
            (root/'release-lock.json').write_text(json.dumps(lock))
            gate.check(root,'prerelease',self.checker,self.trust)
            names=preparation.COMMON | preparation.SYSTEM | {'device.json','system.swu',image_asset}
            plan=dict(schema_version=1,release_type='system',device_files=['device.json'],
                      assets={n:dict(path='system/'+n,sha256=gate.sha(root/n)) for n in names})
            preparation.write(root.parent/'plan.json',plan)
            prepared=preparation.prepare(root.parent/'plan.json',root.parent/'candidate',self.checker,self.trust)
            self.assertEqual(prepared['tag'],'system-a133-v'+lock['version'])
            (root/'system.swu').write_bytes(b'damaged')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):gate.check(root,'draft',self.checker,self.trust)

    def test_system_build_record_uses_real_factory_signature_verification(self):
        import test_system_record
        import system_record
        receipt=test_system_record.SystemRecordTests()
        receipt.setUp()
        try:
            app=receipt.root/'signed-app';self.fixture(app,'a133')
            factory=gate.load(app/'manifest.json');device=gate.load(app/'device.json')
            device['enrollment_id']=''
            (receipt.build/'metadata/factory-manifest.json').write_bytes((app/'manifest.json').read_bytes())
            (receipt.build/'metadata/factory-manifest.sig').write_bytes((app/'manifest.sig').read_bytes())
            preparation.write(receipt.build/'metadata/factory-device.json',device)
            receipt.manifest.update(SYSTEM_VERSION='1.0.0',SYSTEM_TAG='system-a133-v1.0.0')
            receipt.seal.update(factory_commit=factory['commit'],factory_version=factory['version'],
                                factory_package_sha256=factory['package_sha256'])
            for filename,sealed in [('factory-manifest.json','usr/lib/smartdesktopmimi/factory/manifest.json'),
                                    ('factory-manifest.sig','usr/lib/smartdesktopmimi/factory/manifest.sig'),
                                    ('factory-device.json','etc/smartdesktopmimi/device.json')]:
                receipt.seal['files'][sealed]['sha256']=gate.sha(receipt.build/'metadata'/filename)
            preparation.write(receipt.build/'metadata/baseline-lock.json',receipt.seal);receipt.refresh()
            record=system_record.prepare(receipt.build,receipt.plan_path,receipt.output,self.checker,self.trust)
            self.assertEqual(record['factory_application']['sha256'],factory['package_sha256'])
            # Even mutually consistent checksum/seal metadata cannot bless a bad signature.
            sig=receipt.build/'metadata/factory-manifest.sig';sig.write_bytes(b'invalid signature')
            receipt.seal['files']['usr/lib/smartdesktopmimi/factory/manifest.sig']['sha256']=gate.sha(sig)
            preparation.write(receipt.build/'metadata/baseline-lock.json',receipt.seal);receipt.refresh()
            with self.assertRaises(subprocess.CalledProcessError):
                system_record.prepare(receipt.build,receipt.plan_path,receipt.root/'rejected',self.checker,self.trust)
            self.assertFalse((receipt.root/'rejected').exists())
        finally:receipt.doCleanups()

    def test_ambiguous_targets_cannot_be_signed(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(subprocess.CalledProcessError):self.build(Path(temp),True)
            self.assertFalse((Path(temp)/'release').exists())

    def test_blocked_evidence_stays_draft(self):
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'target';lock=self.fixture(path,'a133')
            report=gate.load(path/'validation.json');report['targets'][0]['checks']['identity']['result']='blocked'
            (path/'validation.json').write_text(json.dumps(report));lock['assets']['validation.json']=gate.sha(path/'validation.json')
            (path/'release-lock.json').write_text(json.dumps(lock))
            gate.check(path,'draft',self.checker,self.trust)
            with self.assertRaisesRegex(ValueError,'known failure'):gate.check(path,'prerelease',self.checker,self.trust)

    def test_signed_application_preparation_and_signature_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'input';self.fixture(source,'a133')
            names=preparation.COMMON | preparation.APPLICATION | {'device.json'}
            plan=dict(schema_version=1,release_type='application',device_files=['device.json'],
                      assets={n:dict(path='input/'+n,sha256=gate.sha(source/n)) for n in names})
            preparation.write(root/'plan.json',plan)
            preparation.prepare(root/'plan.json',root/'candidate',self.checker,self.trust)
            self.assertEqual(gate.load(root/'candidate/validation.json')['targets'][0]['checks']['identity']['result'],'not_run')
            (source/'manifest.sig').write_bytes(b'invalid signature')
            plan['assets']['manifest.sig']['sha256']=gate.sha(source/'manifest.sig')
            preparation.write(root/'plan.json',plan)
            with self.assertRaises(subprocess.CalledProcessError):
                preparation.prepare(root/'plan.json',root/'bad',self.checker,self.trust)
            self.assertFalse((root/'bad').exists())

    def test_collection_upload_requires_every_target_before_network(self):
        from unittest.mock import Mock
        with tempfile.TemporaryDirectory() as temp:
            out=self.build(Path(temp));client=Mock()
            with self.assertRaisesRegex(ValueError,'upload requires completed'):
                publishing.publish(out,self.checker,self.trust,client)
            self.assertEqual(client.mock_calls,[])

    def test_unknown_stage_rejected(self):
        with self.assertRaisesRegex(ValueError,'unknown release stage'):
            gate.check(Path('/unused'),'stabel',self.checker,self.trust)

    def test_remote_promotion_rechecks_bytes_and_requires_draft(self):
        with tempfile.TemporaryDirectory() as temp:
            out=self.build(Path(temp));provenance_fixture.complete_synthetic_acceptance(out);client=FakeRelease(out)
            result=promotion.promote(out,'prerelease',self.checker,self.trust,client)
            self.assertEqual(result['stage'],'prerelease');self.assertEqual(client.patches,1)
            with self.assertRaisesRegex(ValueError,'existing draft'):
                promotion.promote(out,'prerelease',self.checker,self.trust,client)

    def test_no_publication_after_remote_tamper_extra_asset_or_race(self):
        with tempfile.TemporaryDirectory() as temp:
            out=self.build(Path(temp));provenance_fixture.complete_synthetic_acceptance(out)
            for failure in ['tamper','extra','race','tag']:
                with self.subTest(failure=failure):
                    client=FakeRelease(out,failure)
                    with self.assertRaises(ValueError):promotion.promote(out,'prerelease',self.checker,self.trust,client)
                    self.assertEqual(client.patches,0)


class FakeRelease:
    repository='dshanpi/SmartDesktopMimi'
    def __init__(self,directory,failure=None):
        self.directory=directory;self.failure=failure;self.patches=0;self.reads=0
        self.release=dict(id=1,draft=True,prerelease=True,tag_name='v1.1.0-rc.1',html_url='https://example.invalid/host-fixture')
    def verify_tag(self,lock):
        if self.failure=='tag':raise ValueError('moved tag')
    def find(self,tag):return copy.deepcopy(self.release)
    def assets(self,release):
        self.reads+=1
        result={p.name:dict(id=i,name=p.name,size=p.stat().st_size,url='https://api.github.com/host-fixture',updated_at='fixed') for i,p in enumerate(sorted(self.directory.iterdir()))}
        if self.failure=='extra':result['unexpected.txt']=dict(id=999,size=0)
        if self.failure=='race' and self.reads>1:result['release-index.json']['id']=1000
        return result
    def download(self,asset,path,size,digest):
        data=(self.directory/asset['name']).read_bytes()
        if self.failure=='tamper':data+=b'x'
        if len(data)!=size or hashlib.sha256(data).hexdigest()!=digest:raise ValueError('remote mismatch')
        path.write_bytes(data)
    def api(self,path,method='GET',data=None):
        if method=='PATCH':
            self.patches+=1;self.release.update(data)
        return copy.deepcopy(self.release)


class TransportTests(unittest.TestCase):
    def test_api_refuses_credential_leak_urls(self):
        client=GitHubRelease('dshanpi/SmartDesktopMimi','host-test-token')
        for url in ['http://api.github.com/x','https://evil.example/x','https://user@api.github.com/x','https://api.github.com:443/x']:
            with self.assertRaises(ValueError):client.request(url)

if __name__=='__main__':unittest.main()
