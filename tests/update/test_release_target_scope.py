"""RELEASE072 real signed rc24 admission; never hardware certification."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/update'))
import release_gate as gate
import prepare_release as prepare


class EvidenceScopeBoundaryTests(unittest.TestCase):
    def test_single_declared_scope_and_unknown_identity(self):
        target = dict(identity={'board': 'host-only'}, runtime={'system': {'min':'1.0.0'}})
        manifest = dict(targets=[target])
        self.assertEqual(gate.evidence_scope(manifest, dict(identity=target['identity']), Path('/unused')),gate.target_key(target))
        with self.assertRaisesRegex(ValueError, 'undeclared'):
            gate.evidence_scope(manifest,dict(identity={'board':'wrong'}),Path('/unused'))

    def test_ambiguous_predicates_are_refused_even_when_the_checker_accepts_both(self):
        target=dict(identity={'board':'host-only'},runtime={})
        with patch.object(gate.subprocess,'run',return_value=subprocess.CompletedProcess([],0)):
            with self.assertRaisesRegex(ValueError,'ambiguous'):
                gate.evidence_scope(dict(targets=[target,copy.deepcopy(target)]),dict(identity=target['identity']),Path('/checker'))


@unittest.skipUnless(os.environ.get('MIMI_RELEASE_SCOPE_FIXTURES'),'requires actual signed SYSTEM070 rc24 candidate')
class ActualSignedReleaseScopeTests(unittest.TestCase):
    def setUp(self):
        self.delivery=Path(os.environ['MIMI_RELEASE_SCOPE_FIXTURES']).resolve()
        self.plan=gate.load(self.delivery/'system070-release-inputs/application-plan.json')
        self.checker=self.delivery/'system-edition068/mimi-update-host'
        self.trust=Path.home()/'.local/share/smartdesktopmimi-signing/20261012/trust.json'
        self.manifest=gate.load(self.delivery/'system070-application-rc24-signed/manifest.json')
        self.device=gate.load(self.delivery/'system070-inputs/device-public.json')

    def admit(self, root, plan):
        path=root/'plan.json';path.write_text(json.dumps(plan))
        return prepare.prepare(path,root/'candidate',self.checker,self.trust)

    def test_actual_common_package_covers_both_runtimes_but_cannot_claim_hardware(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);lock=self.admit(root,self.plan)
            self.assertEqual(lock['version'],'1.0.8-rc.24')
            report=gate.load(root/'candidate/validation.json')
            self.assertEqual(len(report['targets']),2)
            self.assertTrue(all(check['result']=='not_run' for target in report['targets'] for check in target['checks'].values()))
            with self.assertRaisesRegex(ValueError,'required acceptance'):
                gate.check(root/'candidate','stable',self.checker,self.trust)
            with self.assertRaisesRegex(ValueError,'upload requires completed acceptance'):
                gate.require_upload_acceptance(root/'candidate',lock)

    def test_two_aliases_inside_one_range_cannot_replace_the_other_system(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);plan=copy.deepcopy(self.plan)
            alias=copy.deepcopy(self.device)
            alias['enrollment_id']='host-only-same-target-alias'
            alias['runtime']['updater']='1.0.0+alias'
            path=root/'alias.json';path.write_text(json.dumps(alias))
            plan['assets']['device-private.json']=dict(path=str(path),sha256=gate.sha(path))
            with self.assertRaisesRegex(ValueError,'duplicate target evidence scope'):
                self.admit(root,plan)
            self.assertFalse((root/'candidate').exists())

    def test_missing_system_evidence_is_not_silently_covered(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);plan=copy.deepcopy(self.plan)
            plan['device_files'].remove('device-private.json');del plan['assets']['device-private.json']
            with self.assertRaisesRegex(ValueError,'missing declared target evidence scope'):
                self.admit(root,plan)

    def test_overlapping_runtime_declarations_are_ambiguous(self):
        manifest=copy.deepcopy(self.manifest)
        manifest['targets'][1]['runtime']['system']=copy.deepcopy(manifest['targets'][0]['runtime']['system'])
        with self.assertRaisesRegex(ValueError,'ambiguous'):
            gate.evidence_scope(manifest,self.device,self.checker)

    def test_system_scope_uses_source_runtime_without_changing_destination_carrier(self):
        # Predicate-only projection: the real signed system/factory admission
        # remains mandatory at its caller. Source data versions must not be
        # misclassified by destination application constraints.
        system=gate.load(self.delivery/'system070-public-record/system-manifest.json')
        system['targets']=copy.deepcopy(self.manifest['targets'])
        carrier=copy.deepcopy(self.manifest);before=copy.deepcopy(carrier)
        source=copy.deepcopy(self.device);source['data_schema']=7
        actual=gate.evidence_scope(system,source,self.checker,carrier)
        self.assertEqual(actual,gate.target_key(system['targets'][0]))
        self.assertEqual(carrier,before)

    def test_wrong_soc_still_fails_the_real_signature_and_archive_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);plan=copy.deepcopy(self.plan);device=copy.deepcopy(self.device)
            device['identity']['soc_id']='allwinner-t527'
            path=root/'wrong-soc.json';path.write_text(json.dumps(device))
            plan['assets']['device-public.json']=dict(path=str(path),sha256=gate.sha(path))
            with self.assertRaises(subprocess.CalledProcessError):self.admit(root,plan)


if __name__=='__main__':unittest.main()
