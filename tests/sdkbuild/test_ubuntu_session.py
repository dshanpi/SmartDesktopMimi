"""Session negative-boundary tests; real GUI lifecycle uses the separate harness."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('ubuntu_session',ROOT/'services/mimi-ubuntu-provider/session.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)

class SessionBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.runtime=self.root/'run';self.runtime.mkdir(mode=0o700)
    def test_private_directory_and_canonical_paths(self):
        s.private_directory(self.runtime)
        self.runtime.chmod(0o755)
        with self.assertRaises(ValueError):s.private_directory(self.runtime)
        self.runtime.chmod(0o700);alias=self.root/'alias';alias.symlink_to(self.runtime)
        with self.assertRaises(ValueError):s.private_directory(alias)
    def test_hardlink_symlink_and_duplicate_json_rejected(self):
        p=self.runtime/'state.json';p.write_text('{}');p.chmod(0o600);os.link(p,self.runtime/'hard')
        with self.assertRaises(ValueError):s.read_json(p,True)
        (self.runtime/'hard').unlink();q=self.runtime/'link';q.symlink_to(p)
        with self.assertRaises(ValueError):s.read_json(q,True)
        p.write_text('{"a":1,"a":2}')
        with self.assertRaises(ValueError):s.read_json(p,True)
    def test_atomic_writer_does_not_follow_existing_temporary_path(self):
        target=self.root/'preserve';target.write_text('sentinel');(self.runtime/'state.tmp').symlink_to(target)
        with self.assertRaises(FileExistsError):s.write_private(self.runtime/'state','new')
        self.assertEqual(target.read_text(),'sentinel');self.assertFalse((self.runtime/'state').exists())
    def test_health_rejects_wrong_release_before_query(self):
        p=self.runtime/'session.json';s.write_private(p,json.dumps(dict(release='/other',ready=True)))
        with patch.object(s,'query') as query,self.assertRaises(ValueError):s.health(self.runtime,self.root)
        query.assert_not_called()
    def test_health_rejects_actual_pid_with_wrong_executable(self):
        myself=s.process_identity(os.getpid());state=dict(release=str(self.root),ready=True,supervisor=myself,desktop=myself,provider=myself)
        s.write_private(self.runtime/'session.json',json.dumps(state))
        with patch.object(s,'query') as query,self.assertRaisesRegex(ValueError,'executable'):s.health(self.runtime,self.root)
        query.assert_not_called()
    def test_stale_health_rejected_for_actual_selected_process(self):
        (self.root/'bin').mkdir();binary=self.root/'bin/lvglsim';shutil.copy2('/bin/sleep',binary)
        child=subprocess.Popen([str(binary),'30']);self.addCleanup(lambda:(child.terminate(),child.wait()) if child.poll() is None else None)
        identity=s.process_identity(child.pid);state=dict(release=str(self.root),ready=True,supervisor=s.process_identity(os.getpid()),desktop=identity,provider=identity)
        s.write_private(self.runtime/'session.json',json.dumps(state))
        h=dict(pid=child.pid,monotonic_ms=int(time.monotonic()*1000)-6000,backend_reply_ms=1,activity_ready=True,busy=False)
        s.write_private(self.runtime/'ui-health.json',json.dumps(h))
        with patch.object(s,'query') as query,self.assertRaisesRegex(ValueError,'stale'):s.health(self.runtime,self.root)
        query.assert_not_called()

if __name__=='__main__':unittest.main()
