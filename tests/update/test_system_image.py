"""Host-only archive/naming tests, never a flashable image or physical result."""
import gzip
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
import system_image as image


class SystemImageTests(unittest.TestCase):
    def test_product_name_and_bad_versions(self):
        for version in ['1.2.3','1.2.3-rc.4','1.2.3+build.7']:
            name=image.image_name(version)
            self.assertEqual(name,'SmartDesktopMimi-SystemImage-v'+version+'.img.gz')
            self.assertNotIn('a133',name.lower())
        for version in ['../bad','1.2','01.2.3','1.2.3-01','1.2.3\n','1.2.3/'+('x'*20),'1.2.3+'+('x'*200)]:
            with self.subTest(version=version),self.assertRaises(ValueError):image.image_name(version)

    def test_deterministic_archive_readback_and_duplicate_preservation(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d).resolve();raw=root/'board-chip.img';raw.write_bytes(b'HOST FIXTURE ONLY\n'*100)
            first=image.archive(raw,root/'first','1.2.3',image.digest(raw))
            second=image.archive(raw,root/'second','1.2.3',image.digest(raw))
            a=root/'first'/first['filename'];b=root/'second'/second['filename']
            self.assertEqual(a.read_bytes(),b.read_bytes())
            self.assertEqual(gzip.decompress(a.read_bytes()),raw.read_bytes())
            self.assertEqual(first['raw_sha256'],image.digest(raw))
            self.assertEqual(first['archive_sha256'],image.digest(a))
            self.assertEqual(first['physical_acceptance'],'not_run')
            before=a.read_bytes()
            with self.assertRaisesRegex(ValueError,'already exists'):image.archive(raw,a.parent,'1.2.3')
            self.assertEqual(a.read_bytes(),before)

    def test_wrong_pin_alias_and_midflight_change_leave_no_visible_image(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d).resolve();raw=root/'raw';raw.write_bytes(b'HOST ONLY')
            with self.assertRaisesRegex(ValueError,'external SHA256'):image.archive(raw,root/'wrong','1.0.0','0'*64)
            alias=root/'alias';alias.symlink_to(raw)
            with self.assertRaisesRegex(ValueError,'canonical'):image.archive(alias,root/'aliased','1.0.0')
            original=image.shutil.copyfileobj
            def mutate(inp,out,length):
                original(inp,out,length);raw.write_bytes(b'changed while archiving')
            with patch.object(image.shutil,'copyfileobj',side_effect=mutate),self.assertRaisesRegex(ValueError,'source changed'):
                image.archive(raw,root/'changed','1.0.0')
            self.assertFalse((root/'changed'/image.image_name('1.0.0')).exists())
            self.assertFalse(list((root/'changed').glob('.mimi-image-*')))

    def test_raw_decode_limit_is_enforced(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'image.gz';path.write_bytes(gzip.compress(b'x'*100))
            with self.assertRaisesRegex(ValueError,'exceeds pinned'):image.raw_digest(path,10)

    def test_lock_requires_exact_name_version_and_no_dual_images(self):
        name=image.image_name('1.2.3')
        lock=dict(version='1.2.3',image_asset=name,assets={name:'0'*64})
        self.assertEqual(image.release_image(lock),name)
        for change in [dict(image_asset='../wrong'),dict(version='1.2.4'),
                       dict(assets={name:'0'*64,'system.img.gz':'0'*64}),dict(assets={image.image_name('1.2.4'):'0'*64})]:
            with self.subTest(change=change),self.assertRaises(ValueError):image.release_image({**lock,**change})
        with self.assertRaisesRegex(ValueError,'image_asset'):
            image.release_image(dict(version='1.2.3',assets={name:'0'*64}))
        self.assertEqual(image.release_image(dict(version='1.2.3',assets={'system.img.gz':'0'*64})),'system.img.gz')


if __name__=='__main__':unittest.main()
