"""Component replay tests use local Git fixtures, never hardware acceptance."""
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

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
import sdk_snapshot as snapshot


class SDKSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'sdk'
        self.repo = self.sdk / 'component'
        self.repo.mkdir(parents=True)
        subprocess.run(['git', 'init', '-q', str(self.repo)], check=True)
        for key, value in [('user.name', 'Fixture'), ('user.email', 'fixture@example.invalid')]:
            self.git('config', key, value)
        (self.repo / 'binary').write_bytes(bytes(range(256)))
        (self.repo / 'deleted').write_text('old input')
        (self.repo / '.gitignore').write_text('cache/\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'base')
        self.commit = self.git('rev-parse', 'HEAD').decode().strip()
        (self.repo / 'binary').write_bytes(bytes(reversed(range(256))))
        (self.repo / 'binary').chmod(0o755)
        (self.repo / 'deleted').unlink()
        (self.repo / 'added').write_text('new local input')
        (self.repo / 'added').chmod(0o644)
        (self.repo / 'cache').mkdir()
        (self.repo / 'cache' / 'generated').write_text('ignored output')
        self.store = self.root / 'snapshot'
        self.folder = self.store / '000'
        self.folder.mkdir(parents=True)
        (self.folder / 'tracked.patch').write_bytes(self.git('diff', '--binary', '--full-index', 'HEAD'))
        self.inputs = {'tracked_patch_sha256': snapshot.digest(self.folder / 'tracked.patch'),
                       'untracked': {'added': {'sha256': snapshot.digest(self.repo / 'added'), 'mode': 0o644}}}
        self.index = {'schema_version': 1, 'components': [{'path': 'component', 'commit': self.commit,
                       'snapshot_directory': '000'}]}
        self.pack()

    def git(self, *args):
        return snapshot.git(self.repo, *args)

    def pack(self, extra=None):
        with tarfile.open(self.folder / 'untracked.tar.gz', 'w:gz') as archive:
            for name in self.inputs['untracked']:
                archive.add(self.repo / name, arcname=name, recursive=False)
            if extra:
                info = tarfile.TarInfo(extra)
                info.size = 3
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(b'bad'))
        self.repin()

    def repin(self):
        (self.folder / 'inputs.json').write_bytes(snapshot.canonical(self.inputs))
        self.index['components'][0].update(local_inputs_sha256=hashlib.sha256(snapshot.canonical(self.inputs)).hexdigest(),
            untracked_archive_sha256=snapshot.digest(self.folder / 'untracked.tar.gz'))
        (self.store / 'sdk-components.json').write_text(json.dumps(self.index))
        self.pin = snapshot.digest(self.store / 'sdk-components.json')

    def run_verify(self):
        return snapshot.verify(self.store, self.sdk, self.pin)

    def test_binary_mode_delete_replayed_without_mutating_checkout_or_git(self):
        before = {str(p.relative_to(self.repo)): snapshot.digest(p) for p in self.repo.rglob('*') if p.is_file()}
        report = self.run_verify()
        after = {str(p.relative_to(self.repo)): snapshot.digest(p) for p in self.repo.rglob('*') if p.is_file()}
        self.assertEqual(before, after)
        self.assertEqual(report['component_replay'], 'passed')
        self.assertEqual(report['components'][0]['changed_tracked_files'], 2)
        self.assertEqual(report['components'][0]['ignored_paths_not_captured'], ['cache/'])
        self.assertFalse(report['release_qualified'])
        self.assertEqual(report['system_build'], 'not_run')

    def test_external_pin_and_archive_tampering_rejected(self):
        with self.assertRaisesRegex(ValueError, 'external pin'):
            snapshot.verify(self.store, self.sdk, 'f' * 64)
        with (self.folder / 'untracked.tar.gz').open('ab') as stream:
            stream.write(b'tampered')
        with self.assertRaisesRegex(ValueError, 'archive hash'):
            self.run_verify()

    def test_live_input_and_wrong_commit_rejected(self):
        (self.repo / 'added').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'live input changed'):
            self.run_verify()
        (self.repo / 'added').write_text('new local input')
        self.git('commit', '--allow-empty', '-qm', 'different baseline')
        with self.assertRaisesRegex(ValueError, 'commit changed'):
            self.run_verify()

    def test_archive_traversal_and_duplicate_rejected(self):
        for name in ['../escape', 'added', 'unlisted']:
            with self.subTest(name=name):
                self.pack(extra=name)
                with self.assertRaises(ValueError):
                    self.run_verify()
        self.assertFalse((self.root / 'escape').exists())

    def test_absolute_link_is_reported_never_extracted_or_qualified(self):
        (self.repo / 'link').symlink_to('/original/developer/checkout')
        self.inputs['untracked']['link'] = {'link': '/original/developer/checkout', 'mode': 0o777}
        self.pack()
        report = self.run_verify()
        self.assertEqual(report['nonportable_input_count'], 1)
        self.assertEqual(report['components'][0]['nonportable_inputs'][0]['reason'], 'absolute_link')
        self.assertFalse(report['release_qualified'])

    def test_component_and_snapshot_symlink_escape_rejected(self):
        self.index['components'][0]['path'] = '../sdk/component'
        self.repin()
        with self.assertRaises(ValueError):
            self.run_verify()
        self.index['components'][0]['path'] = 'component'
        self.repin()
        saved = self.root / 'saved-patch'
        (self.folder / 'tracked.patch').rename(saved)
        (self.folder / 'tracked.patch').symlink_to(saved)
        with self.assertRaisesRegex(ValueError, 'artifact symlink'):
            self.run_verify()

    def test_duplicate_and_nested_components_rejected(self):
        for path in ['component', 'component/child']:
            duplicate = copy.deepcopy(self.index['components'][0])
            duplicate.update(path=path, snapshot_directory='001')
            self.index['components'] = [self.index['components'][0], duplicate]
            self.repin()
            with self.assertRaises(ValueError):
                self.run_verify()

    def test_reject_json_duplicate_fields_and_special_mode(self):
        (self.folder / 'inputs.json').write_text('{"untracked":{},"untracked":{}}')
        with self.assertRaisesRegex(ValueError, 'duplicate JSON'):
            self.run_verify()
        self.inputs['untracked']['added']['mode'] = 0o4755
        self.repin()
        with self.assertRaisesRegex(ValueError, 'unsafe input mode'):
            self.run_verify()

    def test_delivery_working_tree_cannot_be_public_source(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        import public_release_audit
        for name in ['.delivery/hardware/overlay/config', '.delivery/sdk-input-snapshot/000/tracked.patch']:
            self.assertEqual(public_release_audit.path_rule(name), 'private-or-restricted-path')


if __name__ == '__main__':
    unittest.main()
