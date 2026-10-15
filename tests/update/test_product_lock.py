"""Host fixtures for source locking and stale/mixed rootfs rejection."""
import copy
import json
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import lock_product_sources as p


def lock_fixture(edition='public'):
    repositories = {}
    for key, repo in {'application': p.PUBLIC, 'hardware': p.HARDWARE,
                      **({'private_product': p.PRIVATE} if edition == 'private' else {})}.items():
        repositories[key] = dict(repository=repo, commit='a' * 40, files={
            'fixture.c': dict(sha256='b' * 64, git_blob='c' * 40, git_mode='100644', bytes=1)})
    result = dict(schema_version=1, kind='platform-product-lock', requirement='REQ-MIMI-EDITION-067',
                  edition=edition, platform_id='a133', repositories=repositories,
                  sdk_manifest_sha256='d'*64, recipe_sha256='e'*64, source_graph_sha256='f'*64,
                  provider_tool_sha256='1'*64, compiler_sha256='2'*64, strip_tool_sha256=p.STRIP_SHA256,
                  programs={name: '3'*64 for name in ['provider_sha256', 'tuya_sha256', 'provider_record_sha256', 'tuya_record_sha256']},
                  rootfs_programs={'usr/bin/lv_backend': dict(sha256='4'*64, bytes=1, mode=0o755)},
                  evidence_scope='HOST FIXTURE ONLY; no build or device acceptance',
                  full_sdk_inventory='requires separate system build lock',
                  full_image='not_run', physical='not_run', release_qualified=False)
    if edition == 'private':
        result['rootfs_programs']['usr/bin/your_chat_bot_QIO_1.0.1.bin'] = dict(sha256='5'*64, bytes=1, mode=0o755)
    return result


class ProductSourceLockTests(unittest.TestCase):
    def test_model_rejects_private_public_mix_unknown_fields_and_false_acceptance(self):
        for change in ('private', 'field', 'accepted', 'image', 'path', 'program', 'hash', 'schema-type'):
            with self.subTest(change=change):
                record = lock_fixture()
                p.validate(record)
                if change == 'private':
                    record['repositories']['private_product'] = copy.deepcopy(record['repositories']['application'])
                elif change == 'field': record['secret'] = 'not a real credential'
                elif change == 'accepted': record['release_qualified'] = True
                elif change == 'image': record['full_image'] = 'passed'
                elif change == 'schema-type': record['schema_version'] = True
                elif change == 'path': record['repositories']['hardware']['files']['../escape'] = record['repositories']['hardware']['files'].pop('fixture.c')
                elif change == 'program': record['rootfs_programs']['usr/bin/your_chat_bot_QIO_1.0.1.bin'] = dict(sha256='6'*64, bytes=1, mode=0o755)
                else: record['compiler_sha256'] = 'missing'
                with self.assertRaises(ValueError): p.validate(record)

    def test_committed_blob_matching_rejects_dirty_source_alias_or_wrong_repository(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            def git(*args): return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)
            git('init', '-q');git('config', 'user.name', 'Host Fixture');git('config', 'user.email', 'host@example.invalid')
            git('remote', 'add', 'origin', 'https://github.com/' + p.PUBLIC + '.git')
            source = root / 'source.c';source.write_text('/* host fixture */');source.chmod(0o644)
            git('add', 'source.c');git('commit', '-qm', 'host fixture')
            selected, record = p.repository(str(root), p.PUBLIC)
            self.assertEqual(selected, root)
            self.assertEqual(p.blob(root, 'source.c', source)['sha256'], hashlib.sha256(source.read_bytes()).hexdigest())
            with self.assertRaises(ValueError):p.repository(str(root), p.PRIVATE)
            source.write_text('changed')
            with self.assertRaises(ValueError):p.repository(str(root), p.PUBLIC)
            with self.assertRaises(ValueError):p.blob(root, 'source.c', source)
            git('checkout', '--', 'source.c')
            alias = root / 'alias.c';alias.symlink_to(source)
            with self.assertRaises(ValueError):p.blob(root, 'source.c', alias)

    def rootfs(self, root, edition):
        record = lock_fixture(edition)
        for name, entry in record['rootfs_programs'].items():
            path = root / name;path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(('stripped-host-fixture:' + name).encode());path.chmod(0o755)
            entry.update(sha256=hashlib.sha256(path.read_bytes()).hexdigest(), bytes=path.stat().st_size)
        marker = root / 'etc/smartdesktopmimi/product-edition'
        marker.parent.mkdir(parents=True);marker.write_text(edition + '\n')
        return record

    def test_rootfs_rejects_stale_program_mode_edition_alias_and_credentials(self):
        for edition in ('public', 'private'):
            for change in ('program', 'mode', 'suid', 'edition', 'alias', 'credential'):
                with self.subTest(edition=edition, change=change), tempfile.TemporaryDirectory() as directory:
                    root = Path(directory).resolve();record = self.rootfs(root, edition)
                    p.check_rootfs(record, root)
                    binary = root / 'usr/bin/lv_backend'
                    if change == 'program':binary.write_bytes(b'old SDK cache program')
                    elif change == 'mode':binary.chmod(0o777)
                    elif change == 'suid':binary.chmod(0o4755)
                    elif change == 'edition':(root / 'etc/smartdesktopmimi/product-edition').write_text('wrong')
                    elif change == 'alias':
                        other = root / 'other';binary.rename(other);binary.symlink_to(other)
                    else:
                        path = root / 'factory/tuya/license.env';path.parent.mkdir(parents=True);path.write_text('host-only marker')
                    with self.assertRaises(ValueError):p.check_rootfs(record, root)

    def test_public_rootfs_rejects_stale_private_runtime_even_with_no_credential(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve();record = self.rootfs(root, 'public')
            path = root / 'usr/bin/your_chat_bot_QIO_1.0.1.bin';path.write_bytes(b'stale runtime')
            with self.assertRaisesRegex(ValueError, 'private device runtime'):p.check_rootfs(record, root)

    def test_rootfs_record_output_cannot_overwrite_lock_image_or_rootfs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            fs = root/'rootfs';fs.mkdir()
            record = self.rootfs(fs, 'private')
            lock = root/'lock.json';lock.write_text(json.dumps(record))
            image = root/'host-only.img';image.write_bytes(b'host input fixture, not a system image')
            command = [sys.executable, str(ROOT/'tools/update/product_lock.py'), '--lock', str(lock),
                       '--rootfs', str(fs), '--image', str(image)]
            protected = [lock, image, fs/'usr/bin/lv_backend']
            before = [path.read_bytes() for path in protected]
            for output in protected:
                result = subprocess.run(command+['--output',str(output)],capture_output=True,text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('PRODUCT ROOTFS BLOCKED', result.stderr)
                self.assertEqual([path.read_bytes() for path in protected], before)
            output = root/'readback.json'
            result = subprocess.run(command+['--output',str(output)],capture_output=True,text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIs(json.loads(output.read_text())['release_qualified'],False)


if __name__ == '__main__':unittest.main()
