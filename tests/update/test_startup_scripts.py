import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import check_startup_scripts as gate

class StartupScriptTests(unittest.TestCase):
    def test_current_and_invalid_rootfs_scripts(self):
        rc=os.environ.get('MIMI_RC_COMMON')
        if not rc:self.skipTest('selected SDK rc.common required')
        for failure in ('none','stale_rc','stale_init','missing','symlink','parent_alias','nonexec','writable'):
            with self.subTest(failure=failure),tempfile.TemporaryDirectory() as directory:
                root=Path(directory).resolve()
                for target,source in dict(gate.SCRIPTS,**{'etc/rc.common':rc}).items():
                    dest=root/target;dest.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copy2(Path(source) if source==rc else ROOT/source,dest)
                    dest.chmod(0o755)  # Package installation mode, independent of checkout mode.
                target=root/'etc/rc.common'
                if failure=='stale_rc':target.write_bytes(b'#!/bin/sh\nexit 0\n')
                if failure=='stale_init':(root/'etc/init.d/aitvbox-apps').write_bytes(b'#!/bin/sh\n')
                if failure=='missing':target.unlink()
                if failure=='symlink':target.rename(root/'saved');target.symlink_to(root/'saved')
                if failure=='parent_alias':
                    (root/'etc').rename(root/'elsewhere');(root/'etc').symlink_to(root/'elsewhere',target_is_directory=True)
                if failure=='nonexec':target.chmod(0o644)
                if failure=='writable':target.chmod(0o777)
                if failure=='none':self.assertEqual(gate.check(root,ROOT)['status'],'passed')
                else:
                    with self.assertRaisesRegex(ValueError, 'stale startup script' if failure.startswith('stale') else 'unsafe script mode' if failure in ('nonexec','writable') else 'missing or aliased script'):
                        gate.check(root,ROOT)
