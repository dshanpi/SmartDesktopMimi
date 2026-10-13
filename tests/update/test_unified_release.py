import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
import release_gate as gate
import release_identity as identity
import release_workflow as workflow
import github_release as github
import promote_release as promotion
import publish_draft as publishing
import test_release_gate as fixtures
import provenance_fixture
from test_release_collection import FakeRelease


class UnifiedReleaseTests(unittest.TestCase):
    def test_tag_namespaces_and_unknown_types(self):
        for kind,platform,version,tag in [('application',None,'1.2.3','v1.2.3'),('system','a133','1.2.3','system-a133-v1.2.3'),('system','rk3576','1.2.3-rc.1','system-rk3576-v1.2.3-rc.1')]:
            self.assertEqual(identity.tag_for(kind,version,platform),tag)
            self.assertEqual(identity.parse_tag(tag),(kind,platform,version))
        for tag in ['v01.2.3','system-a133-v1.2.3-01','system--v1.2.3','../v1.2.3','v1.2','v1.2.3/../x']:
            with self.assertRaises(ValueError):identity.parse_tag(tag)

    def test_workflow_accepts_both_namespaces_and_rejects_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for tag in ['v1.2.3','system-a133-v1.2.3']:
                (root/tag).mkdir();self.assertEqual(workflow.candidate(root,tag,'draft'),root/tag)
            with self.assertRaises(ValueError):workflow.candidate(root,'../v1.2.3','draft')

    def test_system_draft_and_promotion_use_unified_repository(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
            root=Path(temp);lock,_=fixtures.SystemDestinationEvidenceTests().system_fixture(root)
            github.verify_repository(lock,identity.PUBLIC_REPOSITORY)
            provenance_fixture.complete_synthetic_acceptance(root)
            lock=gate.load(root/'release-lock.json')
            client=FakeRelease(root);client.release['tag_name']=lock['tag']
            result=publishing.publish(root,Path('/checker'),Path('/external-trust'),client)
            self.assertEqual(result['assets_verified'],len(lock['assets'])+1)
            result=promotion.promote(root,'prerelease',Path('/checker'),Path('/external-trust'),client)
            self.assertEqual(result['stage'],'prerelease');self.assertEqual(client.patches,1)

    def test_system_wrong_platform_tag_or_private_repository_rejected(self):
        for field,value in [('tag','v1.1.0-rc.1'),('platform_id','rk3576'),('hardware_repository','public/vendor')]:
            with self.subTest(field=field),tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
                root=Path(temp);lock,_=fixtures.SystemDestinationEvidenceTests().system_fixture(root)
                lock[field]=value;(root/'release-lock.json').write_text(json.dumps(lock))
                with self.assertRaises(ValueError):gate.check(root,'draft',Path('/checker'),Path('/trust'))

    def test_system_provenance_cannot_point_to_other_sdk_or_factory(self):
        for field,value in [('system_version','9.9.9'),('hardware_commit','b'*40),('image_sha256','a'*64),('sdk_components_sha256','c'*64),('factory_application',{})]:
            with self.subTest(field=field),tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
                root=Path(temp);lock,manifest=fixtures.SystemDestinationEvidenceTests().system_fixture(root)
                record=gate.load(root/'system-build.json');record[field]=value
                (root/'system-build.json').write_text(json.dumps(record))
                lock['assets']['system-build.json']=gate.sha(root/'system-build.json')
                manifest['build_provenance_sha256']=lock['assets']['system-build.json']
                (root/'system-manifest.json').write_text(json.dumps(manifest));lock['assets']['system-manifest.json']=gate.sha(root/'system-manifest.json')
                (root/'release-lock.json').write_text(json.dumps(lock))
                with self.assertRaises(ValueError):gate.check(root,'draft',Path('/checker'),Path('/trust'))

    def test_system_sdk_inventory_and_signed_record_are_required(self):
        for mode in ['empty-components','missing-signature-binding']:
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
                root=Path(temp);lock,manifest=fixtures.SystemDestinationEvidenceTests().system_fixture(root)
                if mode=='missing-signature-binding':manifest.pop('build_provenance_sha256')
                else:
                    components=gate.load(root/'sdk-components.json');components['components']=[]
                    (root/'sdk-components.json').write_text(json.dumps(components));lock['assets']['sdk-components.json']=gate.sha(root/'sdk-components.json')
                    record=gate.load(root/'system-build.json');record['sdk_components_sha256']=lock['assets']['sdk-components.json']
                    (root/'system-build.json').write_text(json.dumps(record));lock['assets']['system-build.json']=gate.sha(root/'system-build.json')
                    manifest['build_provenance_sha256']=lock['assets']['system-build.json'];manifest['sdk_components_sha256']=lock['assets']['sdk-components.json']
                (root/'system-manifest.json').write_text(json.dumps(manifest));lock['assets']['system-manifest.json']=gate.sha(root/'system-manifest.json')
                (root/'release-lock.json').write_text(json.dumps(lock))
                with self.assertRaises(ValueError):gate.check(root,'draft',Path('/checker'),Path('/trust'))

    def test_application_missing_receipt_and_dirty_source_cannot_publish(self):
        for mode in ['missing','dirty','sdk','payload']:
            with self.subTest(mode=mode),tempfile.TemporaryDirectory() as temp,patch.object(gate.subprocess,'run'):
                root=Path(temp);lock,_=fixtures.ReleaseEvidenceTests().fixture(root)
                if mode=='missing':del lock['assets']['application-build.json']
                elif mode=='sdk':lock['sdk_lock_sha256']='0'*64
                elif mode=='payload':
                    manifest=gate.load(root/'manifest.json');manifest['files']['bin/lvglsim']['sha256']='0'*64
                    (root/'manifest.json').write_text(json.dumps(manifest));lock['assets']['manifest.json']=gate.sha(root/'manifest.json')
                else:
                    receipt=gate.load(root/'application-build.json');receipt['source']['dirty']=True
                    (root/'application-build.json').write_text(json.dumps(receipt));lock['assets']['application-build.json']=gate.sha(root/'application-build.json')
                (root/'release-lock.json').write_text(json.dumps(lock))
                with self.assertRaises(ValueError):gate.check(root,'prerelease',Path('/checker'),Path('/trust'))

if __name__=='__main__':unittest.main()
