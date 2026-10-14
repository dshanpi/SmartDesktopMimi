import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/update'))
import sdk_snapshot as audit
import replay_kernel_component as replay


class KernelReplayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.delivery = Path(self.tmp.name) / '.delivery'
        self.sdk = self.delivery / 'sdk'
        self.kernel = self.sdk / replay.COMPONENT
        self.kernel.mkdir(parents=True)
        replay.git(self.kernel, 'init', '--quiet')
        replay.git(self.kernel, 'config', 'user.name', 'Replay fixture')
        replay.git(self.kernel, 'config', 'user.email', 'fixture@invalid')
        (self.kernel / '.gitignore').write_text('.config\n*.o\nvmlinux\n')
        (self.kernel / 'kernel.c').write_text('old\n')
        replay.git(self.kernel, 'add', '.')
        replay.git(self.kernel, 'commit', '--quiet', '-m', 'fixture base')
        self.commit = replay.git(self.kernel, 'rev-parse', 'HEAD').decode().strip()
        (self.kernel / 'kernel.c').write_text('saved modification\n')
        (self.kernel / '.config').write_text('CONFIG_FIXTURE=y\n')
        (self.kernel / 'driver.c').write_text('saved untracked source\n')
        (self.kernel / '.config').chmod(0o600)
        (self.kernel / 'driver.c').chmod(0o600)
        self.snapshot = self.delivery / 'snapshot'
        self.folder = self.snapshot / '000'
        self.folder.mkdir(parents=True)
        patch = replay.git(self.kernel, 'diff', '--no-ext-diff', '--no-textconv', '--binary', '--full-index', 'HEAD')
        (self.folder / 'tracked.patch').write_bytes(patch)
        data = (self.kernel / 'driver.c').read_bytes()
        self.inputs = {'tracked_patch_sha256': hashlib.sha256(patch).hexdigest(),
                       'untracked': {'driver.c': self.desc(data)}}
        self.archive(self.folder / 'untracked.tar.gz', {'driver.c': data})
        self.component = {'path': replay.COMPONENT, 'commit': self.commit,
                          'snapshot_directory': '000', 'local_inputs_sha256': '',
                          'untracked_archive_sha256': ''}
        self.supplement = self.delivery / 'supplement'
        self.supplement.mkdir()
        config = (self.kernel / '.config').read_bytes()
        self.archive(self.supplement / 'inputs.tar.gz', {replay.COMPONENT + '/.config': config})
        extra = {'files': {replay.COMPONENT + '/.config': self.desc(config)},
                 'archive_sha256': audit.digest(self.supplement / 'inputs.tar.gz')}
        (self.supplement / 'supplement.json').write_text(json.dumps(extra))
        self.stage = self.delivery / 'fresh-kernel'
        self.retained = self.delivery / 'retained/kernel'
        self.refresh()

    @staticmethod
    def desc(data):
        return {'mode': 0o600, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}

    @staticmethod
    def archive(path, files):
        with tarfile.open(path, 'w:gz') as archive:
            for name, data in files.items():
                info = tarfile.TarInfo(name)
                info.mode, info.size = 0o600, len(data)
                archive.addfile(info, io.BytesIO(data))

    def refresh(self):
        (self.folder / 'inputs.json').write_text(json.dumps(self.inputs))
        self.component['local_inputs_sha256'] = hashlib.sha256(audit.canonical(self.inputs)).hexdigest()
        self.component['untracked_archive_sha256'] = audit.digest(self.folder / 'untracked.tar.gz')
        (self.snapshot / 'sdk-components.json').write_text(json.dumps({'components': [self.component]}))
        self.snapshot_pin = audit.digest(self.snapshot / 'sdk-components.json')
        self.supplement_pin = audit.digest(self.supplement / 'supplement.json')

    def prepare(self):
        return replay.prepare(self.sdk, self.stage, self.snapshot, self.snapshot_pin,
                              self.supplement, self.supplement_pin)

    def terminal_job(self):
        a = subprocess.Popen(['/bin/true']); a.wait()
        b = subprocess.Popen(['/bin/true']); b.wait()
        path = self.delivery / 'job.json'
        path.write_text(json.dumps({'phase': 'passed', 'pid': a.pid,
                                   'child_pid': b.pid, 'finished_utc': time.time()}))
        return path

    def test_saved_source_and_config_replayed_without_old_cache(self):
        (self.kernel / 'old.o').write_bytes(b'not a source input')
        record = self.prepare()
        replay.verify(self.stage, record)
        replay.cache_free(self.stage)
        self.assertFalse((self.stage / 'old.o').exists())
        self.assertTrue((self.kernel / 'old.o').exists())
        self.assertEqual((self.stage / 'kernel.c').read_text(), 'saved modification\n')
        self.assertEqual((self.stage / 'driver.c').read_text(), 'saved untracked source\n')
        self.assertEqual(record['activation'], 'not_run')

    def test_wrong_snapshot_pin_preserves_live_source(self):
        self.snapshot_pin = '0' * 64
        with self.assertRaisesRegex(ValueError, 'snapshot pin'):
            self.prepare()
        self.assertFalse(self.stage.exists())

    def test_current_source_drift_rejected_before_clone(self):
        (self.kernel / 'kernel.c').write_text('not captured\n')
        with self.assertRaisesRegex(ValueError, 'tracked source'):
            self.prepare()
        self.assertFalse(self.stage.exists())

    def test_uncaptured_source_cannot_be_silently_dropped(self):
        (self.kernel / 'new_driver.c').write_text('not saved\n')
        with self.assertRaisesRegex(ValueError, 'uncaptured untracked'):
            self.prepare()
        self.assertFalse(self.stage.exists())

    def test_archive_content_still_checked_after_container_pin(self):
        self.archive(self.folder / 'untracked.tar.gz',
                     {'driver.c': b'x' * len(b'saved untracked source\n')})
        self.refresh()
        with self.assertRaisesRegex(ValueError, 'bytes changed'):
            self.prepare()
        self.assertFalse((self.stage / replay.RECEIPT).exists())

    def test_tar_traversal_rejected(self):
        data = b'escape'
        self.inputs['untracked']['../escape'] = self.desc(data)
        self.archive(self.folder / 'untracked.tar.gz', {'driver.c': b'saved untracked source\n', '../escape': data})
        self.refresh()
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse((self.delivery / 'escape').exists())

    def test_archive_git_metadata_rejected(self):
        path = self.folder / 'malicious.tar.gz'
        data = b'not git config'
        self.archive(path, {'.git/config': data})
        with self.assertRaises(ValueError):
            replay.extract(path, {'.git/config': self.desc(data)}, self.kernel)
        self.assertEqual(replay.git(self.kernel, 'rev-parse', 'HEAD').decode().strip(), self.commit)

    def test_external_archive_link_rejected(self):
        archive = self.folder / 'link.tar.gz'
        with tarfile.open(archive, 'w:gz') as tar:
            info = tarfile.TarInfo('escape'); info.type = tarfile.SYMTYPE
            info.linkname = '/etc/passwd'; info.mode = 0o777; tar.addfile(info)
        with self.assertRaisesRegex(ValueError, 'untrusted archive link'):
            replay.extract(archive, {'escape': {'link': '/etc/passwd', 'mode': 0o777}}, self.kernel)

    def test_cached_prepared_tree_cannot_activate(self):
        self.prepare()
        (self.stage / 'vmlinux').write_bytes(b'old cache')
        with self.assertRaisesRegex(ValueError, 'compiler cache'):
            replay.activate(self.sdk, self.stage, self.retained, self.terminal_job())
        self.assertFalse(self.retained.exists())

    def test_active_job_blocks_activation(self):
        self.prepare()
        job = self.delivery / 'job.json'; job.write_text(json.dumps({'phase': 'running'}))
        with self.assertRaisesRegex(ValueError, 'not finished'):
            replay.activate(self.sdk, self.stage, self.retained, job)
        self.assertFalse(self.retained.exists())

    def test_live_pid_blocks_even_with_terminal_receipt(self):
        self.prepare()
        job = self.terminal_job(); record = json.loads(job.read_text())
        record['pid'] = os.getpid(); job.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'still live'):
            replay.activate(self.sdk, self.stage, self.retained, job)

    def test_symlinked_stage_path_rejected(self):
        link = self.delivery / 'linked'; link.symlink_to(self.kernel, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, 'linked reconstruction'):
            replay.prepare(self.sdk, link, self.snapshot, self.snapshot_pin,
                           self.supplement, self.supplement_pin)

    def test_terminal_job_activation_retains_original_exactly(self):
        self.prepare()
        (self.kernel / 'old.o').write_bytes(b'old cache retained')
        record = replay.activate(self.sdk, self.stage, self.retained, self.terminal_job())
        self.assertEqual(record['phase'], 'activated')
        self.assertEqual((self.retained / 'old.o').read_bytes(), b'old cache retained')
        self.assertFalse((self.kernel / 'old.o').exists())
        replay.verify(self.kernel, record)


if __name__ == '__main__':
    unittest.main()
