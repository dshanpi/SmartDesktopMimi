"""Actual selected SDK lifecycle locking; host temporary fixtures only."""
import sys, pathlib, unittest, fcntl, os, subprocess, time, shutil, json
from pathlib import Path
from tests.platform import test_host_integration as m
from tests.sdk_source import services_source
class Faults(m.PlatformHostTests):
 @classmethod
 def setUpClass(cls):
  super().setUpClass()
  cls.locked_writer = cls.bin_dir / 'locked-writer'
  safe = services_source('src/safe_io.c')
  services_source('src/app_lock.h')
  m.run(['gcc', '-Wall', '-Wextra', '-Werror', '-I'+str(safe.parent),
         Path(__file__).with_name('app_locked_writer.c'), safe,
         '-Wl,--wrap=renameat', '-o', cls.locked_writer])
 def test_paused_atomic_writer_update_retry_preserves_acknowledged_value(self):
  source=self.workspace/'race-source';self.make_app(source,version='1.0.0');pkg=self.runtime/'race.aitapp';self.build_app(source,pkg)
  apps=self.runtime/'race-apps';trust=self.runtime/'race-trust';trust.mkdir();shutil.copy2(self.public_key,trust/'dev.pem');env=self.appctl_env(apps,trust)
  m.run([m.APPCTL,'install',pkg],env=env);value=apps/'com.100ask.hello/data/state.value';value.write_text('old')
  manifest=source/'manifest.json';v=json.loads(manifest.read_text());v['version']='1.1.0';manifest.write_text(json.dumps(v));self.build_app(source,pkg)
  ready=self.runtime/'writer-ready';cont=self.runtime/'writer-go'
  p=subprocess.Popen([str(self.locked_writer),str(value),str(apps)],env={**os.environ,'READY':str(ready),'CONTINUE':str(cont)},stdout=subprocess.PIPE,text=True)
  try:
   deadline=time.monotonic()+5
   while not ready.exists():
    self.assertIsNone(p.poll());self.assertLess(time.monotonic(),deadline);time.sleep(.001)
   result=m.run([m.APPCTL,'install',pkg],env=env,check=False);self.assertNotEqual(result.returncode,0);self.assertIn(b'busy',result.stderr)
   self.assertEqual(value.read_text(),'old');cont.touch();out=p.communicate(timeout=5)[0];self.assertEqual(p.returncode,0,out)
   m.run([m.APPCTL,'install',pkg],env=env);self.assertEqual(value.read_text(),'new')
   self.assertEqual(json.loads((value.parent.parent/'manifest.json').read_text())['version'],'1.1.0')
  finally:
   if p.poll() is None:p.kill();p.communicate()
 def test_policy_rejects_unsafe_descriptor_and_root(self):
  r=self.runtime/'lock-check';r.mkdir();other=self.runtime/'other-root';other.mkdir();link=self.runtime/'root-link';link.symlink_to(r,target_is_directory=True)
  def call(opened,named):
   return m.run(['/bin/sh','-c','exec 9<"$1"; exec "$2" lock-root "$3"','check',opened,self.app_policy,named],check=False)
  self.assertNotEqual(call(other,r).returncode,0)
  self.assertNotEqual(call(r,link).returncode,0)
  r.chmod(0o777);self.assertNotEqual(call(r,r).returncode,0);r.chmod(0o755)
  self.assertNotEqual(m.run([self.app_policy,'lock-root',r],check=False).returncode,0)
  self.assertEqual(call(r,r).returncode,0)
 def test_killed_holder_releases_lock(self):
  r=self.runtime/'killed-lock';r.mkdir();ready=self.runtime/'holder-ready'
  code='import os,fcntl,sys,time;fd=os.open(sys.argv[1],os.O_RDONLY);fcntl.flock(fd,fcntl.LOCK_EX);open(sys.argv[2],"w").close();time.sleep(30)'
  p=subprocess.Popen([sys.executable,'-c',code,str(r),str(ready)])
  fd=os.open(r,os.O_RDONLY)
  try:
   deadline=time.monotonic()+5
   while not ready.exists():
    self.assertIsNone(p.poll());self.assertLess(time.monotonic(),deadline);time.sleep(.001)
   with self.assertRaises(BlockingIOError):fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
   p.kill();p.wait(timeout=5);fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
  finally:
   os.close(fd)
   if p.poll() is None:p.kill();p.wait()

def load_tests(loader, tests, pattern):
    names = ['test_paused_atomic_writer_update_retry_preserves_acknowledged_value',
             'test_policy_rejects_unsafe_descriptor_and_root',
             'test_killed_holder_releases_lock']
    return unittest.TestSuite(Faults(name) for name in names)
