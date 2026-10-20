"""Synthetic provenance metadata only; actual signed candidate tested separately."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import test_release_gate as fixture

gate = fixture.gate

class UbuntuProvenanceTests(unittest.TestCase):
    def candidate(self, root, mutation=None):
        lock, report = fixture.ReleaseEvidenceTests().fixture(root)
        sdk = gate.load(root/'sdk-lock.json')
        receipt = gate.load(root/'application-build.json')
        manifest = gate.load(root/'manifest.json')
        sdk.update(platform_id='ubuntu-host', architecture='x86_64')
        receipt.update(platform_id='ubuntu-host', architecture='x86_64')
        manifest['targets'][0]['identity'].update(platform_id='ubuntu-host', architecture='x86_64')
        device = gate.load(root/'device.json');device['identity']=manifest['targets'][0]['identity']
        (root/'device.json').write_text(json.dumps(device));lock['assets']['device.json']=gate.sha(root/'device.json')
        ownership=gate.load(root/'source-ownership.json');assert ownership['platform_id']=='a133'
        sdk.update(schema_version=2, target_os='ubuntu', target_os_version='20.04')
        receipt.update(target_os='ubuntu', target_os_version='20.04')
        manifest['targets'][0]['capabilities'] = ['target-os:ubuntu:20.04']
        if mutation: mutation(sdk, receipt, manifest)
        def write(name, value):
            (root/name).write_text(json.dumps(value))
            lock['assets'][name] = gate.sha(root/name)
        write('sdk-lock.json', sdk)
        lock['sdk_lock_sha256'] = receipt['sdk_lock_sha256'] = lock['assets']['sdk-lock.json']
        write('application-build.json', receipt)
        manifest['files']['resources/application-build.json']['sha256'] = lock['assets']['application-build.json']
        write('manifest.json', manifest)
        (root/'release-lock.json').write_text(json.dumps(lock))

    def check(self, mutation=None):
        with tempfile.TemporaryDirectory() as directory, patch.object(gate.subprocess, 'run'):
            root = Path(directory); self.candidate(root, mutation)
            return gate.check(root, 'draft', Path('/checker'), Path('/trust'))

    def test_runtime_target_remains_bound_to_sdk(self):
        import build_provenance
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.candidate(root)
            lock=gate.load(root/'release-lock.json');manifest=gate.load(root/'manifest.json')
            manifest['targets'][0]['identity']['platform_id']='a133'
            with self.assertRaisesRegex(ValueError,'SDK/build/package target mismatch'):
                build_provenance.application(root,lock,manifest,'draft',gate.load)

    def test_unknown_source_registry_rejected(self):
        import build_provenance
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.candidate(root)
            lock=gate.load(root/'release-lock.json');manifest=gate.load(root/'manifest.json')
            ownership=gate.load(root/'source-ownership.json');ownership['platform_id']='unknown'
            (root/'source-ownership.json').write_text(json.dumps(ownership))
            with self.assertRaisesRegex(ValueError,'source ownership/build provenance mismatch'):
                build_provenance.application(root,lock,manifest,'draft',gate.load)

    def test_schema2_with_exact_os_capability(self):
        self.check()

    def test_unknown_schema_and_bool_rejected(self):
        for schema in (3, True):
            with self.subTest(schema=schema), self.assertRaisesRegex(ValueError, 'unknown build/SDK'):
                self.check(lambda s, r, m: s.update(schema_version=schema))

    def test_unknown_os_and_wrong_receipt_version_rejected(self):
        for mutate in (lambda s,r,m:s.update(target_os='tina'), lambda s,r,m:r.update(target_os_version='22.04')):
            with self.assertRaisesRegex(ValueError, 'OS provenance mismatch'): self.check(mutate)

    def test_missing_wrong_and_partially_scoped_capability_rejected(self):
        for caps in ([], ['target-os:ubuntu:22.04']):
            with self.subTest(caps=caps), self.assertRaisesRegex(ValueError, 'lacks SDK OS capability'):
                self.check(lambda s,r,m:m['targets'][0].update(capabilities=caps))
        # Test the provenance layer directly so every signed target is checked,
        # without the earlier evidence-matrix duplicate check masking this case.
        import build_provenance
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);self.candidate(root)
            lock=gate.load(root/'release-lock.json');manifest=gate.load(root/'manifest.json')
            second=dict(manifest['targets'][0]);second['capabilities']=[];manifest['targets'].append(second)
            with self.assertRaisesRegex(ValueError,'lacks SDK OS capability'):
                build_provenance.application(root,lock,manifest,'draft',gate.load)

if __name__ == '__main__': unittest.main()
