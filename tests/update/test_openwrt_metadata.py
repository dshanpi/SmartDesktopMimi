from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
import openwrt_metadata as gate


class MetadataGate(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.path = self.root / '.packageinfo'
        self.blocks = {name: ('Source-Makefile: package/subpackage/allwinner/custom/' + name +
                            '/Makefile\n\nPackage: ' + name + '\nVersion: 1.0\n@@\n')
                       for name in gate.PACKAGES}
        self.save()

    def save(self):
        self.path.write_text('\n'.join(self.blocks.values()))

    def test_complete_metadata_accepts_other_sdk_packages(self):
        self.path.write_text(self.path.read_text() + '\nSource-Makefile: package/libs/foo/Makefile\nPackage: foo\n')
        self.assertEqual(gate.verify(self.path)['packages'], list(gate.PACKAGES))

    def test_zero_exit_scanner_with_missing_recipe_is_rejected(self):
        # Actual vendor failure retains a Source-Makefile line with no package.
        for name in gate.PACKAGES:
            with self.subTest(name=name):
                original = self.blocks[name]
                self.blocks[name] = original.split('\nPackage: ')[0] + '\n'
                self.save()
                with self.assertRaisesRegex(ValueError, name): gate.verify(self.path)
                self.blocks[name] = original

    def test_duplicate_package_is_rejected(self):
        self.path.write_text(self.path.read_text() + self.blocks[gate.PACKAGES[0]])
        with self.assertRaisesRegex(ValueError, 'duplicate'): gate.verify(self.path)

    def test_wrong_source_cannot_supply_product_name(self):
        self.path.write_text(self.path.read_text().replace('custom/aitvbox-suite/', 'custom/unrelated/'))
        with self.assertRaisesRegex(ValueError, 'wrong-source'): gate.verify(self.path)

    def test_unscoped_package_is_rejected(self):
        self.blocks[gate.PACKAGES[0]] = 'Package: ' + gate.PACKAGES[0] + '\n'
        self.save()
        with self.assertRaises(ValueError): gate.verify(self.path)

    def test_missing_linked_or_invalid_utf8_metadata_is_rejected(self):
        self.path.unlink()
        with self.assertRaises(ValueError): gate.verify(self.path)
        other = self.root/'other'; other.write_text('fixture'); self.path.symlink_to(other)
        with self.assertRaises(ValueError): gate.verify(self.path)
        self.path.unlink(); self.path.write_bytes(b'\xff')
        with self.assertRaises(ValueError): gate.verify(self.path)
