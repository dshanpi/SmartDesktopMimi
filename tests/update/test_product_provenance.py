"""Synthetic release metadata only; these fixtures are never deployable releases."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
import build_provenance as p
import provenance_fixture as fixture
from test_product_lock import lock_fixture


class ProductProvenanceTests(unittest.TestCase):
    def setup_product(self, directory, edition):
        self.directory = directory
        self.lock = dict(version='2.0.0', source_commit='a'*40, hardware_commit='b'*40, platform_id='a133',
                         partition_sha256='1'*64, toolchain_sha256='2'*64, sysroot_sha256='3'*64,
                         dependency_lock_sha256='4'*64, image_asset='SmartDesktopMimi-SystemImage-v2.0.0.img.gz')
        self.manifest = dict(version='2.0.0')
        self.factory = dict(commit='c'*40, version='1.0.0', package_sha256='d'*64)
        (directory / self.lock['image_asset']).write_bytes(b'HOST FIXTURE NOT AN IMAGE')
        fixture.system(directory, self.manifest, self.lock, self.factory)
        self.product = lock_fixture(edition)
        self.product['repositories']['application']['commit'] = self.lock['source_commit']
        self.product['repositories']['hardware']['commit'] = self.lock['hardware_commit']
        self.product['sdk_manifest_sha256'] = 'e'*64
        self.rootfs = dict(schema_version=1, status='passed', edition=edition,
                           product_lock_sha256='', rootfs_image_sha256='8'*64, physical='not_run', release_qualified=False)
        self.record = json.loads((directory / 'system-build.json').read_text())
        self.record.update(product_edition=edition, rootfs_image_sha256='8'*64)
        self.lock['product_edition'] = edition
        self.refresh()

    def refresh(self):
        d = self.directory
        fixture.write(d/'platform-product-lock.json', self.product)
        digest = fixture.sha(d/'platform-product-lock.json')
        self.rootfs['product_lock_sha256'] = digest
        fixture.write(d/'product-rootfs-check.json', self.rootfs)
        self.record.update(platform_product_lock_sha256=digest,
                           product_rootfs_check_sha256=fixture.sha(d/'product-rootfs-check.json'))
        fixture.write(d/'system-build.json', self.record)
        self.manifest['build_provenance_sha256'] = fixture.sha(d/'system-build.json')
        self.lock['platform_product_lock_sha256'] = digest
        self.lock['assets'] = {x.name:fixture.sha(x) for x in d.iterdir() if x.is_file()}

    def check(self):
        return p.system(self.directory, self.lock, self.manifest, self.factory,
                        lambda path:json.loads(path.read_text()))

    def test_system_signed_build_record_carries_public_and_private_source_locks(self):
        for edition in ('public', 'private'):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as directory:
                self.setup_product(Path(directory), edition)
                self.check()

    def test_rehashed_assets_cannot_hide_wrong_source_edition_or_sdk(self):
        for change in ('source', 'sdk', 'edition', 'qualified', 'image', 'manifest-hash', 'lock-hash'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                self.setup_product(Path(directory), 'private')
                if change == 'source':self.product['repositories']['application']['commit'] = 'f'*40
                elif change == 'sdk':self.product['sdk_manifest_sha256'] = '7'*64
                elif change == 'edition':self.rootfs['edition'] = 'public'
                elif change == 'qualified':self.rootfs['release_qualified'] = True
                elif change == 'image':self.rootfs['rootfs_image_sha256'] = '6'*64
                self.refresh()
                if change == 'manifest-hash':self.manifest['build_provenance_sha256'] = '5'*64
                elif change == 'lock-hash':self.lock['platform_product_lock_sha256'] = '4'*64
                with self.assertRaises(ValueError):self.check()

    def test_new_product_cannot_omit_its_lock_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            self.setup_product(Path(directory), 'public')
            self.lock['assets'].pop('platform-product-lock.json')
            with self.assertRaisesRegex(ValueError, 'missing product source lock'):self.check()


if __name__ == '__main__':unittest.main()
