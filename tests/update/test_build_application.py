"""Admission checks for the controlled application builder."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
import build_application as builder


class ApplicationBuilderTests(unittest.TestCase):
    def test_sdk_selection_rejects_escape_and_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'a133-v1').mkdir()
            self.assertEqual(builder.selected_sdk(root,'a133-v1'),root/'a133-v1')
            (root/'alias').symlink_to(root/'a133-v1',target_is_directory=True)
            for name in ['../escape','/tmp/sdk','alias','missing','..']:
                with self.subTest(name=name),self.assertRaises(ValueError):builder.selected_sdk(root,name)

    def test_dirty_source_creates_no_build_output(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(builder,'source_state',return_value=dict(commit='a'*40,dirty=True)):
            output=Path(temp)/'candidate'
            with self.assertRaisesRegex(ValueError,'clean source'):builder.build(Path('/unused'), 'a'*64, output)
            self.assertFalse(output.exists())

    def test_output_cannot_escape_back_into_source_through_parent_link(self):
        with tempfile.TemporaryDirectory() as temp,patch.object(builder,'source_state',return_value=dict(commit='a'*40,dirty=False)):
            root=Path(temp);source=root/'source';source.mkdir();(root/'alias').symlink_to(source,target_is_directory=True)
            with patch.object(builder,'ROOT',source),self.assertRaisesRegex(ValueError,'outside'):
                builder.build(Path('/unused'),'a'*64,root/'alias/new')
            self.assertFalse((source/'new').exists())


if __name__=='__main__':unittest.main()
