import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools/update'))
import filesystem_image


class FilesystemImageTests(unittest.TestCase):
    def test_actual_ext4_damage_is_rejected_without_repairing_it(self):
        with tempfile.TemporaryDirectory() as temp:
            image=Path(temp)/'test.img'
            with image.open('wb') as f:f.truncate(8*1024*1024)
            subprocess.run(['mkfs.ext4','-q','-F',str(image)],check=True,stderr=subprocess.DEVNULL)
            before=hashlib.sha256(image.read_bytes()).hexdigest()
            self.assertEqual(filesystem_image.verify(image),'ext4')
            self.assertEqual(before,hashlib.sha256(image.read_bytes()).hexdigest())
            # Free the root directory inode in the bitmap, leaving its contents.
            subprocess.run(['debugfs','-w','-R','freei <2>',str(image)],check=True,
                           stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            damaged=hashlib.sha256(image.read_bytes()).hexdigest()
            self.assertNotEqual(before,damaged)
            with self.assertRaisesRegex(ValueError,'integrity check failed'):filesystem_image.verify(image)
            self.assertEqual(damaged,hashlib.sha256(image.read_bytes()).hexdigest())

    def test_missing_empty_and_linked_images_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);image=root/'empty';image.touch();link=root/'link';link.symlink_to(image)
            for p in [root/'missing',image,link,root]:
                with self.subTest(path=p),self.assertRaises(ValueError):filesystem_image.verify(p)


if __name__=='__main__':unittest.main()
