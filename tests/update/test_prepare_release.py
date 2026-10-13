"""Synthetic metadata tests; never device acceptance or upload to GitHub."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/update'))
import prepare_release as prepare
import release_gate as gate
import publish_draft
import promote_release
import test_release_gate as fixtures


class PreparationTests(unittest.TestCase):
    def plan(self, root, kind, validation=True):
        source=root/'inputs';source.mkdir()
        if kind=='application':
            fixtures.ReleaseEvidenceTests().fixture(source)
        else:
            fixtures.SystemDestinationEvidenceTests().system_fixture(source)
        names = prepare.COMMON | (prepare.APPLICATION if kind=='application' else prepare.SYSTEM) | {'device.json'}
        if kind=='system':names |= {'system.swu'}
        if validation:names |= {'validation.json', 'host.log'}
        plan=dict(schema_version=1,release_type=kind,device_files=['device.json'],
                  assets={name:dict(path='inputs/'+name,sha256=gate.sha(source/name)) for name in names})
        path=root/'plan.json';prepare.write(path,plan)
        return path,plan

    def test_both_kinds_same_version_separate_tags_no_fake_acceptance(self):
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run'):
            parent=Path(temp);tags=[]
            for kind in ['application','system']:
                root=parent/kind;root.mkdir();path,_=self.plan(root,kind,False)
                lock=prepare.prepare(path,root/'candidate',Path('/checker'),Path('/external-trust'))
                tags.append(lock['tag'])
                report=gate.load(root/'candidate/validation.json')
                self.assertTrue(all(e['result']=='not_run' for e in report['targets'][0]['checks'].values()))
                self.assertEqual(report['artifact_sha256'],lock['assets']['application.tar.gz' if kind=='application' else 'system.img.gz'])
            self.assertNotEqual(*tags)

    def test_pinned_input_rejects_modification_and_leaves_no_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.plan(root,'application')
            (root/'inputs/application.tar.gz').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'input changed'):
                prepare.prepare(path,root/'candidate',Path('/checker'),Path('/trust'))
            self.assertFalse((root/'candidate').exists())

    def test_failed_signature_leaves_no_partial_candidate(self):
        import subprocess
        with tempfile.TemporaryDirectory() as temp, patch.object(gate.subprocess,'run',side_effect=subprocess.CalledProcessError(1,'checker')):
            root=Path(temp);path,_=self.plan(root,'application')
            with self.assertRaises(subprocess.CalledProcessError):
                prepare.prepare(path,root/'candidate',Path('/checker'),Path('/trust'))
            self.assertFalse((root/'candidate').exists())
            self.assertFalse(list(root.glob('.mimi-prepare-*')))

    def test_unlisted_private_attachment_and_path_escape_rejected(self):
        for name in ['private-key.pem','../escape']:
            with self.subTest(name=name),tempfile.TemporaryDirectory() as temp:
                root=Path(temp);path,plan=self.plan(root,'application')
                plan['assets'][name]=dict(path='inputs/host.log',sha256=gate.sha(root/'inputs/host.log'))
                prepare.write(path,plan)
                with self.assertRaises(ValueError):prepare.prepare(path,root/'candidate',Path('/checker'),Path('/trust'))
                self.assertFalse((root/'candidate').exists())

    def test_existing_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);path,_=self.plan(root,'application');output=root/'candidate';output.mkdir();(output/'keep').write_text('keep')
            with self.assertRaisesRegex(ValueError,'already exists'):
                prepare.prepare(path,output,Path('/checker'),Path('/trust'))
            self.assertEqual((output/'keep').read_text(),'keep')

    def test_stale_validation_and_changed_provenance_rejected(self):
        for kind in ['application','system']:
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
                root=Path(temp);path,plan=self.plan(root,kind)
                report=gate.load(root/'inputs/validation.json');report['artifact_sha256']='0'*64
                prepare.write(root/'inputs/validation.json',report)
                plan['assets']['validation.json']['sha256']=gate.sha(root/'inputs/validation.json');prepare.write(path,plan)
                with self.assertRaisesRegex(ValueError,'does not describe'):
                    prepare.prepare(path,root/'candidate',Path('/checker'),Path('/trust'))
                self.assertFalse((root/'candidate').exists())

    def test_draft_and_promotion_block_before_network_for_both_types(self):
        for kind in ['application','system']:
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
                root=Path(temp);path,_=self.plan(root,kind)
                prepare.prepare(path,root/'candidate',Path('/checker'),Path('/trust'))
                client=Mock()
                for action in [lambda:publish_draft.publish(root/'candidate',Path('/checker'),Path('/trust'),client),
                               lambda:promote_release.promote(root/'candidate','prerelease',Path('/checker'),Path('/trust'),client)]:
                    with self.assertRaisesRegex(ValueError,'upload requires completed'):action()
                self.assertEqual(client.mock_calls,[])


if __name__=='__main__':unittest.main()
