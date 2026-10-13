import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
import provenance_fixture
spec=importlib.util.spec_from_file_location('release_gate',ROOT/'tools/update/release_gate.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)

class ReleaseGateTests(unittest.TestCase):
    def test_duplicate_identity_cannot_be_hidden_in_json(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'manifest.json';p.write_text('{"board":"a133","board":"rk3576"}')
            with self.assertRaises(ValueError):gate.load(p)

    def test_no_release_from_empty_or_legacy_metadata(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            for data in [{},{'schema_version':1},{'schema_version':1,'policy':gate.POLICY,'release_type':'unknown'}]:
                (directory/'release-lock.json').write_text(json.dumps(data))
                with self.assertRaises((ValueError,KeyError)):
                    gate.check(directory,'draft',Path('/nonexistent-checker'),Path('/external-trust'))

    def test_gate_has_explicit_physical_acceptance_requirements(self):
        self.assertTrue({'power_loss','navigation_touch','system_application_pairing','rollback','voice'} <= gate.HARDWARE)
        self.assertTrue({'signature','compatibility','transaction_recovery'} <= gate.AUTOMATED)


class ReleaseEvidenceTests(unittest.TestCase):
    def fixture(self, directory):
        from unittest.mock import patch
        identity={'platform_id':'a133','board_id':'B6','ipc_contract':'mimi-ipc-v1.1'}
        files={
            'source.tar.gz':b'fixture-source', 'sbom.spdx.json':b'{}',
            'application.tar.gz':b'fixture-package', 'manifest.sig':b'fixture-signature',
            'platform-contract-lock.json':json.dumps({'contract_id':'mimi-ipc-v1.1'}).encode(),
            'device.json':json.dumps({'identity':identity}).encode(),
            'host.log':b'host-fixture-results-only',
        }
        for name,data in files.items():(directory/name).write_bytes(data)
        package_hash=gate.sha(directory/'application.tar.gz')
        manifest={'commit':'a'*40,'version':'1.1.0-rc.1','channel':'testing','package_sha256':package_hash,'targets':[{'identity':identity}]}
        (directory/'manifest.json').write_text(json.dumps(manifest))
        report={'schema_version':1,'source_commit':'a'*40,'artifact_sha256':package_hash,
                'targets':[{'device_file':'device.json','checks':{}}]}
        for case in gate.AUTOMATED:
            report['targets'][0]['checks'][case]={'result':'passed','method':'host','evidence':'host.log'}
        for case in gate.HARDWARE:
            report['targets'][0]['checks'][case]={'result':'not_run','method':'physical-device','reason':'No physical fixture'}
        (directory/'validation.json').write_text(json.dumps(report))
        lock={'schema_version':1,'policy':gate.POLICY,'release_type':'application','source_commit':'a'*40,
              'version':'1.1.0-rc.1','tag':'v1.1.0-rc.1','ipc_contract':'mimi-ipc-v1.1',
              'toolchain_sha256':'b'*64,'sysroot_sha256':'c'*64,'dependency_lock_sha256':'d'*64,
              'assets':{p.name:gate.sha(p) for p in directory.iterdir()}}
        provenance_fixture.application(directory,manifest,lock)
        (directory/'device.json').write_text(json.dumps({'identity':identity}))
        (directory/'manifest.json').write_text(json.dumps(manifest))
        lock['assets']={p.name:gate.sha(p) for p in directory.iterdir() if p.name!='release-lock.json'}
        (directory/'release-lock.json').write_text(json.dumps(lock))
        return lock,report

    def mutate_report(self, directory, lock, report):
        (directory/'validation.json').write_text(json.dumps(report))
        lock['assets']['validation.json']=gate.sha(directory/'validation.json')
        (directory/'release-lock.json').write_text(json.dumps(lock))

    def test_draft_and_prerelease_do_not_imply_physical_pass(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            directory=Path(temp);self.fixture(directory)
            for stage in ['draft','prerelease']:gate.check(directory,stage,Path('/checker'),Path('/external-trust'))
            with self.assertRaisesRegex(ValueError,'required acceptance'):
                gate.check(directory,'stable',Path('/checker'),Path('/external-trust'))

    def test_host_cannot_claim_hardware_pass(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            directory=Path(temp);lock,report=self.fixture(directory)
            report['targets'][0]['checks']['power_loss']={'result':'passed','method':'host','evidence':'host.log'}
            self.mutate_report(directory,lock,report)
            with self.assertRaisesRegex(ValueError,'host evidence'):
                gate.check(directory,'draft',Path('/checker'),Path('/external-trust'))

    def test_known_failure_stays_draft(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            directory=Path(temp);lock,report=self.fixture(directory)
            report['targets'][0]['checks']['cold_boot']['result']='failed'
            self.mutate_report(directory,lock,report)
            gate.check(directory,'draft',Path('/checker'),Path('/external-trust'))
            with self.assertRaisesRegex(ValueError,'known failure'):
                gate.check(directory,'prerelease',Path('/checker'),Path('/external-trust'))

    def test_stale_artifact_evidence_rejected(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            directory=Path(temp);lock,report=self.fixture(directory)
            report['artifact_sha256']='e'*64;self.mutate_report(directory,lock,report)
            with self.assertRaisesRegex(ValueError,'does not describe'):
                gate.check(directory,'draft',Path('/checker'),Path('/external-trust'))

    def test_remote_asset_replacement_is_detectable(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            directory=Path(temp);self.fixture(directory)
            (directory/'application.tar.gz').write_bytes(b'replaced')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):
                gate.check(directory,'draft',Path('/checker'),Path('/external-trust'))

if __name__=='__main__':unittest.main()

class SystemDestinationEvidenceTests(unittest.TestCase):
    fixture = ReleaseEvidenceTests.fixture
    def system_fixture(self, directory):
        lock, report = self.fixture(directory)
        identity = {'platform_id':'a133','board_id':'B6','ipc_contract':'mimi-ipc-v1.1'}
        source = {'identity':identity, 'runtime':{'system':'1.0.0'}, 'enrollment_id':'host-only'}
        destination = {'identity':identity, 'runtime':{'system':lock['version']}, 'enrollment_id':''}
        (directory/'device.json').write_text(json.dumps({'device':source}))
        for name, payload in [('system.img.gz',b'host-image'),('system.swu',b'host-swu'),('system-manifest.sig',b'host-signature'),('sdk-components.json',b'[]')]:
            (directory/name).write_bytes(payload)
        (directory/'factory-manifest.json').write_bytes((directory/'manifest.json').read_bytes())
        (directory/'factory-manifest.sig').write_bytes(b'factory-signature')
        system = {'commit':lock['source_commit'],'version':lock['version'],'targets':[{'identity':identity}],
                  'image_sha256':gate.sha(directory/'system.img.gz'),'partition_sha256':'e'*64,
                  'factory_manifest_sha256':gate.sha(directory/'factory-manifest.json'),
                  'sdk_components_sha256':gate.sha(directory/'sdk-components.json'),
                  'factory_application_sha256':lock['assets']['application.tar.gz'],
                  'ota':{'channel':'testing','package_sha256':gate.sha(directory/'system.swu'),'destination_device':destination}}
        (directory/'system-manifest.json').write_text(json.dumps(system))
        report['artifact_sha256']=system['image_sha256']
        (directory/'validation.json').write_text(json.dumps(report))
        lock.update(release_type='system', platform_id='a133', tag='system-a133-v'+lock['version'], hardware_repository='100askTeam/A133-AIDesktop-Tina5', hardware_commit='a'*40, factory_application_commit='a'*40,
                    partition_sha256='e'*64, factory_application_sha256=system['factory_application_sha256'])
        provenance_fixture.system(directory,system,lock,json.loads((directory/'factory-manifest.json').read_text()))
        (directory/'system-manifest.json').write_text(json.dumps(system))
        lock['assets']={p.name:gate.sha(p) for p in directory.iterdir() if p.name!='release-lock.json'}
        (directory/'release-lock.json').write_text(json.dumps(lock))
        return lock, system

    def test_factory_gate_uses_destination_runtime_and_also_verifies_swu(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp); self.system_fixture(directory); calls=[]
            def checker(command, **kwargs):
                calls.append(command[1])
                if command[1]=='verify-manifest':
                    device=json.loads(Path(command[command.index('--device')+1]).read_text())
                    self.assertEqual(device['runtime']['system'],'1.1.0-rc.1')
                    self.assertEqual(device['enrollment_id'],'host-only')
            with patch.object(gate.subprocess,'run',side_effect=checker):
                gate.check(directory,'draft',Path('/checker'),Path('/external-trust'))
            self.assertEqual(calls,['verify-system','verify-manifest','verify-system-ota'])

    def test_swu_is_required_when_manifest_declares_ota(self):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            directory=Path(temp);lock,_=self.system_fixture(directory)
            del lock['assets']['system.swu']
            (directory/'release-lock.json').write_text(json.dumps(lock))
            with self.assertRaisesRegex(ValueError,'OTA artifact'):
                gate.check(directory,'draft',Path('/checker'),Path('/external-trust'))
